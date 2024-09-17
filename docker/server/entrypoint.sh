#!/bin/bash

# network configuration
ip route add 192.168.90.0/24 via 192.168.91.3

sysctl -w net.ipv4.tcp_rmem="8192 2100000 8400000"
sysctl -w net.ipv4.tcp_wmem="8192 2100000 8400000"

/picoquic/picoquicdemo -p 4443 -G cubic -q .
sleep infinity