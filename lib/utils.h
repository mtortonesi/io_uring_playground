#ifndef MY_UTILS_H
#define MY_UTILS_H

#include <stdint.h>
#include <liburing.h>

struct io_uring_sqe *io_uring_get_sqe_reliably(struct io_uring *_ring);
void *xmalloc(size_t size);
int get_port(const char *str);

#endif /* MY_UTILS_H */
