/* The MIT License (MIT)

Copyright (c) 2019 Renan Maidana.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE. 

Author: Renan Maidana (renan.maidana@edu.pucrs.br) */

// C headers
#include <arpa/inet.h> 
#include <errno.h>
#include <netinet/in.h> 
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

// C++ headers
#include <algorithm>
#include <fstream>
#include <string>

// NAVIO headers
#include <Ublox.h>

using namespace std;

#define DAEMON_NAME "timesyncd"

// Configuration file variables
static string mode;
static string master_hostname;
static int port;
static int timeout;

// Helper function to exit daemon and close the logs in case of error
void exit_daemon(int priority, const char *msg){
    syslog(priority, "%s", msg);
    syslog(LOG_INFO, "Closing daemon");
    closelog();
    sleep(1);       // Delay to avoid race condition in journald ---> https://bbs.archlinux.org/viewtopic.php?id=170495
    exit(EXIT_FAILURE);
}

// Read and parse configuration file
int read_conf_file(char *filename){
	// Instantiate and open file as input
	ifstream conf_file(filename);
	
	// Check if file was open
	if (!conf_file.is_open()){
        char *msg;
        sprintf(msg, "Could not open configuration file %s: %s\n", filename, strerror(errno));
		exit_daemon(LOG_ERR, msg);
	}

	// Look for arguments
	string s;
	string delim = "=";
	size_t pos;
	while(getline(conf_file, s)){
	   s.erase(remove(s.begin(), s.end(), ' '), s.end());	// Remove whitespaces from line read
	   pos = s.find(delim);
	   string argname = s.substr(0, pos);
	   string argval = s.substr(pos+1, s.length());
       
       // Transform argument name to lowercase
	   transform(argname.begin(), argname.end(), argname.begin(), [](unsigned char c){ return tolower(c); });

	   if (argname == "mode"){
           mode = argval;
       } else if (argname == "master_hostname"){
		   master_hostname = argval.c_str();
	   } else if (argname == "port"){
		   port = atoi(argval.c_str());
	   } else if (argname == "timeout") {
		   timeout = atoi(argval.c_str());
	   }
	}
	
	conf_file.close();

	return 0;
}

void timesync_server_daemon(){
    // Vector to hold GPS data
    std::vector<double> gpsdata;
    // Create ublox class instance
    Ublox gps;

    // Here we test connection with the receiver. Function testConnection() waits for a ubx protocol message and checks it.
    // If there's at least one correct message in the first 300 symbols the test is passed
    if(gps.testConnection())
    {
        syslog(LOG_INFO, "Ublox GPS test: OK\n");
        if (!gps.configureSolutionRate(5000))
        {
            syslog(LOG_WARNING, "Setting new rate: FAILED\n");
        }
        
        // Try to get GPS time for 10 seconds
        int wait_cnt = 0;
        while (wait_cnt <= timeout){
            if (gps.decodeSingleMessage(Ublox::NAV_TIMEGPS, gpsdata) == 1)
            {
                // NAV-TIMEGPS messages arrive as (gpsdata[0..3]):
                // GPS Millisecond time of week
                // GPS Nanosecond time of week
                // GPS Week number of navigation epoch
                // GPS Leap seconds
                
                // Convert from GPS time to unix UTC since epoch
                // time of week = GPS Milliseconds of week + nanoseconds of week + leap seconds
                // Time since epoch = time of week + (week number * 7 * 24 * 3600)[week offset]
                double timeofweek = gpsdata[0]/1000.0 + gpsdata[1]/1000000000.0 + (int)gpsdata[3];
                // 2 * 1024 = 2048 -> Two rollovers of 1024 weeks since Jan. 1980 (1999 and 2019), another rollover in approx. 20 years
                int weeknumber = (int)gpsdata[2] + 2048; 
                time_t utcepoch = (long)(timeofweek + (int)gpsdata[3] + (weeknumber*7*24*3600)); 
                utcepoch += 315964800;  // Offset for GPS time, as it started in Jan. 6, 1980 (UTC is Jan 1, 1970)

                syslog(LOG_INFO, "Obtained GPS epoch: %ld", utcepoch);

                // Set the system time according to the epoch
                struct timeval tv;
                struct timezone tz;     // Time
                tv.tv_sec = utcepoch;
                tv.tv_usec = 0;
                if(settimeofday(&tv, NULL) < 0){
                    exit_daemon(LOG_ERR, "Could not set system time according to GPS time");
                } else {
                    syslog(LOG_INFO, "Set system time according to GPS time");
                }

                break;
            }
            sleep(1); // Sleep for 1 s
            wait_cnt++;
        }
        // If unsuccessful for whatever reason, don't update the system time according to GPS
        if (wait_cnt > timeout)
            syslog(LOG_WARNING, "Could not set time according to GPS");
    } else {
        syslog(LOG_WARNING, "Could not connect to GPS");
    }

    // Create and bind socket
    int server_fd; 
    struct sockaddr_in address; 
    int addrlen = sizeof(address);
       
    // Creating socket file descriptor 
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        exit_daemon(LOG_ERR, "Could not create socket");
    
    address.sin_family = AF_INET; 
    address.sin_addr.s_addr = INADDR_ANY; 
    address.sin_port = htons( port ); 
       
    // Forcefully attaching socket to the port 8080 
    if (bind(server_fd, (struct sockaddr *)&address,  
                                 sizeof(address))<0) {
        char *msg;
        sprintf(msg, "Could not bind socket in port %d", port);
        exit_daemon(LOG_ERR, msg);
    } 

    if (listen(server_fd, 3) < 0) {
        char *msg;
        sprintf(msg, "Could not listen to port %d", port);
        exit_daemon(LOG_ERR, msg);
    }
    
    // Wait for requests for time update from clients
    while(true){
        char buf[1024] = {0};

        syslog (LOG_DEBUG, "Waiting for connection...");
        int new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        // Get IPv4 address of client
        struct sockaddr_in* pV4Addr = (struct sockaddr_in*)&address;
        struct in_addr clientaddr = pV4Addr->sin_addr;
        char clientaddrstr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientaddr, clientaddrstr, INET_ADDRSTRLEN);
        
        // Identify who requested the time by IPv4 address
        syslog (LOG_DEBUG, "Request received from %s, sending current time", clientaddrstr);
        
        // Get current time (may be synchronized with GPS or not)
        time_t now = time(NULL);
        
        // Convert time to string and send it back to client
        char out [100];
        sprintf(out, "%ld", now);
        send(new_socket, out, strlen(out), 0);

        // Debug messages
        syslog(LOG_DEBUG, "System time (string): %s", out);
        syslog(LOG_DEBUG, "System time (long int): %ld", now);
    }
}

void timesync_client_daemon(){
    // Create and bind UDP socket
    syslog(LOG_DEBUG, "Creating TCP socket");
    int sockfd;
    struct sockaddr_in servaddr;
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
        exit_daemon(LOG_ERR, "Could not create socket");

    // Get server IP address from hostname (/etc/hosts)
    char *server_IP;
    struct hostent *serv_host_entry;
    serv_host_entry = gethostbyname(master_hostname.c_str());
    server_IP = inet_ntoa(*((struct in_addr*)
                           serv_host_entry->h_addr_list[0]));
    if(inet_pton(AF_INET, server_IP, &servaddr.sin_addr)<=0)
        exit_daemon(LOG_ERR, "Invalid address or address not supported");

    servaddr.sin_family    = AF_INET;                   // Indicates IPv4 address
    servaddr.sin_port = htons(port);                    // Bind to port 12321
    // DEBUG: Print server IP from /etc/hosts
    syslog(LOG_DEBUG, "Server address: %s", (char*)inet_ntoa((struct in_addr)servaddr.sin_addr)); 
    
    // Try to request time to server every 1 second
    // Quit program if timeout
    int wait_cnt = 0;
    syslog(LOG_DEBUG, "Will attempt to connect to server for %d seconds", timeout);
    while(connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
        sleep(1);
        wait_cnt++;
        if (wait_cnt > timeout){
            // If no connection was established, sleep for some time and try again
            char errmsg[100]; sprintf(errmsg, "Server connection timeout, retrying in %ld seconds", timeout*10);
            syslog(LOG_WARNING, "%s", errmsg);
            wait_cnt = 0;
            sleep(timeout*10);
        }
    }

    // Request time from server
    syslog(LOG_DEBUG, "Connected, requesting time from server");

    // Wait for an answer
    char buffer[1024] = {0};
    int resp = read( sockfd , buffer, 1024);
    syslog(LOG_DEBUG, "Response length: %d", resp);
    
    // Convert server time from string to long int
    time_t server_time = atol(buffer);
    char logmsg[100]; sprintf(logmsg, "Received time from server: %ld", server_time);
    syslog(LOG_INFO, "%s", logmsg);
    
    // Set system time to sync with server
    struct timeval tv;
    tv.tv_sec = server_time;
    tv.tv_usec = 0;
    if(settimeofday(&tv, NULL) < 0){
        close(sockfd);
        exit_daemon(LOG_ERR, "Could not set system time");
    } else {        
        char logmsg[100]; sprintf(logmsg, "Setting system time to %ld UTC", server_time);
        syslog(LOG_DEBUG, "%s", logmsg);
    }

    // Close socket
    close(sockfd);
}

// Helper function to convert string to lowercase
string convertLowerCase(string str){
    string res = NULL;
    for (int i = 0; i < str.length(); i++)
        res += tolower(str[i]);
    return res;
}

int main(int argc, char *argv[]){
    // start_daemon();

    /* Open the log file */
    setlogmask (LOG_UPTO (LOG_INFO));
    openlog (DAEMON_NAME, LOG_CONS | LOG_PID | LOG_NDELAY | LOG_NOWAIT, LOG_LOCAL1 | LOG_USER | LOG_DAEMON);

    // Read configuration file
    if(read_conf_file(argv[1]) < 0)
        exit_daemon(LOG_ERR, "Could not read configuration file");    
    
    // Select server or client mode according to "mode" argument in config file
    // Transform mode argument value to lowercase
    transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c){ return tolower(c); });
    if (mode == "server") {
        syslog(LOG_INFO, "Starting daemon in server mode");
        timesync_server_daemon();
    } else if (mode == "client") {
        syslog(LOG_INFO, "Starting daemon in client mode");
        timesync_client_daemon();
    } else {
        exit_daemon(LOG_ERR, "Invalid mode, must be \"server\" or \"client\"");
    }

    // Gracefully exit in success
    closelog();
    return (EXIT_SUCCESS);
}
