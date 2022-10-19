#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <liburing.h>

void *xmalloc(size_t size)
{
	void *ret = malloc(size);
	if (ret == NULL) {
		perror("malloc");
		exit(EXIT_FAILURE);
	}
	return ret;
}

/* Much safer than just calling atoi */
int get_port(const char *str)
{
	long ret;
	char *endptr;

	ret = strtol(str, &endptr, 10);

	if (ret == 0 && errno == EINVAL) {
		/* No conversion done */
		return -1;
	}

	if (errno == ERANGE) {
		if (ret == LONG_MIN) {
			/* Underflow */
			return -2;
		} else { // ret == LONG_MAX
			/* Overflow */
			return -3;
		}
	}

	if (ret < 0 || ret > 65535) {
		/* Value out of range */
		return -4;
	}

	if (*endptr != '\0') {
		/* Not necessarily an error, but something to report */
		fprintf(stderr, "Possible issue in port string to integer conversion (%s -> %ld)!\n", str, ret);
	}

	return (int)ret;
}


struct io_uring_sqe *io_uring_get_sqe_reliably(struct io_uring *_ring)
{
	struct io_uring_sqe *sqe;

	/* Get pointer to submission queue */
	sqe = io_uring_get_sqe(_ring);

	/* Submit and retry in case sqe is NULL */
	if (sqe == NULL) {
		/* Submit pending entries */
		io_uring_submit(_ring);

		/* Re-get pointer to submission queue */
		sqe = io_uring_get_sqe(_ring);

		/* If sqe is still NULL something must be wrong... */
		if (sqe == NULL) {
			fputs("Cannot get access to io_uring submission queue.", stderr);
			exit(EXIT_SUCCESS);
		}
	}

	return sqe;
}
