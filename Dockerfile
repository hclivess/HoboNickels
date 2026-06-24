# syntax=docker/dockerfile:1
#
# Multi-stage build for the HoboNickels daemon (HoboNickelsd).
#
#   docker build -t hobonickels .
#   docker run --rm hobonickels --help
#   docker run -d -v hbn-data:/data --name hbn hobonickels
#
# RPC/P2P ports: 7372 (P2P), 7373 (RPC).

# ---- Build stage ---------------------------------------------------------
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake pkg-config \
        libboost-all-dev libssl-dev libdb++-dev \
        libminiupnpc-dev zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_UPNP=ON \
        -DBUILD_TESTS=OFF \
    && cmake --build build -j"$(nproc)" --target HoboNickelsd \
    && strip build/HoboNickelsd

# ---- Runtime stage -------------------------------------------------------
FROM ubuntu:24.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        libboost-system1.83.0 libboost-filesystem1.83.0 \
        libboost-program-options1.83.0 libboost-thread1.83.0 \
        libboost-chrono1.83.0t64 \
        libdb5.3++t64 libssl3t64 libminiupnpc17 zlib1g \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --create-home --home-dir /data --shell /usr/sbin/nologin hobo

COPY --from=build /src/build/HoboNickelsd /usr/local/bin/HoboNickelsd

USER hobo
ENV HOME=/data
WORKDIR /data
VOLUME ["/data"]

# P2P and RPC ports
EXPOSE 7372 7373

ENTRYPOINT ["HoboNickelsd"]
CMD ["-printtoconsole"]
