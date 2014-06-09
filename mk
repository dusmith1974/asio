#!/bin/bash
rm -f async_tcp_client blocking_udp_client blocking_tcp_client server
g++ -pthread async_tcp_client.cc /usr/lib/x86_64-linux-gnu/libboost_system.a /usr/lib/x86_64-linux-gnu/libboost_thread.a -o async_tcp_client \
&& g++ -pthread blocking_udp_client.cc /usr/lib/x86_64-linux-gnu/libboost_system.a /usr/lib/x86_64-linux-gnu/libboost_thread.a -o blocking_udp_client \
&& g++ -pthread blocking_tcp_client.cc /usr/lib/x86_64-linux-gnu/libboost_system.a /usr/lib/x86_64-linux-gnu/libboost_thread.a -o blocking_tcp_client \
&& g++ -pthread server.cc /usr/lib/x86_64-linux-gnu/libboost_system.a /usr/lib/x86_64-linux-gnu/libboost_thread.a -o server \
