#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <liburing.h>
#include "lib/utils.h"

#define QUEUE_DEPTH 32
#define READ_SZ     4096

/* TODO
 * - RFC 4038 server scheme
 * - do not malloc accept structures
 * - use posix_memalign instead of malloc for buffers?
 *
 * FIND OUT
 * - what is a proper queue depth
 */

/* Define event types */
enum {
	EVENT_TYPE_ACCEPT = 0,
	EVENT_TYPE_READ = 1,
	EVENT_TYPE_WRITE = 2
};

/* Define request structure */
struct request {
	int socket;
	int event_type;
	int iovec_count;
	struct iovec iov[];
};

/* Global variable to hold ring */
struct io_uring ring;

void sigint_handler(int signo)
{
	(void)signo;
	io_uring_queue_exit(&ring);
	exit(EXIT_SUCCESS);
}

void add_accept_request(int server_socket, struct sockaddr_in *client_addr, socklen_t *client_addr_len)
{
	struct io_uring_sqe *sqe;
	struct request *req;

	/* Get pointer to submission queue */
	sqe = io_uring_get_sqe_reliably(&ring);

	/* Prepare accept request */
	req = xmalloc(sizeof(*req));
	req->socket = server_socket;
	req->event_type = EVENT_TYPE_ACCEPT;
	io_uring_prep_accept(sqe, server_socket, (struct sockaddr *)client_addr, client_addr_len, 0);
	io_uring_sqe_set_data(sqe, req);
}

/* Linux kernel 5.5 has support for readv, but not for recv() or read() */
void add_read_request(int socket)
{
	struct io_uring_sqe *sqe;
	struct request *req;

	/* Get pointer to submission queue */
	sqe = io_uring_get_sqe_reliably(&ring);

	/* Prepare read request */
	req = xmalloc(sizeof(*req) + sizeof(struct iovec));
	req->iov[0].iov_base = calloc(1, READ_SZ);
	req->iov[0].iov_len = READ_SZ;
	req->iovec_count = 1;
	req->event_type = EVENT_TYPE_READ;
	req->socket = socket;
	io_uring_prep_readv(sqe, socket, req->iov, 1, 0);
	io_uring_sqe_set_data(sqe, req);
}

void add_write_request(struct request *req)
{
	struct io_uring_sqe *sqe;

	/* Get pointer to submission queue */
	sqe = io_uring_get_sqe_reliably(&ring);

#ifdef DEBUG
	write(2, "writing > ", 10);
	for (int i = 0; i < req->iovec_count; i++) {
		write(2, req->iov[i].iov_base, req->iov[i].iov_len);
	}
	write(2, "\n", 1);
#endif

	/* Prepare read request */
	req->event_type = EVENT_TYPE_WRITE;
	io_uring_prep_writev(sqe, req->socket, req->iov, req->iovec_count, 0);
	io_uring_sqe_set_data(sqe, req);
}

int main(int argc, char *argv[])
{
	int sd;
	struct sockaddr_in srv_addr;
	int on = 1;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len;
	int port;

	if (argc < 2) {
		fputs("Usage: liburing_server_echo port", stderr);
		exit(EXIT_FAILURE);
	}

	port = get_port(argv[1]);
	if (port < 0) {
		fputs("Invalid port <%s>: must be an integer between 0 and 65535.",
		      stderr);
		exit(EXIT_FAILURE);
	}

	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(int)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}

	memset((void *)&srv_addr, 0, sizeof(srv_addr));
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons((in_port_t)port);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	if (bind(sd, (const struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
		perror("bind");
		exit(EXIT_FAILURE);
	}

	if (listen(sd, SOMAXCONN) < 0) {
		perror("listen");
		exit(EXIT_FAILURE);
	}

	if (io_uring_queue_init(QUEUE_DEPTH, &ring, 0) != 0) {
		perror("io_uring_queue_init");
		exit(EXIT_FAILURE);
	}

	/* Add SIGINT handler for (relatively) graceful shutdown */
	signal(SIGINT, sigint_handler);

	/* Ignore SIGPIPE */
	signal(SIGPIPE, SIG_IGN);

	client_addr_len = sizeof(client_addr);
	add_accept_request(sd, &client_addr, &client_addr_len);

	while (1) {
		uint32_t head = 0;
		uint32_t count = 0;
		struct io_uring_cqe *cqe;

		/* Submit pending requests and wait for completions */
		io_uring_submit_and_wait(&ring, 1);

		/* Process cqes */
		io_uring_for_each_cqe(&ring, head, cqe) {
			struct request *req;

			/* Get request struct in cqe user data */
			req = io_uring_cqe_get_data(cqe);

			/* Check result */
			if (cqe->res < 0) {
				fprintf(stderr, "Async request failed: %s for event: %d\n",
					strerror(-cqe->res), req->event_type);
				exit(EXIT_FAILURE);
			}

			switch (req->event_type) {
			case EVENT_TYPE_ACCEPT:
				/* Re-add the socket for listening */
				add_accept_request(sd, &client_addr, &client_addr_len);

				/* Add the client socket for reading
				 * (note that the socket descriptor to use is
				 * the value returned by accept, i.e.,
				 * cqe->res; cqe->res->socket is the server
				 * socket instead) */
				add_read_request(cqe->res);

				/* TODO: allocation and deallocation of accept
				 * request is expensive. Fix this. */

				/* Free the request structure
				 * (which was allocated in add_accept_request) */
				free(req);

				break;

			case EVENT_TYPE_READ:
#ifdef DEBUG
				puts("loop loop read");
				printf("cqe->res: %d\n", cqe->res);
#endif

				/* Empty request means end of input */
				if (cqe->res == 0) {
					/* Close client socket */
					close(req->socket);

					/* Free the buffer */
					free(req->iov[0].iov_base);

					/* Free the request */
					free(req);
				} else {
#ifdef DEBUG
					write(2, "read > ", 7);
					for (int i = 0; i < req->iovec_count; i++) {
						write(2, req->iov[i].iov_base, req->iov[i].iov_len);
					}
					write(2, "\n", 1);
#endif

					/* When we are here, we need to handle
					 * the request. We do that by simply
					 * re-writing it as is back to the
					 * socket. */
					add_write_request(req);

					/* Re-add the client socket for reading */
					add_read_request(req->socket);
				}

				break;

			case EVENT_TYPE_WRITE:
#ifdef DEBUG
				puts("loop loop write");
				printf("cqe->res: %d\n", cqe->res);

				write(2, "wrote > ", 8);
				for (int i = 0; i < req->iovec_count; i++) {
					write(2, req->iov[i].iov_base, req->iov[i].iov_len);
				}
				write(2, "\n", 1);
#endif

				/* When we are here, the write request has
				 * finished and we have to clean up the used
				 * buffers and struct request metadata */

				/* Free all buffers (theoretically we have only one) */
				for (int i = 0; i < req->iovec_count; i++) {
					free(req->iov[i].iov_base);
				}

				/* Free the request (which was allocated in add_read_request) */
				free(req);

				break;
			}
			count++;
		}

		/* Notify the kernel we have processed count entries in the cqe */
		io_uring_cq_advance(&ring, count);
	}

	return 0;
}

