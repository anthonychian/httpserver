CC=gcc
CFLAGS=-I -lpthread

program: httpserver

httpserver: httpserver.cpp
	$(CC) -o httpserver httpserver.cpp
