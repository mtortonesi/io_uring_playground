##############################################################################

# STAGE 1: build
FROM buildpack-deps:jammy AS builder
RUN apt-get update && apt-get install -y liburing2 liburing-dev && rm -rf /var/lib/apt/lists/*
LABEL builder=true

WORKDIR /build

COPY . .

RUN make


##############################################################################

# STAGE 2: deploy
FROM ubuntu:jammy
RUN apt-get update && apt-get install -y liburing2 && rm -rf /var/lib/apt/lists/*
LABEL builder=false

WORKDIR /app

COPY --from=builder /build/echo_server_plain_v4only .

EXPOSE 50000/tcp

ENTRYPOINT [ "/app/echo_server_plain_v4only", "50000" ]

