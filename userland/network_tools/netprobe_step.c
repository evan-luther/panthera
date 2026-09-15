#include <arpa/inet.h>
#include <errno.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef SIOCGIFEFLAGS
#define SIOCGIFEFLAGS _IOWR('i', 142, struct ifreq)
#endif
#ifndef SIOCPROTOATTACH
#define SIOCPROTOATTACH _IOWR('i', 80, struct ifreq)
#endif

typedef struct {
	struct rt_msghdr hdr;
	struct sockaddr_in dst;
	struct sockaddr_in gw;
	struct sockaddr_in mask;
} RouteMessage;

typedef struct {
	struct rt_msghdr hdr;
	struct sockaddr_in dst;
	struct sockaddr_dl gw;
} LinkRouteMessage;

struct panthera_ifreq_eflags {
	char ifr_name[IFNAMSIZ];
	union {
		unsigned long long ifru_eflags;
		char ifru_pad[sizeof(struct sockaddr)];
	} ifr_ifru;
};

static void
mark_step(const char *step, const char *stage)
{
	char line[160];
	int n;

	n = snprintf(line, sizeof(line), "PANTHERA_NETPROBE_STAGE:%s:%s\n",
	    step, stage);
	if (n <= 0 || (size_t)n >= sizeof(line))
		return;
	(void)write(STDOUT_FILENO, line, (size_t)n);
}

static void
mark_errno(const char *step, const char *op)
{
	char line[192];
	int n;

	n = snprintf(line, sizeof(line), "PANTHERA_NETPROBE_ERROR:%s:%s:%d\n",
	    step, op, errno);
	if (n <= 0 || (size_t)n >= sizeof(line))
		return;
	(void)write(STDERR_FILENO, line, (size_t)n);
}

static int
parse_ipv4(const char *text, struct in_addr *addr)
{
	unsigned long part[4] = { 0, 0, 0, 0 };
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

	addr->s_addr = htonl((part[0] << 24) | (part[1] << 16) |
	    (part[2] << 8) | part[3]);
	return 1;
}

static pid_t
parse_pid(const char *text)
{
	pid_t pid = 0;

	while (*text >= '0' && *text <= '9') {
		pid = (pid * 10) + (pid_t)(*text - '0');
		text++;
	}
	if (*text != '\0')
		return -1;
	return pid;
}

static int
parse_mac(const char *text, unsigned char mac[6])
{
	int i;

	for (i = 0; i < 6; i++) {
		unsigned int value = 0;
		int digits = 0;

		while ((*text >= '0' && *text <= '9') ||
		    (*text >= 'a' && *text <= 'f') ||
		    (*text >= 'A' && *text <= 'F')) {
			value <<= 4;
			if (*text >= '0' && *text <= '9')
				value += (unsigned int)(*text - '0');
			else if (*text >= 'a' && *text <= 'f')
				value += 10U + (unsigned int)(*text - 'a');
			else
				value += 10U + (unsigned int)(*text - 'A');
			if (value > 255 || ++digits > 2)
				return 0;
			text++;
		}
		if (digits == 0)
			return 0;
		mac[i] = (unsigned char)value;
		if (i < 5) {
			if (*text != ':')
				return 0;
			text++;
		} else if (*text != '\0') {
			return 0;
		}
	}
	return 1;
}

static int
set_flags_noarp_up(int fd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	mark_step("flags", "before_get");
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
		return 1;
	mark_step("flags", "after_get");

	ifr.ifr_flags |= (IFF_UP | IFF_NOARP);
	ifr.ifr_flags &= ~IFF_MULTICAST;
	mark_step("flags", "before_set");
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		return 1;
	mark_step("flags", "after_set");
	return 0;
}

static int
set_addr(int fd, const char *ifname, const char *value, unsigned long request,
    int quiet_after_ioctl)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	if (!parse_ipv4(value, &sin->sin_addr))
		return 2;

	mark_step(request == SIOCSIFNETMASK ? "netmask" : "addr", "before_ioctl");
	if (ioctl(fd, request, &ifr) != 0)
		return 1;
	if (!quiet_after_ioctl)
		mark_step(request == SIOCSIFNETMASK ? "netmask" : "addr", "after_ioctl");
	return 0;
}

static int
get_flags(int fd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	mark_step("getflags", "before_ioctl");
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
		return 1;
	mark_step("getflags", "after_ioctl");
	printf("PANTHERA_NETPROBE_FLAGS:0x%x\n", (unsigned int)ifr.ifr_flags);
	return 0;
}

static int
set_arp_enabled(int fd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	mark_step("arp_on", "before_get");
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
		return 1;
	mark_step("arp_on", "after_get");

	ifr.ifr_flags |= IFF_UP;
	ifr.ifr_flags &= ~IFF_NOARP;
	mark_step("arp_on", "before_set");
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0)
		return 1;
	mark_step("arp_on", "after_set");
	return 0;
}

static int
attach_inet_proto(int fd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	mark_step("proto_attach", "before_ioctl");
	if (ioctl(fd, SIOCPROTOATTACH, &ifr) != 0)
		return 1;
	mark_step("proto_attach", "after_ioctl");
	return 0;
}

static int
get_eflags(int fd, const char *ifname)
{
	struct panthera_ifreq_eflags ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	mark_step("eflags", "before_ioctl");
	if (ioctl(fd, SIOCGIFEFLAGS, &ifr) != 0)
		return 1;
	mark_step("eflags", "after_ioctl");
	printf("PANTHERA_NETPROBE_EFLAGS:0x%llx\n",
	    ifr.ifr_ifru.ifru_eflags);
	return 0;
}

static int
verify_static_ipv4(int fd, const char *ifname, const char *addr,
    const char *netmask, int require_netmask, int require_noarp)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;
	struct in_addr want_addr;
	struct in_addr want_mask;

	if (!parse_ipv4(addr, &want_addr))
		return 2;
	if (require_netmask && !parse_ipv4(netmask, &want_mask))
		return 2;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0)
		return 1;
	if ((ifr.ifr_flags & IFF_UP) != IFF_UP)
		return 3;
	if (require_noarp && (ifr.ifr_flags & IFF_NOARP) != IFF_NOARP)
		return 3;
	printf("PANTHERA_NETPROBE_VERIFY_FLAGS:0x%x\n",
	    (unsigned int)ifr.ifr_flags);

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFADDR, &ifr) != 0)
		return 1;
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	if (sin->sin_addr.s_addr != want_addr.s_addr)
		return 4;
	printf("PANTHERA_NETPROBE_VERIFY_ADDR:%s\n", addr);

	if (!require_netmask)
		return 0;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFNETMASK, &ifr) != 0)
		return 1;
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	if (sin->sin_addr.s_addr != want_mask.s_addr)
		return 5;

	return 0;
}

static int
set_static_ipv4(int fd, const char *ifname, const char *addr,
    const char *netmask)
{
	int rc;

	mark_step("static", "flags_begin");
	rc = set_flags_noarp_up(fd, ifname);
	if (rc != 0)
		return rc;
	mark_step("static", "quiet_config_begin");
	rc = set_addr(fd, ifname, addr, SIOCSIFADDR, 1);
	if (rc != 0)
		return rc;
	(void)netmask;
	return 0;
}

static int
add_default_route(const char *gateway)
{
	RouteMessage msg;
	int route_fd;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.rtm_msglen = sizeof(msg);
	msg.hdr.rtm_version = RTM_VERSION;
	msg.hdr.rtm_type = RTM_ADD;
	msg.hdr.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
	msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	msg.hdr.rtm_seq = 1;
	msg.hdr.rtm_pid = getpid();

	msg.dst.sin_len = sizeof(msg.dst);
	msg.dst.sin_family = AF_INET;
	msg.dst.sin_addr.s_addr = htonl(INADDR_ANY);

	msg.mask.sin_len = sizeof(msg.mask);
	msg.mask.sin_family = AF_INET;
	msg.mask.sin_addr.s_addr = htonl(INADDR_ANY);

	msg.gw.sin_len = sizeof(msg.gw);
	msg.gw.sin_family = AF_INET;
	if (!parse_ipv4(gateway, &msg.gw.sin_addr))
		return 2;

	mark_step("route", "before_socket");
	route_fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
	if (route_fd < 0)
		return 1;
	mark_step("route", "after_socket");

	mark_step("route", "before_write");
	if (write(route_fd, &msg, sizeof(msg)) != (ssize_t)sizeof(msg))
		return 1;
	return 0;
}

static int
add_static_arp(const char *ifname, const char *ipv4, const char *mac_text)
{
	LinkRouteMessage msg;
	unsigned char mac[6];
	unsigned int ifindex;
	int route_fd;

	if (!parse_mac(mac_text, mac))
		return 2;
	ifindex = if_nametoindex(ifname);
	if (ifindex == 0 && strcmp(ifname, "en0") == 0)
		ifindex = 2;
	if (ifindex == 0)
		return 1;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.rtm_msglen = sizeof(msg);
	msg.hdr.rtm_version = RTM_VERSION;
	msg.hdr.rtm_type = RTM_ADD;
	msg.hdr.rtm_index = (unsigned short)ifindex;
	msg.hdr.rtm_flags = RTF_UP | RTF_HOST | RTF_STATIC | RTF_LLINFO |
	    RTF_IFSCOPE;
	msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY;
	msg.hdr.rtm_seq = 2;
	msg.hdr.rtm_pid = getpid();

	msg.dst.sin_len = sizeof(msg.dst);
	msg.dst.sin_family = AF_INET;
	if (!parse_ipv4(ipv4, &msg.dst.sin_addr))
		return 2;

	msg.gw.sdl_len = sizeof(msg.gw);
	msg.gw.sdl_family = AF_LINK;
	msg.gw.sdl_index = (unsigned short)ifindex;
	msg.gw.sdl_type = IFT_ETHER;
	msg.gw.sdl_alen = 6;
	memcpy(LLADDR(&msg.gw), mac, sizeof(mac));

	mark_step("arp", "before_socket");
	route_fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
	if (route_fd < 0) {
		mark_errno("arp", "socket");
		return 1;
	}
	mark_step("arp", "after_socket");

	mark_step("arp", "before_write");
	if (write(route_fd, &msg, sizeof(msg)) != (ssize_t)sizeof(msg)) {
		mark_errno("arp", "write");
		return 1;
	}
	return 0;
}

static void
usage(void)
{
	fprintf(stderr, "usage: netprobe_step <ifname> <flags|addr|netmask|route|arp|arp_on|proto_attach|getflags|eflags|static|verify|verify_addr|verify_addr_up> [ipv4] [netmask|mac]\n");
}

int
main(int argc, char **argv)
{
	const char *ifname;
	const char *step;
	int fd;
	int rc;

	if (argc < 3) {
		usage();
		return 2;
	}

	ifname = argv[1];
	step = argv[2];
	mark_step(step, "main");
	mark_step(step, "before_socket");
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		mark_step(step, "socket_failed");
		return 1;
	}
	mark_step(step, "after_socket");

	if (strcmp(step, "flags") == 0) {
		mark_step("flags", "start");
		rc = set_flags_noarp_up(fd, ifname);
	} else if (strcmp(step, "addr") == 0 && argc == 4) {
		mark_step("addr", "start");
		rc = set_addr(fd, ifname, argv[3], SIOCSIFADDR, 1);
	} else if (strcmp(step, "netmask") == 0 && argc == 4) {
		mark_step("netmask", "start");
		rc = set_addr(fd, ifname, argv[3], SIOCSIFNETMASK, 0);
	} else if (strcmp(step, "route") == 0 && argc == 4) {
		mark_step("route", "start");
		rc = add_default_route(argv[3]);
	} else if (strcmp(step, "arp") == 0 && argc == 5) {
		mark_step("arp", "start");
		rc = add_static_arp(ifname, argv[3], argv[4]);
	} else if (strcmp(step, "arp_on") == 0) {
		mark_step("arp_on", "start");
		rc = set_arp_enabled(fd, ifname);
	} else if (strcmp(step, "proto_attach") == 0) {
		mark_step("proto_attach", "start");
		rc = attach_inet_proto(fd, ifname);
	} else if (strcmp(step, "getflags") == 0) {
		mark_step("getflags", "start");
		rc = get_flags(fd, ifname);
	} else if (strcmp(step, "eflags") == 0) {
		mark_step("eflags", "start");
		rc = get_eflags(fd, ifname);
	} else if (strcmp(step, "static") == 0 && (argc == 5 || argc == 6)) {
		pid_t notify_pid = argc == 6 ? parse_pid(argv[5]) : -1;

		mark_step("static", "start");
		rc = set_static_ipv4(fd, ifname, argv[3], argv[4]);
		if (rc == 0 && notify_pid > 0)
			(void)kill(notify_pid, SIGUSR1);
		for (;;)
			pause();
	} else if (strcmp(step, "verify") == 0 && argc == 5) {
		rc = verify_static_ipv4(fd, ifname, argv[3], argv[4], 1, 1);
	} else if (strcmp(step, "verify_addr") == 0 && argc == 4) {
		rc = verify_static_ipv4(fd, ifname, argv[3], NULL, 0, 1);
	} else if (strcmp(step, "verify_addr_up") == 0 && argc == 4) {
		rc = verify_static_ipv4(fd, ifname, argv[3], NULL, 0, 0);
	} else {
		usage();
		rc = 2;
	}

	if (strcmp(step, "addr") == 0 || strcmp(step, "netmask") == 0 ||
	    strcmp(step, "route") == 0 || strcmp(step, "arp") == 0)
		_exit(rc);

	if (rc == 0)
		mark_step(step, "done");
	else
		mark_step(step, "failed");
	mark_step(step, "before_close");
	close(fd);
	mark_step(step, "after_close");
	return rc;
}
