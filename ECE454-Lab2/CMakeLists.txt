cmake_minimum_required(VERSION 3.5)
project(ECE454_Lab2)

set(CMAKE_C_STANDARD 99)
# SET(CMAKE_C_FLAGS "-Os")
# SET(CMAKE_C_FLAGS "-O3 -pg")
# SET(CMAKE_C_FLAGS "-O2")
SET(CMAKE_C_FLAGS "-g")
# SET(CMAKE_C_FLAGS "-O3 -g -fprofile-arcs -ftest-coverage")
# SET(CMAKE_C_FLAGS "-g -pg -no-pie")

set(SOURCE_FILES ECE454-Lab2.c)
add_executable(ECE454_Lab2 ${SOURCE_FILES}
        loadbmp.h
        utilities.c utilities.h
        fcyc.c fcyc.h clock.c clock.h
        implementation_reference.c implementation_reference.h
        implementation.c implementation.h)
