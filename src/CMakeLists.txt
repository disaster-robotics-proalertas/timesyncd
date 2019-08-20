set (timesyncd_src timesyncd.cpp)

# Make build flags compiler specific
if (CMAKE_COMPILER_IS_GNUCC)
	if (CMAKE_BUILD_TYPE STREQUAL "Debug")
		set (CMAKE_C_FLAGS "-D_REETRANT -ggdb -fPIC -Wall -Wextra -pedantic -O0")
	elseif( CMAKE_BUILD_TYPE STREQUAL "Release" )
		set (CMAKE_C_FLAGS "-D_REETRANT -DNDEBUG -fPIC -Wall -Wextra -pedantic -O3")
	endif ()
endif (CMAKE_COMPILER_IS_GNUCC)

find_path( UBLOX_INCLUDE_DIR Ublox.h 
  HINTS ../include/Navio/Common
  DOC "Ublox lib include dir" )
IF( NOT UBLOX_INCLUDE_DIR )
  MESSAGE( FATAL_ERROR "Ublox.h not found!" )
ENDIF()

set (UBLOX_INCLUDE_FLAGS "-I${UBLOX_INCLUDE_DIR}")
set (UBLOX_SOURCE_FILE "${UBLOX_INCLUDE_DIR}/Ublox.cpp")
set_source_files_properties(${UBLOX_SOURCE_FILE} PROPERTIES COMPILE_FLAGS "${UBLOX_INCLUDE_FLAGS} -std=c++11")

add_library (ublox SHARED ${UBLOX_SOURCE_FILE})

set_source_files_properties(${timesyncd_src} PROPERTIES COMPILE_FLAGS "${UBLOX_INCLUDE_FLAGS} -std=c++11 -lublox")
add_executable (timesyncd ${timesyncd_src})
target_link_libraries(timesyncd ublox)

# The rule to install timesyncd binary
install (TARGETS timesyncd
	RUNTIME
	DESTINATION bin)

install (TARGETS ublox
	LIBRARY DESTINATION lib
	PUBLIC_HEADER DESTINATION lib)