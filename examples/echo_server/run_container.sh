#!/bin/sh

# this command
# - increases memlock ulimit to 64MiB
# - exposes port 50000 to the outer world
# - prints on stdout
docker run --ulimit memlock=67108864:67108864 \
           -p 0.0.0.0:50000:50000 \
	   -it \
	   io_uring_echo_server_plain_v4only:v1

