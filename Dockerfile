# Reproducible Pico firmware build — Ubuntu + gcc-arm-none-eabi + pinned pico-sdk.
# Build: docker compose build
# Run:   docker compose run --rm firmware   (or: make docker-build)

FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
  && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    gcc-arm-none-eabi \
    git \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    python3 \
  && rm -rf /var/lib/apt/lists/*

# SDK lives in the image so the host does not need ./pico-sdk. Override with build-arg for updates.
ARG PICO_SDK_REF=2.1.0
RUN git clone --depth 1 --branch "${PICO_SDK_REF}" https://github.com/raspberrypi/pico-sdk.git /opt/pico-sdk \
  && cd /opt/pico-sdk && git submodule update --init --depth 1

ENV PICO_SDK_PATH=/opt/pico-sdk

WORKDIR /project
# Build/run: docker compose run --rm firmware (compose runs /project/docker/build.sh from the bind mount).
