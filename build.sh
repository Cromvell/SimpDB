# Build debugee
g++ -g -std=c++14 -O0 -gdwarf-2 -fno-omit-frame-pointer debugee.cpp -o debugee

g++ -g -o mydbg main.cpp lib/linenoise.a -I/usr/local/include/libelfin -L/usr/local/lib -ldwarf++ -lelf++
