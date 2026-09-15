#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static void
write_line(int fd, const char *prefix, const char *value)
{
	size_t prefix_len = 0;
	size_t value_len = 0;

	while (prefix[prefix_len] != '\0')
		prefix_len++;
	while (value[value_len] != '\0')
		value_len++;

	write(fd, prefix, prefix_len);
	write(fd, value, value_len);
	write(fd, "\n", 1);
}

static int
parse_ipv4(const char *text, struct in_addr *addr)
{
	unsigned long part[4] = {0, 0, 0, 0};
	int i;

	for (i = 0; i < 4; i++) {
		int saw_digit = 0;
		while (*text >= '0' && *text <= '9') {
			part[i] = (part[i] * 10) + (unsigned long)(*text - '0');
			if (part[i] > 255)
				return 0;
			saw_digit = 1;
			text++;
		}
		if (!saw_digit)
			return 0;
		if (i < 3) {
			if (*text != '.')
				return 0;
			text++;
		} else if (*text != '\0') {
			return 0;
		}
	}

	addr->s_addr = htonl((part[0] << 24) | (part[1] << 16) | (part[2] << 8) | part[3]);
	return 1;
}

static void
usage(void)
{
	fprintf(stderr, "usage: ifconfig [iface [addr] [netmask mask] [up|down]]\n");
	exit(1);
}

static void
print_interface(const char *want)
{
	char buf[4096];
	struct ifconf ifc;
	char *ptr;
	char *end;
	int fd;
	char line[IFNAMSIZ + 2];

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		exit(1);
	}

	memset(&ifc, 0, sizeof(ifc));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	if (ioctl(fd, SIOCGIFCONF, &ifc) != 0) {
		perror("ioctl(SIOCGIFCONF)");
		close(fd);
		exit(1);
	}

	end = buf + ifc.ifc_len;
	for (ptr = buf; ptr + sizeof(struct ifreq) <= end; ) {
		struct ifreq *ifr = (struct ifreq *)ptr;
		size_t step = _SIZEOF_ADDR_IFREQ(*ifr);
		size_t len = 0;

		if (step < sizeof(struct ifreq))
			step = sizeof(struct ifreq);

		if (want != NULL && strcmp(ifr->ifr_name, want) != 0)
			goto next;

		while (len < IFNAMSIZ && ifr->ifr_name[len] != '\0') {
			line[len] = ifr->ifr_name[len];
			len++;
		}
		line[len++] = '\n';
		write(1, line, len);
next:
		ptr += step;
	}
	close(fd);
}

static void
set_addr(int fd, const char *ifname, const char *value, unsigned long request)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	if (!parse_ipv4(value, &sin->sin_addr)) {
		fprintf(stderr, "invalid IPv4 address: %s\n", value);
		exit(1);
	}
	if (ioctl(fd, request, &ifr) != 0) {
		perror("ioctl");
		exit(1);
	}
}

static void
set_flags(int fd, const char *ifname, int up)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
		perror("ioctl(SIOCGIFFLAGS)");
		exit(1);
	}
	if (up)
		ifr.ifr_flags |= IFF_UP;
	else
		ifr.ifr_flags &= ~IFF_UP;
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0) {
		perror("ioctl(SIOCSIFFLAGS)");
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	const char *ifname;
	const char *addr = NULL;
	const char *netmask = NULL;
	int set_up = -1;
	int fd;
	int i;

	if (argc == 1) {
		print_interface(NULL);
		return 0;
	}

	ifname = argv[1];
	if (strcmp(ifname, "-a") == 0) {
		if (argc != 2)
			usage();
		print_interface(NULL);
		return 0;
	}
	if (argc == 2) {
		print_interface(ifname);
		return 0;
	}

	for (i = 2; i < argc; i++) {
		if (strcmp(argv[i], "netmask") == 0) {
			if (++i >= argc)
				usage();
			netmask = argv[i];
		} else if (strcmp(argv[i], "up") == 0) {
			set_up = 1;
		} else if (strcmp(argv[i], "down") == 0) {
			set_up = 0;
		} else if (addr == NULL) {
			addr = argv[i];
		} else {
			usage();
		}
	}

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		perror("socket");
		return 1;
	}

	if (addr != NULL)
		set_addr(fd, ifname, addr, SIOCSIFADDR);
	if (netmask != NULL)
		set_addr(fd, ifname, netmask, SIOCSIFNETMASK);
	if (set_up != -1)
		set_flags(fd, ifname, set_up);

	close(fd);
	write_line(1, "configured ", ifname);
	return 0;
}
