# HTTP server with multithreading, logging, and caching

HTTP server is a program for a server that listens to messages (headers) from a client and sends replies back (response headers) for either PUT or GET requests with the addition of multithreading to enable concurrent requests from the client/multiple requests at a time. Also offers logging of each request and caching with a FIFO linked list

## Installation

No installation required just compile and ready to use

## Usage

To build httpserver.cpp use the command 'make'

Use these flags to compile
clang++ -std=gnu++11 -Wall -Wextra -Wpedantic -Wshadow -lphthreads

For usage, type ./httpserver 'hostname' 'PORT' '-N' 'number' '-l' 'logfile' which runs the server specifying the hostname, port number, number of threads, and file to write for logging

Server works with a client - testing was used with curl

asgn2 directory also contains 2 more text files used for testing in the test file folder:

logfile.txt
test.txt
test1.txt
test2.txt
test3.txt
test4.txt
test5.txt
ABCDEFarqdeXYZxyzf012344-ab
ABCDEFarqdeXYZxyzf012345-ab
ABCDEFarqdeXYZxyzf012346-ab
ABCDEFarqdeXYZxyzf012347-ab
