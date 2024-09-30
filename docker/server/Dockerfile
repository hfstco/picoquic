FROM ubuntu:24.04

# update
RUN DEBIAN_FRONTEND=noninteractive apt-get update -y
RUN DEBIAN_FRONTEND=noninteractive apt-get upgrade -y

# net tools
RUN DEBIAN_FRONTEND=noninteractive apt-get update -y
RUN DEBIAN_FRONTEND=noninteractive apt-get install iproute2 iputils-ping iperf3 -y

# build tools
RUN DEBIAN_FRONTEND=noninteractive apt-get update -y
RUN DEBIAN_FRONTEND=noninteractive apt-get install build-essential git cmake pkg-config libssl-dev iputils-ping -y

# picoquic
RUN git clone https://github.com/hfstco/picoquic.git /picoquic
WORKDIR /picoquic
RUN git fetch
RUN git checkout cr
RUN cmake -DPICOQUIC_FETCH_PTLS=Y .
RUN make

# entrypoint
COPY entrypoint.sh /entrypoint.sh
RUN ["chmod", "+x", "/entrypoint.sh"]
ENTRYPOINT ["/entrypoint.sh"]