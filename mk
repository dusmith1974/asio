#!/bin/bash
rm -f client server
g++ -std=c++11 -pthread client.cc /usr/lib/x86_64-linux-gnu/libboost_system.a /usr/lib/x86_64-linux-gnu/libboost_thread.a -o client \
&& g++ -std=c++11 -pthread server.cc /usr/lib/x86_64-linux-gnu/libboost_system.a /usr/lib/x86_64-linux-gnu/libboost_thread.a -o server \
