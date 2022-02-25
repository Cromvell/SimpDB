#!/bin/bash

LIB_ARGS="-I/usr/local/include/libelfin -L/usr/local/lib -ldwarf++ -lelf++"

if [ "$#" -ge 1 ] ; then
  if [ $1 == "-lib" ] ; then
    echo "Building library"
    # Mode 1: compile debugger as a library
    g++ -g -c debugger.cpp $LIB_ARGS -o library_debugger.o
    ar rcs library_debugger.a library_debugger.o
    rm library_debugger.o
    # And optionally link it with another program
    #g++ -g test.cpp library_debugger.a $LIB_ARGS -o test
  else
    echo "ERROR: Unknown argument. Building nothing"
  fi
else
  echo "Building stand-alone program"
  # Mode 2: compile debugger into stand-alone program
  # c++ `sdl2-config --cflags` -I imgui/ gui.cpp imgui/*.cpp `sdl2-config --libs` -lGL -ldl $LIB_ARGS
  g++ `sdl2-config --cflags` -I imgui/ gui.cpp imgui/imgui_impl_opengl3.cpp `sdl2-config --libs` -lGL -ldl $LIB_ARGS -o debugger_gui
  #g++ -g test.cpp $LIB_ARGS -o test
fi
