# timesyncd

## Description

This linux daemon implements a server/client TCP interface for synchronizing the wall clocks (i.e., system clocks) of computers on a WiFi network.
This program was developed with mobile robots in mind: We use it to synchronize the clocks of several embedded computers on an [autonomous surface vehicle (ASV)](http://senseplatypus.com/lutra-airboat/) running [ROS](https://www.ros.org/), to avoid problems with data synchronization (i.e., diverging timestamps from sensors connected to different computers). The program tries to obtain time from a [Ublox GNSS receiver](https://www.u-blox.com/en/product/neo-m8-series) whenever possible, falling back to local time when the GPS is unavailable. We use EMLID's [Navio2 GPS driver library](https://github.com/emlid/Navio2/blob/master/C%2B%2B/Navio/Common/Ublox.h), extended to parse [UBX-NAV-TIMEGPS](https://github.com/disaster-robotics-proalertas/timesyncd/blob/c50f7d2e04f5c30842738db26687f4ac468326c9/include/Navio/Common/Ublox.cpp#L627) messages.

This program is similar in functionality to an [NTP server](https://en.wikipedia.org/wiki/Network_Time_Protocol), but with two main concerns regarding our mobile base's limitations:

* _Computing power_: As the embedded computers on our boat have limited computing power, the daemon must have a small CPU footprint, as to not overburden the processor;
* _Bandwidth_: As ROS is running on the boat, control and perception packets have priority, and the daemon must use as little bandwidth as possible.

The daemon works in two modes: _server_ and _client_. When in _server_ (or "master") mode, the daemon first tries to obtain reference time from a GPS satellite, via a Ublox NEO-M8 receiver, up to a timeout. If successful, it sets the master computer's system clock to the time specified by the GPS (converted to UTC). If unsuccessful (e.g., GPS error or timeout), the server does nothing and does not change the computer's system clock. Then, the server binds and listens to a TCP port, accepting connections from any IPv4 address in the same WiFi network. As the function for accepting new connections is _blocking_, the daemon idles while waiting for new connections, reducing its processor footprint. When a new connection is established, the server obtains its own wall clock time in UTC and sends this time value to the requesting client. Finally, the server returns to idle as it waits for new connections.

In _client_ mode, the daemon attempts to connect to the server directly through the server IPv4 address. The client obtains the server's IP address via the computer's host table, and according to the _MASTER\_HOSTNAME_ parameter in the daemon's [configuration file](https://github.com/disaster-robotics-proalertas/timesyncd/blob/master/timesyncd.conf). For example, if _MASTER\_HOSTNAME=localhost_, the client will attempt to connect to the "127.0.0.1" address (assuming localhost is "127.0.0.1" by default). The client will attempt connection up to a timeout. If successful, it will receive an UTC time from the server and set its computer's system clock to that time value, completing the synchronization. In the case of timeout, the client daemon will exit in a failed state, thus eliminating any unnecessary CPU usage.

The operating mode, master hostname, TCP port and timeout values can be changed in the [configuration file](https://github.com/disaster-robotics-proalertas/timesyncd/blob/master/timesyncd.conf).

Although the daemon should work on any platform meeting the dependencies (assuming also proper daemon configuration), please be advised that **we have tested the program only on our setup, and thus we can not guarantee it will work on yours**.

## Dependencies

* gcc (tested on version 5.4.0)
* make (tested on version 4.1)
* cmake (tested on version 3.5.1)
* systemd, or equivalent

## Installation

Install the dependencies (debian/ubuntu example):

```
sudo apt-get install gcc make cmake systemd
```

Clone this repository to your device (for example, to your HOME folder):

```
git clone https://github.com/disaster-robotics-proalertas/timesyncd $HOME
```

Go to the repository's root folder and create a "build" folder:

```
cd $HOME/timesyncd
mkdir -p build
```

Go into the build folder and run "cmake", indicating the "/usr" folder as install prefix and pointing to the root folder:

```
cmake -DCMAKE_INSTALL_PREFIX=/usr ..
```

Run "make" to compile the daemon:

```
make
```

Run "make install" with root privileges to install the program (will copy the binary, config file and service file to the "/usr" folder):

```
sudo make install
```

## Usage

After adjusting the [configuration file](https://github.com/disaster-robotics-proalertas/timesyncd/blob/master/timesyncd.conf) (see below) to reflect your setup, you can run the daemon with _systemd_:

```
sudo systemctl start timesyncd.service

OR

sudo service timesyncd start
```

Alternatively, you can set the daemon to start on boot:

```
sudo systemctl enable timesyncd.service
```

To see the status of the daemon, run:

```
sudo systemctl status timesyncd.service

OR

sudo service timesyncd status
```

## [Configuration file](https://github.com/disaster-robotics-proalertas/timesyncd/blob/master/timesyncd.conf) parameters

If the daemon is installed using "sudo make install" as described above, the configuration file's path will be "/etc/timesyncd/timesyncd.conf". Use a text editor (e.g., gedit, vi, nano...) to change the parameters stated below.

* MODE: Selects server or client mode. Any other setting will result in a failed state.
* MASTER_HOSTNAME: Parameter for client, indicates the hostname (in your /etc/hosts) for the computer running the server/master daemon. _Not used in server mode_.
* PORT: Indicates the TCP port to be used. Change this to an available port on your device. Default is 12321, a random palindrome port.
* TIMEOUT: In server mode, indicates the time the server will wait for a GPS reference time (GPST). In client mode, indicates the total time the client will attempt to connect with the server before exiting.

## Acknowledgements

* [EMLID](https://emlid.com/) : [Navio2 UBlox NEO-M8 driver library](https://github.com/emlid/Navio2);
* [Jiri Hnidek](https://github.com/jirihnidek) : [Daemon C example](https://github.com/jirihnidek/daemon)

## Contributors

* [Renan Maidana](https://github.com/rgmaidana)
