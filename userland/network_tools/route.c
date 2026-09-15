#include <arpa/inet.h>
#include <errno.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

typedef struct {
	struct rt_msghdr hdr;
	struct sockaddr_in dst;
	struct sockaddr_in gw;
	struct sockaddr_in mask;
} RouteMessage;

static void
usage(void)
{
	fprintf(stderr, "usage: route add default <gateway>\n");
	exit(1);
}

int
main(int argc, char **argv)
{
	RouteMessage msg;
	int fd;
	int seq = 1;

	if (argc != 4 || strcmp(argv[1], "add") != 0 || strcmp(argv[2], "default") != 0)
		usage();

	memset(&msg, 0, sizeof(msg));
	msg.hdr.rtm_msglen = sizeof(msg);
	msg.hdr.rtm_version = RTM_VERSION;
	msg.hdr.rtm_type = RTM_ADD;
	msg.hdr.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
	msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	msg.hdr.rtm_seq = seq++;

	msg.dst.sin_len = sizeof(msg.dst);
	msg.dst.sin_family = AF_INET;
	msg.dst.sin_addr.s_addr = htonl(INADDR_ANY);

	msg.mask.sin_len = sizeof(msg.mask);
	msg.mask.sin_family = AF_INET;
	msg.mask.sin_addr.s_addr = htonl(INADDR_ANY);

	msg.gw.sin_len = sizeof(msg.gw);
	msg.gw.sin_family = AF_INET;
	if (inet_pton(AF_INET, argv[3], &msg.gw.sin_addr) != 1) {
		fprintf(stderr, "invalid gateway: %s\n", argv[3]);
		return 1;
	}

	fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
	if (fd < 0) {
		perror("socket(PF_ROUTE)");
		return 1;
	}

	if (write(fd, &msg, sizeof(msg)) != (ssize_t)sizeof(msg)) {
		perror("write(route)");
		close(fd);
		return 1;
	}

	close(fd);
	printf("default route via %s added\n", argv[3]);
	return 0;
}
