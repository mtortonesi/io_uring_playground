CFLAGS  = -std=c17 -O2 -g3 -Wall -Werror -Wextra -pedantic -D_FORTIFY_SOURCE=2
LDFLAGS = -luring -fpie -Wl,-pie

.PHONY: clean

all: echo_server_plain_v4only

echo_server_plain_v4only: echo_server_plain_v4only.o lib/utils.o
	$(CC) -o $@ $^ $(LDFLAGS)

echo_server_plain_v4only.o: echo_server_plain_v4only.c lib/utils.h

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	@rm -f *.o

