#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define MDNS_PROBE_MAX_ATTEMPTS 60
#define MDNS_PROBE_RETRY_DELAY_US 250000 /* 250ms */

static void
say(const char *msg)
{
	(void)write(STDOUT_FILENO, msg, strlen(msg));
}

int
main(void)
{
	struct sockaddr_in addr;
	int fd;
	int rc = -1;
	int attempt;

	say("PANTHERA_MDNS_DNSSD_PROBE:BEGIN\n");

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5354);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	for (attempt = 0; attempt < MDNS_PROBE_MAX_ATTEMPTS; attempt++) {
		errno = 0;
		fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) {
			say("PANTHERA_MDNS_DNSSD_PROBE:SOCKET_FAIL\n");
			return 1;
		}

		errno = 0;
		rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
		close(fd);

		if (rc == 0) {
			say("PANTHERA_MDNS_DNSSD_PROBE:PASS\n");
			say("PANTHERA_MDNS_DNSSD_PROBE\n");
			return 0;
		}

		if (attempt + 1 < MDNS_PROBE_MAX_ATTEMPTS) {
			struct timeval tv;
			tv.tv_sec = 0;
			tv.tv_usec = MDNS_PROBE_RETRY_DELAY_US;
			(void)select(0, NULL, NULL, NULL, &tv);
		}
	}

	say("PANTHERA_MDNS_DNSSD_PROBE:CONNECT_FAIL\n");
	return 2;
}
