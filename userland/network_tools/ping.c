#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DNS_PORT 53
#define DNS_FALLBACK_SERVER "10.0.2.3"

static uint16_t
checksum16(const void *data, size_t len)
{
	const uint16_t *words = data;
	uint32_t sum = 0;

	while (len > 1) {
		sum += *words++;
		len -= 2;
	}
	if (len == 1)
		sum += *(const unsigned char *)words;
	while (sum >> 16)
		sum = (sum & 0xffffU) + (sum >> 16);
	return (uint16_t)~sum;
}

static void
usage(void)
{
	fprintf(stderr, "usage: ping [-c count] <host>\n");
	exit(1);
}

static int
format_ipv4_addr(const unsigned char *addr, char *buf, size_t buf_size)
{
	int written = snprintf(buf, buf_size, "%u.%u.%u.%u",
	    addr[0], addr[1], addr[2], addr[3]);

	return (written > 0 && (size_t)written < buf_size) ? 0 : -1;
}

static void
dns_write_u16(unsigned char *dst, unsigned short value)
{
	dst[0] = (unsigned char)((value >> 8) & 0xff);
	dst[1] = (unsigned char)(value & 0xff);
}

static unsigned short
dns_read_u16(const unsigned char *src)
{
	return (unsigned short)((src[0] << 8) | src[1]);
}

static int
encode_dns_name(const char *name, unsigned char *buf, size_t buf_size)
{
	const char *label = name;
	size_t offset = 0;

	while (*label != '\0') {
		const char *dot = strchr(label, '.');
		size_t label_len = (dot == NULL) ? strlen(label) : (size_t)(dot - label);

		if (label_len == 0 || label_len > 63 || offset + label_len + 2 > buf_size) {
			return -1;
		}
		buf[offset++] = (unsigned char)label_len;
		memcpy(&buf[offset], label, label_len);
		offset += label_len;

		if (dot == NULL) {
			break;
		}
		label = dot + 1;
	}

	if (offset + 1 > buf_size) {
		return -1;
	}
	buf[offset++] = 0;
	return (int)offset;
}

static int
skip_dns_name(const unsigned char *msg, size_t msg_len, size_t offset)
{
	while (offset < msg_len) {
		unsigned char len = msg[offset++];

		if (len == 0) {
			return (int)offset;
		}
		if ((len & 0xc0U) == 0xc0U) {
			if (offset >= msg_len) {
				return -1;
			}
			return (int)(offset + 1);
		}
		if (offset + len > msg_len) {
			return -1;
		}
		offset += len;
	}
	return -1;
}

static int
write_full(int fd, const unsigned char *buf, size_t len)
{
	while (len > 0) {
		ssize_t written = write(fd, buf, len);
		if (written <= 0) {
			return -1;
		}
		buf += (size_t)written;
		len -= (size_t)written;
	}
	return 0;
}

static int
read_full(int fd, unsigned char *buf, size_t len)
{
	while (len > 0) {
		ssize_t nread = read(fd, buf, len);
		if (nread <= 0) {
			return -1;
		}
		buf += (size_t)nread;
		len -= (size_t)nread;
	}
	return 0;
}

static void
load_dns_server(char *server, size_t server_len)
{
	FILE *fp;
	char line[256];

	if (server_len == 0) {
		return;
	}

	strncpy(server, DNS_FALLBACK_SERVER, server_len);
	server[server_len - 1] = '\0';

	fp = fopen("/etc/resolv.conf", "r");
	if (fp == NULL) {
		return;
	}

	while (fgets(line, sizeof(line), fp) != NULL) {
		char *p = line;
		char *end;
		struct in_addr tmp;

		while (*p == ' ' || *p == '\t') {
			p++;
		}
		if (strncmp(p, "nameserver", 10) != 0 ||
		    (p[10] != ' ' && p[10] != '\t')) {
			continue;
		}

		p += 10;
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		end = p;
		while (*end != '\0' && *end != ' ' && *end != '\t' &&
		    *end != '\r' && *end != '\n') {
			end++;
		}
		*end = '\0';
		if (inet_pton(AF_INET, p, &tmp) == 1) {
			strncpy(server, p, server_len);
			server[server_len - 1] = '\0';
			break;
		}
	}

	fclose(fp);
}

static int
connect_tcp_dns_server(const char *server)
{
	struct sockaddr_in addr;
	int fd;

	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(DNS_PORT);
	if (inet_pton(AF_INET, server, &addr.sin_addr) != 1) {
		return -1;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		close(fd);
		return -1;
	}
	return fd;
}

static int
resolve_via_dns(const char *target, struct in_addr *addr, char *addrbuf, size_t addrbuf_len)
{
	unsigned char packet[514];
	unsigned char response[514];
	char server[INET_ADDRSTRLEN];
	int fd;
	int name_len;
	int packet_len;
	unsigned short response_len;
	unsigned short flags;
	unsigned short answer_count;
	size_t offset;

	load_dns_server(server, sizeof(server));
	memset(packet, 0, sizeof(packet));
	packet[2] = 0x12;
	packet[3] = 0x34;
	packet[4] = 0x01;
	packet[5] = 0x00;
	packet[7] = 0x01;

	name_len = encode_dns_name(target, &packet[14], sizeof(packet) - 18);
	if (name_len < 0) {
		return -1;
	}
	packet_len = 14 + name_len;
	dns_write_u16(&packet[packet_len], 1);
	packet_len += 2;
	dns_write_u16(&packet[packet_len], 1);
	packet_len += 2;
	dns_write_u16(&packet[0], (unsigned short)(packet_len - 2));

	fd = connect_tcp_dns_server(server);
	if (fd < 0) {
		return -1;
	}
	if (write_full(fd, packet, (size_t)packet_len) != 0 ||
	    read_full(fd, response, 2) != 0) {
		close(fd);
		return -1;
	}

	response_len = dns_read_u16(response);
	if (response_len < 12 || response_len > sizeof(response) - 2 ||
	    read_full(fd, &response[2], response_len) != 0) {
		close(fd);
		return -1;
	}
	close(fd);

	flags = dns_read_u16(&response[4]);
	answer_count = dns_read_u16(&response[8]);
	if ((flags & 0x8000U) == 0 || (flags & 0x000fU) != 0) {
		return -1;
	}

	offset = (size_t)(14 + name_len + 4);
	if (offset > (size_t)response_len + 2) {
		return -1;
	}

	while (answer_count-- > 0) {
		unsigned short type;
		unsigned short class_code;
		unsigned short rdlength;
		int next = skip_dns_name(response, (size_t)response_len + 2, offset);

		if (next < 0 || (size_t)next + 10 > (size_t)response_len + 2) {
			return -1;
		}
		offset = (size_t)next;
		type = dns_read_u16(&response[offset]);
		class_code = dns_read_u16(&response[offset + 2]);
		rdlength = dns_read_u16(&response[offset + 8]);
		offset += 10;
		if (offset + rdlength > (size_t)response_len + 2) {
			return -1;
		}

		if (type == 1 && class_code == 1 && rdlength == 4 &&
		    format_ipv4_addr(&response[offset], addrbuf, addrbuf_len) == 0) {
			memcpy(addr, &response[offset], sizeof(*addr));
			return 0;
		}

		offset += rdlength;
	}

	return -1;
}

static int
resolve_target(const char *target, struct sockaddr_in *dst, char *addrbuf, size_t addrbuf_len)
{
	struct addrinfo hints;
	struct addrinfo *res = NULL;
	struct sockaddr_in *resolved;
	int gai_error;

	memset(dst, 0, sizeof(*dst));
	dst->sin_len = sizeof(*dst);
	dst->sin_family = AF_INET;

	if (inet_pton(AF_INET, target, &dst->sin_addr) == 1) {
		if (format_ipv4_addr((const unsigned char *)&dst->sin_addr, addrbuf, addrbuf_len) != 0) {
			return -1;
		}
		return 0;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	gai_error = getaddrinfo(target, NULL, &hints, &res);
	if (gai_error == 0 && res != NULL && res->ai_addr != NULL && res->ai_addrlen >= sizeof(*dst)) {
		resolved = (struct sockaddr_in *)res->ai_addr;
		memcpy(dst, resolved, sizeof(*dst));
		dst->sin_len = sizeof(*dst);
		if (format_ipv4_addr((const unsigned char *)&dst->sin_addr, addrbuf, addrbuf_len) == 0) {
			freeaddrinfo(res);
			return 0;
		}
	}
	if (res != NULL) {
		freeaddrinfo(res);
	}

	if (resolve_via_dns(target, &dst->sin_addr, addrbuf, addrbuf_len) == 0) {
		return 0;
	}

	fprintf(stderr, "ping: cannot resolve %s: %s\n", target,
	    gai_error != 0 ? gai_strerror(gai_error) : "no IPv4 address found");
	return -1;
}

int
main(int argc, char **argv)
{
	struct sockaddr_in dst;
	struct icmp req;
	unsigned char recvbuf[1500];
	char addrbuf[INET_ADDRSTRLEN];
	const char *target;
	int count = 3;
	int fd;
	int i;
	int argi = 1;
	int received = 0;
	pid_t ident = getpid() & 0xffff;

	if (argc >= 3 && strcmp(argv[1], "-c") == 0) {
		count = atoi(argv[2]);
		argi = 3;
	}
	if (argi >= argc || count <= 0)
		usage();

	target = argv[argi];
	if (resolve_target(target, &dst, addrbuf, sizeof(addrbuf)) != 0) {
		return 1;
	}

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
	if (fd < 0) {
		perror("socket(ICMP)");
		return 1;
	}
	if (fcntl(fd, F_SETFL, O_NONBLOCK) != 0) {
		perror("fcntl(O_NONBLOCK)");
		close(fd);
		return 1;
	}

	printf("PING %s (%s)\n", target, addrbuf);
	for (i = 0; i < count; i++) {
		unsigned int poll_count;
		int got_reply = 0;

		memset(&req, 0, sizeof(req));
		req.icmp_type = ICMP_ECHO;
		req.icmp_code = 0;
		req.icmp_id = ident;
		req.icmp_seq = i;
		req.icmp_cksum = checksum16(&req, sizeof(req));

		if (sendto(fd, &req, sizeof(req), 0,
		    (struct sockaddr *)&dst, sizeof(dst)) != (ssize_t)sizeof(req)) {
			perror("sendto");
			close(fd);
			return 1;
		}

		for (poll_count = 0; poll_count != 400U; poll_count++) {
			ssize_t nread;
			struct sockaddr_in from;
			socklen_t fromlen = sizeof(from);
			char reply_addrbuf[INET_ADDRSTRLEN];

			nread = recvfrom(fd, recvbuf, sizeof(recvbuf), 0,
			    (struct sockaddr *)&from, &fromlen);
			if (nread < 0) {
				if (errno == EAGAIN || errno == EWOULDBLOCK) {
					usleep(10000);
					continue;
				}
				perror("recvfrom");
				close(fd);
				return 1;
			}

			if ((size_t)nread >= sizeof(struct ip) + sizeof(struct icmp)) {
				struct ip *ip = (struct ip *)(void *)recvbuf;
				size_t ip_hlen = (size_t)ip->ip_hl << 2;

				if ((size_t)nread >= ip_hlen + sizeof(struct icmp)) {
					struct icmp *reply =
					    (struct icmp *)(void *)(recvbuf + ip_hlen);
					if (reply->icmp_type == ICMP_ECHOREPLY &&
					    reply->icmp_id == ident &&
					    reply->icmp_seq == i) {
						received++;
						got_reply = 1;
						if (format_ipv4_addr((const unsigned char *)&from.sin_addr,
						    reply_addrbuf, sizeof(reply_addrbuf)) != 0) {
							strncpy(reply_addrbuf, "unknown", sizeof(reply_addrbuf));
							reply_addrbuf[sizeof(reply_addrbuf) - 1] = '\0';
						}
						printf("%zd bytes from %s: icmp_seq=%d\n", nread,
						    reply_addrbuf, i);
						break;
					}
				}
			}
		}

		if (!got_reply) {
			printf("timeout from %s seq=%d\n", addrbuf, i);
		}
	}

	close(fd);
	printf("--- %s ping statistics ---\n", target);
	printf("%d packets transmitted, %d packets received\n", count, received);
	return (received > 0) ? 0 : 1;
}
