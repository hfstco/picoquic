#!/bin/bash

# network configuration
ip route add 192.168.91.0/24 via 192.168.90.3

sysctl -w net.ipv4.tcp_rmem="8192 2100000 8400000"
sysctl -w net.ipv4.tcp_wmem="8192 2100000 8400000"

/picoquic/picoquicdemo -G cubic -n h3 -q . 192.168.91.3 4443 /100000000
sleep 3
/picoquic/picoquicdemo -G cubic -n h3 -q . 192.168.91.3 4443 /100000000
sleep infinity