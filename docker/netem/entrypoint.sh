#!/bin/bash

# network configuration
sysctl -w net.ipv4.conf.all.proxy_arp=1
sysctl -w net.ipv4.ip_forward=1

tc qdisc add dev client-net0 root handle 1:0 netem delay 300ms limit 1000000 #forward link
tc qdisc add dev server-net0 root handle 1:0 netem delay 300ms limit 1000000 #return link

tc qdisc add dev client-net0 parent 1:1 handle 10: tbf rate 50Mbit burst 4542 limit 3750000 #forward link (one BDP)
tc qdisc add dev server-net0 parent 1:1 handle 10: tbf rate  5Mbit burst 4542 limit  375000 #return link (one BDP)

ip addr
ip route
tc qdisc show

sysctl -w net.ipv4.tcp_rmem="8192 2100000 8400000"
sysctl -w net.ipv4.tcp_wmem="8192 2100000 8400000"

sleep infinity