#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/route.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct {
	struct rt_msghdr hdr;
	struct sockaddr_in dst;
	struct sockaddr_in gw;
	struct sockaddr_in mask;
} RouteMessage;

static int quiet_after_flags;

static void
panthera_sleep_seconds(unsigned int seconds)
{
	struct timeval timeout;

	timeout.tv_sec = (time_t)seconds;
	timeout.tv_usec = 0;
	(void)select(0, NULL, NULL, NULL, &timeout);
}

static void
write_full(int fd, const char *buf, size_t len)
{
	if (quiet_after_flags)
		return;

	while (len > 0) {
		ssize_t written = write(fd, buf, len);
		if (written <= 0)
			return;
		buf += (size_t)written;
		len -= (size_t)written;
	}
}

static void
write_cstr(int fd, const char *text)
{
	size_t len = 0;

	while (text[len] != '\0')
		len++;
	write_full(fd, text, len);
}

static void
write_line2(int fd, const char *a, const char *b)
{
	write_cstr(fd, a);
	write_cstr(fd, b);
	write_cstr(fd, "\n");
}

static void
trace_call(const char *call, const char *ifname)
{
	char line[160];
	size_t len = 0;
	size_t i;

	if (quiet_after_flags)
		return;

#define TRACE_APPEND_CSTR(s) do { \
		const char *_s = (s); \
		for (i = 0; _s[i] != '\0' && len + 1 < sizeof(line); i++) \
			line[len++] = _s[i]; \
	} while (0)

	TRACE_APPEND_CSTR("netbringup: trace ");
	TRACE_APPEND_CSTR(call);
	TRACE_APPEND_CSTR(" ");
	TRACE_APPEND_CSTR(ifname);
	if (len + 1 < sizeof(line))
		line[len++] = '\n';
	write_full(1, line, len);

#undef TRACE_APPEND_CSTR
}

static void
write_uint_line3(int fd, const char *a, unsigned int value, const char *b)
{
	char number[16];
	size_t len = 0;

	if (value == 0) {
		number[len++] = '0';
	} else {
		unsigned int tmp = value;
		char rev[16];
		size_t rev_len = 0;

		while (tmp != 0 && rev_len < sizeof(rev)) {
			rev[rev_len++] = (char)('0' + (tmp % 10));
			tmp /= 10;
		}
		while (rev_len > 0)
			number[len++] = rev[--rev_len];
	}
	number[len] = '\0';

	write_cstr(fd, a);
	write_cstr(fd, number);
	write_cstr(fd, b);
	write_cstr(fd, "\n");
}

static int
get_flags(int fd, const char *ifname, short *flags_out)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	trace_call("enter SIOCGIFFLAGS", ifname);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
		trace_call("fail SIOCGIFFLAGS", ifname);
		return 1;
	}
	trace_call("ok SIOCGIFFLAGS", ifname);
	*flags_out = ifr.ifr_flags;
	return 0;
}

static int
interface_exists(int fd, const char *ifname)
{
	char buf[4096];
	struct ifconf ifc;
	char *ptr;
	char *end;
	short flags;

	if (get_flags(fd, ifname, &flags) == 0)
		return 1;

	memset(&ifc, 0, sizeof(ifc));
	ifc.ifc_len = sizeof(buf);
	ifc.ifc_buf = buf;
	trace_call("enter SIOCGIFCONF", ifname);
	if (ioctl(fd, SIOCGIFCONF, &ifc) != 0) {
		trace_call("fail SIOCGIFCONF", ifname);
		return 0;
	}
	trace_call("ok SIOCGIFCONF", ifname);

	end = buf + ifc.ifc_len;
	for (ptr = buf; ptr + sizeof(struct ifreq) <= end; ) {
		struct ifreq *ifr = (struct ifreq *)ptr;
		size_t step = _SIZEOF_ADDR_IFREQ(*ifr);

		if (step < sizeof(struct ifreq))
			step = sizeof(struct ifreq);
		if (strncmp(ifr->ifr_name, ifname, IFNAMSIZ) == 0)
			return 1;
		ptr += step;
	}
	return 0;
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

static int
wait_for_interface(int fd, const char *ifname, unsigned int retries)
{
	unsigned int i;

	for (i = 0; i < retries; i++) {
		if (interface_exists(fd, ifname))
			return 0;
		if ((i % 5U) == 0U) {
			write_uint_line3(1, "netbringup: waiting for interface attempt ", i + 1U, "");
		}
		panthera_sleep_seconds(1);
	}

	if (interface_exists(fd, ifname))
		return 0;

	write_line2(2, "netbringup: interface not ready ", ifname);
	return 1;
}

static void
wait_for_link_initialization(const char *ifname)
{
	write_line2(1, "netbringup: settling interface startup ", ifname);
	panthera_sleep_seconds(2);
	write_line2(1, "netbringup: settled interface startup ", ifname);
}

static int
wait_for_interface_running(int fd, const char *ifname, unsigned int retries)
{
	short flags;
	unsigned int i;

	for (i = 0; i < retries; i++) {
		if (get_flags(fd, ifname, &flags) == 0 && (flags & IFF_RUNNING) != 0)
			return 0;
		if ((i % 4U) == 0U)
			write_uint_line3(1, "netbringup: waiting for RUNNING attempt ", i + 1U, "");
		panthera_sleep_seconds(1);
	}

	write_line2(2, "netbringup: interface never reached RUNNING ", ifname);
	return 1;
}

static int
set_addr(int fd, const char *ifname, const char *value, unsigned long request)
{
	struct ifreq ifr;
	struct sockaddr_in *sin;
	const char *label;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	sin = (struct sockaddr_in *)&ifr.ifr_addr;
	sin->sin_len = sizeof(*sin);
	sin->sin_family = AF_INET;
	if (!parse_ipv4(value, &sin->sin_addr)) {
		write_line2(2, "netbringup: invalid IPv4 address ", value);
		return 1;
	}
	label = (request == SIOCSIFNETMASK) ? "set-netmask-ioctl" :
	    "set-address-ioctl";
	if (request == SIOCSIFNETMASK)
		trace_call("enter set-netmask-ioctl", ifname);
	else
		trace_call("enter set-address-ioctl", ifname);
	if (ioctl(fd, request, &ifr) != 0) {
		if (request == SIOCSIFNETMASK)
			trace_call("fail set-netmask-ioctl", ifname);
		else
			trace_call("fail set-address-ioctl", ifname);
		write_line2(2, "netbringup: ioctl failed for ", value);
		return 1;
	}
	(void)label;
	return 0;
}

static int
set_up(int fd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	trace_call("enter set-up SIOCGIFFLAGS", ifname);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
		trace_call("fail set-up SIOCGIFFLAGS", ifname);
		write_cstr(2, "netbringup: SIOCGIFFLAGS failed\n");
		return 1;
	}
	trace_call("ok set-up SIOCGIFFLAGS", ifname);
	ifr.ifr_flags |= IFF_UP;
	trace_call("enter SIOCSIFFLAGS", ifname);
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0) {
		trace_call("fail SIOCSIFFLAGS", ifname);
		write_cstr(2, "netbringup: SIOCSIFFLAGS failed\n");
		return 1;
	}
	trace_call("ok SIOCSIFFLAGS", ifname);
	return 0;
}

static int
set_probe_noarp_up(int fd, const char *ifname)
{
	struct ifreq ifr;

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	trace_call("enter noarp-up SIOCGIFFLAGS", ifname);
	if (ioctl(fd, SIOCGIFFLAGS, &ifr) != 0) {
		trace_call("fail noarp-up SIOCGIFFLAGS", ifname);
		write_cstr(2, "netbringup: SIOCGIFFLAGS failed\n");
		return 1;
	}
	trace_call("ok noarp-up SIOCGIFFLAGS", ifname);
	/*
	 * Keep the Panthera probe away from ARP transmit and the automatic
	 * all-hosts multicast join until those driver paths are proven.
	 */
	ifr.ifr_flags |= (IFF_UP | IFF_NOARP);
	ifr.ifr_flags &= ~IFF_MULTICAST;
	trace_call("enter noarp-up SIOCSIFFLAGS", ifname);
	if (ioctl(fd, SIOCSIFFLAGS, &ifr) != 0) {
		trace_call("fail noarp-up SIOCSIFFLAGS", ifname);
		write_cstr(2, "netbringup: SIOCSIFFLAGS failed\n");
		return 1;
	}
	trace_call("ok noarp-up SIOCSIFFLAGS", ifname);
	return 0;
}

static int
add_default_route(const char *gateway)
{
	RouteMessage msg;
	int fd;

	memset(&msg, 0, sizeof(msg));
	msg.hdr.rtm_msglen = sizeof(msg);
	msg.hdr.rtm_version = RTM_VERSION;
	msg.hdr.rtm_type = RTM_ADD;
	msg.hdr.rtm_flags = RTF_UP | RTF_GATEWAY | RTF_STATIC;
	msg.hdr.rtm_addrs = RTA_DST | RTA_GATEWAY | RTA_NETMASK;
	msg.hdr.rtm_seq = 1;

	msg.dst.sin_len = sizeof(msg.dst);
	msg.dst.sin_family = AF_INET;
	msg.dst.sin_addr.s_addr = htonl(INADDR_ANY);

	msg.mask.sin_len = sizeof(msg.mask);
	msg.mask.sin_family = AF_INET;
	msg.mask.sin_addr.s_addr = htonl(INADDR_ANY);

	msg.gw.sin_len = sizeof(msg.gw);
	msg.gw.sin_family = AF_INET;
	if (!parse_ipv4(gateway, &msg.gw.sin_addr)) {
		write_line2(2, "netbringup: invalid gateway ", gateway);
		return 1;
	}

	fd = socket(PF_ROUTE, SOCK_RAW, AF_INET);
	if (fd < 0) {
		write_cstr(2, "netbringup: route socket failed\n");
		return 1;
	}

	if (write(fd, &msg, sizeof(msg)) != (ssize_t)sizeof(msg)) {
		close(fd);
		write_cstr(2, "netbringup: default route add failed\n");
		return 1;
	}

	close(fd);
	write_line2(1, "netbringup: default route via ", gateway);
	return 0;
}

static int
encode_dns_name(const char *name, unsigned char *buf, size_t buf_size)
{
	const char *label = name;
	size_t offset = 0;

	while (*label != '\0') {
		const char *dot = strchr(label, '.');
		size_t label_len = dot ? (size_t)(dot - label) : strlen(label);

		if (label_len == 0 || label_len > 63 || offset + label_len + 2 > buf_size)
			return -1;
		buf[offset++] = (unsigned char)label_len;
		memcpy(&buf[offset], label, label_len);
		offset += label_len;
		if (!dot)
			break;
		label = dot + 1;
	}
	if (offset + 1 > buf_size)
		return -1;
	buf[offset++] = 0;
	return (int)offset;
}

static void
write_be16(unsigned char *dst, unsigned short value)
{
	dst[0] = (unsigned char)((value >> 8) & 0xff);
	dst[1] = (unsigned char)(value & 0xff);
}

static unsigned short
read_be16(const unsigned char *src)
{
	return (unsigned short)((src[0] << 8) | src[1]);
}

static int
udp_dns_probe(const char *target)
{
	struct sockaddr_in dst;
	struct timeval timeout;
	fd_set readset;
	unsigned char packet[256];
	unsigned char response[512];
	int name_len;
	int packet_len;
	int fd;
	int nready;
	ssize_t nread;

	memset(&dst, 0, sizeof(dst));
	dst.sin_len = sizeof(dst);
	dst.sin_family = AF_INET;
	dst.sin_port = htons(53);
	if (!parse_ipv4(target, &dst.sin_addr)) {
		write_line2(2, "netbringup: invalid dns probe target ", target);
		return 1;
	}

	memset(packet, 0, sizeof(packet));
	packet[0] = 0x12;
	packet[1] = 0x34;
	packet[2] = 0x01; /* recursion desired */
	packet[5] = 0x01; /* one question */
	name_len = encode_dns_name("example.com", &packet[12], sizeof(packet) - 16);
	if (name_len < 0) {
		write_cstr(2, "netbringup: dns query encode failed\n");
		return 1;
	}
	packet_len = 12 + name_len;
	write_be16(&packet[packet_len], 1); /* A */
	packet_len += 2;
	write_be16(&packet[packet_len], 1); /* IN */
	packet_len += 2;

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		write_cstr(2, "netbringup: socket(UDP) failed\n");
		return 1;
	}

	if (sendto(fd, packet, (size_t)packet_len, 0,
	    (struct sockaddr *)&dst, sizeof(dst)) != packet_len) {
		close(fd);
		write_cstr(2, "netbringup: dns probe send failed\n");
		return 1;
	}

	FD_ZERO(&readset);
	FD_SET(fd, &readset);
	timeout.tv_sec = 5;
	timeout.tv_usec = 0;
	nready = select(fd + 1, &readset, NULL, NULL, &timeout);
	if (nready <= 0) {
		close(fd);
		write_line2(2, "netbringup: dns probe timeout ", target);
		return 1;
	}

	nread = recvfrom(fd, response, sizeof(response), 0, NULL, NULL);
	if (nread < 12) {
		close(fd);
		write_cstr(2, "netbringup: dns probe short response\n");
		return 1;
	}
	if (read_be16(&response[0]) != 0x1234 ||
	    (response[2] & 0x80) == 0 ||
	    (read_be16(&response[6]) == 0 && read_be16(&response[8]) == 0)) {
		close(fd);
		write_cstr(2, "netbringup: dns probe invalid response\n");
		return 1;
	}

	close(fd);
	write_line2(1, "netbringup: dns probe ok ", target);
	return 0;
}

static void
usage(void)
{
	write_cstr(2, "usage: netbringup <iface> <local-ip> <netmask> <gateway> <dns-probe-target>\n");
	_exit(1);
}

int
main(int argc, char **argv)
{
	const char *ifname;
	const char *local_ip;
	const char *netmask;
	const char *gateway;
	const char *target;
	int fd;

	if (argc != 6)
		usage();

	ifname = argv[1];
	local_ip = argv[2];
	netmask = argv[3];
	gateway = argv[4];
	target = argv[5];

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		write_cstr(2, "netbringup: datagram socket failed\n");
		return 1;
	}

	wait_for_link_initialization(ifname);

	if (wait_for_interface(fd, ifname, 10) != 0) {
		close(fd);
		return 1;
	}

	if (set_probe_noarp_up(fd, ifname) != 0) {
		close(fd);
		return 1;
	}
	quiet_after_flags = 1;

	if (set_addr(fd, ifname, local_ip, SIOCSIFADDR) != 0) {
		close(fd);
		return 1;
	}

	if (set_addr(fd, ifname, netmask, SIOCSIFNETMASK) != 0) {
		close(fd);
		return 1;
	}
	if (!quiet_after_flags)
		write_line2(1, "netbringup: netmask set ", netmask);

	if (set_up(fd, ifname) != 0) {
		close(fd);
		return 1;
	}
	if (!quiet_after_flags)
		write_line2(1, "netbringup: interface up ", ifname);

	if (wait_for_interface_running(fd, ifname, 10) != 0) {
		close(fd);
		return 1;
	}
	if (!quiet_after_flags)
		write_line2(1, "netbringup: interface running ", ifname);
	close(fd);

	if (add_default_route(gateway) != 0)
		return 1;

	if (udp_dns_probe(target) != 0) {
		if (!quiet_after_flags)
			write_cstr(1, "netbringup: configured; dns probe failed\n");
		return 0;
	}

	if (!quiet_after_flags)
		write_cstr(1, "netbringup: success\n");
	return 0;
}
