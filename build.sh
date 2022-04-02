#!/bin/bash

LIB_ARGS="-I/usr/local/include/libelfin -L/usr/local/lib -ldwarf++ -lelf++"

#g++ -g debugee.cpp -o debugee

if [ "$#" -ge 1 ] ; then
    case $1 in
        -lib)
            echo "Building library"
            # Mode 1: compile debugger as a library
            g++ -g -c debugger.cpp $LIB_ARGS -o library_debugger.o
            ar rcs library_debugger.a library_debugger.o
            rm library_debugger.o
            # And optionally link it with another program
            #g++ -g test.cpp library_debugger.a $LIB_ARGS -o test
            ;;

        -gui)
            echo "Building stand-alone program"
            # Mode 2: compile debugger into stand-alone program
            g++ `sdl2-config --cflags` -I imgui/ main.cpp `sdl2-config --libs` -lGL -ldl -lpthread $LIB_ARGS -o debugger_gui
            ;;

        -test)
            echo "Building tests"
            # Mode 3: Building test program
            g++ -g test.cpp $LIB_ARGS -o test
            ;;

        *)
            echo "ERROR: Unknown argument. Building nothing"
            ;;
        esac
else
    echo "ERROR: Specify version to build: -lib, -gui, -test"
fi
