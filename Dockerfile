FROM ubuntu:focal AS base

RUN apt-get update -y && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        libbsd0

FROM base AS build

RUN DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        make \
        g++-10 \
        libbsd-dev

ENV CC=gcc-10 CXX=g++-10

RUN mkdir -p /src/ls-l/
COPY *.c *.h Makefile /src/ls-l/
RUN make -j $(nproc) -C /src/ls-l

FROM base

COPY --from=build /src/ls-l/ls-l /usr/local/bin
ENTRYPOINT ["/usr/local/bin/ls-l"]
