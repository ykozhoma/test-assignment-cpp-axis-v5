ARG ARCH=armv7hf
ARG REPO=axisecp
ARG SDK_VERSION=1.11
ARG UBUNTU_VERSION=22.04

FROM arm32v7/ubuntu:${UBUNTU_VERSION} as runtime-image-armv7hf
FROM arm64v8/ubuntu:${UBUNTU_VERSION} as runtime-image-aarch64

FROM ${REPO}/acap-computer-vision-sdk:${SDK_VERSION}-${ARCH}-runtime AS cv-sdk-runtime
FROM ${REPO}/acap-computer-vision-sdk:${SDK_VERSION}-${ARCH}-devel AS cv-sdk-devel

ENV DEBIAN_FRONTEND=noninteractive

## Install dependencies
ARG ARCH

RUN <<EOF
apt-get update
apt-get install -y -f make pkg-config libglib2.0-dev libsystemd0
EOF

RUN <<EOF
if [ ${ARCH} = armv7hf ]; then
    apt-get install -y -f g++-arm-linux-gnueabihf
    dpkg --add-architecture armhf
    apt-get update
    apt-get install -y libglib2.0-dev:armhf libsystemd0:armhf libcurl4-gnutls-dev:armhf libxml2-dev:armhf
elif [ ${ARCH} = aarch64 ]; then
    apt-get install -y -f g++-aarch64-linux-gnu
    dpkg --add-architecture arm64
    apt-get update
    apt-get install -y libglib2.0-dev:arm64 libsystemd0:arm64 libcurl4-gnutls-dev:arm64 libxml2-dev:arm64
else
    printf "Error: '%s' is not a valid value for the ARCH variable\n", ${ARCH}
    exit 1
fi
EOF

COPY app/Makefile /build/
COPY app/src/ /build/src/
WORKDIR /build

RUN <<EOF
if [ ${ARCH} = armv7hf ]; then
    make CXX=arm-linux-gnueabihf-g++ CC=arm-linux-gnueabihf-gcc STRIP=arm-linux-gnueabihf-strip
elif [ ${ARCH} = aarch64 ]; then
    make  CXX=/usr/bin/aarch64-linux-gnu-g++ CC=/usr/bin/aarch64-linux-gnu-gcc STRIP=aarch64-linux-gnu-strip
else
    printf "Error: '%s' is not a valid value for the ARCH variable\n", ${ARCH}
    exit 1
fi
EOF

FROM runtime-image-${ARCH}

ARG ARCH
ARG HTTP_SERVER
ENV TARGET_HTTP_SERVER ${HTTP_SERVER}

COPY --from=cv-sdk-devel /build/ta_axisv5_app /usr/bin/
COPY --from=cv-sdk-devel /usr/lib/${ARCH}-linux-gnu/* /usr/lib/
COPY --from=cv-sdk-runtime /axis/opencv /
COPY --from=cv-sdk-runtime /axis/openblas /

CMD ["sh", "-c", "/usr/bin/ta_axisv5_app ${TARGET_HTTP_SERVER}"]
