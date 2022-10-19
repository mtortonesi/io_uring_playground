#!/bin/sh

cp -r ../../lib .

#docker build --no-cache --progress=plain -t io_uring_echo_server_plain_v4only:v1 .
docker build -t io_uring_echo_server_plain_v4only:v1 .

rm -rf lib

