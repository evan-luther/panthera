#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DNS_PORT 53

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
connect_tcp_dns_server(const char *server)
{
	struct sockaddr_in addr;
	int fd;

	memset(&addr, 0, sizeof(addr));
	addr.sin_len = sizeof(addr);
	addr.sin_family = AF_INET;
	addr.sin_port = htons(DNS_PORT);
	if (inet_pton(AF_INET, server, &addr.sin_addr) != 1) {
		fprintf(stderr, "dnsprobe: invalid tcp server %s\n", server);
		return 1;
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("dnsprobe: socket");
		return -1;
	}
	if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "dnsprobe: tcp connect %s:%d failed: %s\n",
		    server, DNS_PORT, strerror(errno));
		close(fd);
		return -1;
	}

	printf("dnsprobe: tcp connect ok %s:%d\n", server, DNS_PORT);
	return fd;
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

static int
verify_dns_query(const char *name, const char *server)
{
	unsigned char packet[514];
	unsigned char response[514];
	int fd;
	int name_len;
	int packet_len;
	unsigned short response_len;
	size_t offset;
	unsigned short answer_count;
	unsigned short flags;

	memset(packet, 0, sizeof(packet));
	packet[2] = 0x12;
	packet[3] = 0x34;
	packet[4] = 0x01;
	packet[5] = 0x00;
	packet[7] = 0x01;

	name_len = encode_dns_name(name, &packet[14], sizeof(packet) - 18);
	if (name_len < 0) {
		fprintf(stderr, "dnsprobe: invalid dns name %s\n", name);
		return 1;
	}
	packet_len = 14 + name_len;
	dns_write_u16(&packet[packet_len], 1);
	packet_len += 2;
	dns_write_u16(&packet[packet_len], 1);
	packet_len += 2;

	dns_write_u16(&packet[0], (unsigned short)(packet_len - 2));

	fd = connect_tcp_dns_server(server);
	if (fd < 0) {
		return 1;
	}
	if (write_full(fd, packet, (size_t)packet_len) != 0) {
		perror("dnsprobe: write");
		close(fd);
		return 1;
	}
	if (read_full(fd, response, 2) != 0) {
		perror("dnsprobe: read length");
		close(fd);
		return 1;
	}
	response_len = dns_read_u16(response);
	if (response_len < 12 || response_len > sizeof(response) - 2) {
		fprintf(stderr, "dnsprobe: invalid response length %u\n", response_len);
		close(fd);
		return 1;
	}
	if (read_full(fd, &response[2], response_len) != 0) {
		perror("dnsprobe: read payload");
		close(fd);
		return 1;
	}
	close(fd);

	flags = dns_read_u16(&response[4]);
	answer_count = dns_read_u16(&response[8]);
	if ((flags & 0x8000U) == 0 || (flags & 0x000fU) != 0) {
		fprintf(stderr, "dnsprobe: dns response error flags=0x%04x\n", flags);
		return 1;
	}

	offset = (size_t)(14 + name_len + 4);
	if (offset > (size_t)response_len + 2) {
		fprintf(stderr, "dnsprobe: truncated dns question\n");
		return 1;
	}

	while (answer_count-- > 0) {
		unsigned short type;
		unsigned short class_code;
		unsigned short rdlength;
		char addrbuf[INET_ADDRSTRLEN];
		int next = skip_dns_name(response, (size_t)response_len + 2, offset);

		if (next < 0 || (size_t)next + 10 > (size_t)response_len + 2) {
			fprintf(stderr, "dnsprobe: malformed dns answer\n");
			return 1;
		}
		offset = (size_t)next;
		type = dns_read_u16(&response[offset]);
		class_code = dns_read_u16(&response[offset + 2]);
		rdlength = dns_read_u16(&response[offset + 8]);
		offset += 10;
		if (offset + rdlength > (size_t)response_len + 2) {
			fprintf(stderr, "dnsprobe: truncated dns rdata\n");
			return 1;
		}

		if (type == 1 && class_code == 1 && rdlength == 4 &&
		    format_ipv4_addr(&response[offset], addrbuf, sizeof(addrbuf)) == 0) {
			printf("dnsprobe: resolved %s -> %s via %s\n", name, addrbuf, server);
			return 0;
		}

		offset += rdlength;
	}

	fprintf(stderr, "dnsprobe: no A record found for %s\n", name);
	return 1;
}

int
main(int argc, char **argv)
{
	const char *dns_name = "example.com";
	const char *dns_server = "10.0.2.3";

	if (argc >= 2) {
		dns_name = argv[1];
	}
	if (argc >= 3) {
		dns_server = argv[2];
	}
	if (argc > 3) {
		fprintf(stderr, "usage: dnsprobe [dns-name] [dns-server]\n");
		return 1;
	}

	if (verify_dns_query(dns_name, dns_server) != 0) {
		return 1;
	}
	return 0;
}
