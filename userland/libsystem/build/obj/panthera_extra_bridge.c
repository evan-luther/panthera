/*
 * panthera_extra_bridge.c
 *
 * Consolidated bridge and wrapper implementations for libpanthera_extra.
 */

#include "panthera_resolve_wave2.c"
#include "panthera_resolve_wave3.c"
#include "../patches/clang_runtime_impl.c"

/*
 * Preserve the legacy raw export surface for symbols that were previously
 * emitted from panthera_remaining.o. These are stripped from the final dylib
 * when the real providers exist elsewhere in the re-export chain.
 */
float fmodf(float x, float y) {
    return __builtin_fmodf(x, y);
}

float powf(float x, float y) {
    return __builtin_powf(x, y);
}

long double powl(long double x, long double y) {
    extern double pow(double, double);
    return (long double)pow((double)x, (double)y);
}



/* panthera_patch.sh impl: userland/dropbear/shims/freeaddrinfo.c */
/* freeaddrinfo() — free addrinfo linked list returned by getaddrinfo() */

extern void free(void *);

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    unsigned int ai_addrlen;
    char *ai_canonname;
    void *ai_addr;
    struct addrinfo *ai_next;
};

void freeaddrinfo(struct addrinfo *ai) {
    struct addrinfo *next;
    while (ai) {
        next = ai->ai_next;
        if (ai->ai_canonname)
            free(ai->ai_canonname);
        if (ai->ai_addr)
            free(ai->ai_addr);
        free(ai);
        ai = next;
    }
}


/* panthera_patch.sh impl: userland/dropbear/shims/gai_strerror.c */
/* gai_strerror() — error strings for getaddrinfo() */

/* EAI_* constants from <netdb.h> */
#define EAI_ADDRFAMILY  1
#define EAI_AGAIN       2
#define EAI_BADFLAGS    3
#define EAI_FAIL        4
#define EAI_FAMILY      5
#define EAI_MEMORY      6
#define EAI_NODATA      7
#define EAI_NONAME      8
#define EAI_SERVICE     9
#define EAI_SOCKTYPE   10
#define EAI_SYSTEM     11
#define EAI_OVERFLOW   14

const char *gai_strerror(int ecode) {
    switch (ecode) {
    case EAI_ADDRFAMILY: return "Address family not supported";
    case EAI_AGAIN:      return "Temporary failure in name resolution";
    case EAI_BADFLAGS:   return "Invalid flags";
    case EAI_FAIL:       return "Non-recoverable failure in name resolution";
    case EAI_FAMILY:     return "Address family not supported";
    case EAI_MEMORY:     return "Memory allocation failure";
    case EAI_NODATA:     return "No address associated with hostname";
    case EAI_NONAME:     return "Hostname not known";
    case EAI_SERVICE:    return "Service not known";
    case EAI_SOCKTYPE:   return "Socket type not supported";
    case EAI_SYSTEM:     return "System error";
    case EAI_OVERFLOW:   return "Argument buffer overflow";
    default:             return "Unknown error";
    }
}



/*
 * getaddrinfo() — standalone DNS resolver for Panthera.
 * Bypasses Libinfo entirely. Reads /etc/resolv.conf for nameserver,
 * sends raw UDP DNS queries (RFC 1035), parses responses.
 */

#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
#ifndef SOCK_STREAM
#define SOCK_STREAM 1
#endif

extern int socket(int, int, int);
extern int close(int);
extern void *malloc(unsigned long);
extern void *calloc(unsigned long, unsigned long);
extern unsigned long strlen(const char *);
extern int strcmp(const char *, const char *);
extern void *memcpy(void *, const void *, unsigned long);
extern char *strdup(const char *);
extern int sscanf(const char *, const char *, ...);
extern int snprintf(char *, unsigned long, const char *, ...);
extern int sprintf(char *, const char *, ...);
extern unsigned long strlcpy(char *, const char *, unsigned long);

/* sockaddr_in for IPv4 results */
struct panthera_sockaddr_in {
	uint8_t sin_len;
	uint8_t sin_family;
	uint16_t sin_port;
	uint32_t sin_addr;
	char sin_zero[8];
};

/* Parse /etc/resolv.conf, return nameserver IP in network byte order */
static uint32_t
panthera_get_nameserver(void)
{
	extern int open(const char *, int, ...);
	extern long read(int, void *, unsigned long);
	extern int close(int);

	char buf[256];
	int fd;
	long n;
	unsigned int a, b, c, d;

	fd = open("/etc/resolv.conf", 0 /* O_RDONLY */, 0);
	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';

	/* Find "nameserver X.X.X.X" */
	char *p = buf;
	while (*p) {
		if (p == buf || *(p-1) == '\n') {
			if (sscanf(p, "nameserver %u.%u.%u.%u", &a, &b, &c, &d) == 4)
				return (uint32_t)(a | (b << 8) | (c << 16) | (d << 24));
		}
		while (*p && *p != '\n') p++;
		if (*p == '\n') p++;
	}
	return 0;
}

/* Encode a DNS name: "www.google.com" → "\3www\6google\3com\0" */
static int
panthera_dns_encode_name(const char *name, uint8_t *dst, int max)
{
	const char *p = name;
	int total = 0;

	while (*p) {
		const char *dot = p;
		while (*dot && *dot != '.') dot++;
		int len = (int)(dot - p);
		if (len == 0 || len > 63 || total + len + 2 > max)
			return -1;
		dst[total++] = (uint8_t)len;
		memcpy(dst + total, p, (unsigned long)len);
		total += len;
		p = (*dot == '.') ? dot + 1 : dot;
	}
	dst[total++] = 0;
	return total;
}

/* Parse numeric IPv4 address string → network byte order.
 * Returns 1 on success, 0 if not a valid numeric address. */
static int
panthera_parse_ipv4(const char *s, uint32_t *out)
{
	unsigned int a, b, c, d;
	if (sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d) != 4)
		return 0;
	if (a > 255 || b > 255 || c > 255 || d > 255)
		return 0;
	*out = (uint32_t)(a | (b << 8) | (c << 16) | (d << 24));
	return 1;
}

/* Build an addrinfo node for an IPv4 address */
static struct addrinfo *
panthera_make_ai(uint32_t addr_nbo, uint16_t port_nbo, int socktype, int proto)
{
	struct addrinfo *ai;
	struct panthera_sockaddr_in *sa;

	ai = (struct addrinfo *)calloc(1, sizeof(*ai));
	if (!ai) return NULL;
	sa = (struct panthera_sockaddr_in *)calloc(1, sizeof(*sa));
	if (!sa) { free(ai); return NULL; }

	sa->sin_len = sizeof(*sa);
	sa->sin_family = AF_INET;
	sa->sin_port = port_nbo;
	sa->sin_addr = addr_nbo;

	ai->ai_family = AF_INET;
	ai->ai_socktype = socktype ? socktype : 1 /* SOCK_STREAM */;
	ai->ai_protocol = proto;
	ai->ai_addrlen = sizeof(*sa);
	ai->ai_addr = (void *)sa;
	return ai;
}

/* Convert service name/port string to port in network byte order */
static uint16_t
panthera_resolve_port(const char *servname)
{
	unsigned int port;
	if (!servname) return 0;
	if (sscanf(servname, "%u", &port) == 1 && port <= 65535) {
		return (uint16_t)((port >> 8) | ((port & 0xff) << 8)); /* htons */
	}
	if (strcmp(servname, "http") == 0)  return (uint16_t)((80 >> 8) | ((80 & 0xff) << 8));
	if (strcmp(servname, "https") == 0) return (uint16_t)((443 >> 8) | ((443 & 0xff) << 8));
	if (strcmp(servname, "ssh") == 0)   return (uint16_t)((22 >> 8) | ((22 & 0xff) << 8));
	if (strcmp(servname, "ftp") == 0)   return (uint16_t)((21 >> 8) | ((21 & 0xff) << 8));
	if (strcmp(servname, "domain") == 0) return (uint16_t)((53 >> 8) | ((53 & 0xff) << 8));
	if (strcmp(servname, "smtp") == 0)  return (uint16_t)((25 >> 8) | ((25 & 0xff) << 8));
	return 0;
}

int
getaddrinfo(const char *hostname, const char *servname,
    const struct addrinfo *hints, struct addrinfo **res)
{
	uint32_t addr_nbo;
	uint16_t port_nbo;
	int socktype = 0, proto = 0;

	if (!res) return EAI_FAIL;
	*res = NULL;

	if (hints) {
		socktype = hints->ai_socktype;
		proto = hints->ai_protocol;
	}

	port_nbo = panthera_resolve_port(servname);

	/* Case 1: no hostname → AI_PASSIVE gives INADDR_ANY, else loopback */
	if (!hostname || hostname[0] == '\0') {
		uint32_t bind_addr = 0x0100007f; /* 127.0.0.1 */
		if (hints && (hints->ai_flags & 0x01 /* AI_PASSIVE */))
			bind_addr = 0x00000000; /* INADDR_ANY (0.0.0.0) */
		*res = panthera_make_ai(bind_addr, port_nbo, socktype, proto);
		return *res ? 0 : EAI_MEMORY;
	}

	/* Case 2: numeric IPv4 address */
	if (panthera_parse_ipv4(hostname, &addr_nbo)) {
		*res = panthera_make_ai(addr_nbo, port_nbo, socktype, proto);
		return *res ? 0 : EAI_MEMORY;
	}

	/* Case 3: DNS lookup */
	{
		uint32_t ns = panthera_get_nameserver();
		if (ns == 0)
			return EAI_FAIL;

		/* Build DNS query packet (RFC 1035) */
		uint8_t qbuf[512];
		/* Header: ID=0x1234, RD=1, QDCOUNT=1 */
		qbuf[0] = 0x12; qbuf[1] = 0x34; /* ID */
		qbuf[2] = 0x01; qbuf[3] = 0x00; /* flags: RD=1 */
		qbuf[4] = 0x00; qbuf[5] = 0x01; /* QDCOUNT=1 */
		/* Question section */
		int nlen = panthera_dns_encode_name(hostname, qbuf + 12, 256);
		if (nlen < 0)
			return EAI_NONAME;
		int qoff = 12 + nlen;
		qbuf[qoff] = 0; qbuf[qoff+1] = 1;   /* QTYPE = A */
		qbuf[qoff+2] = 0; qbuf[qoff+3] = 1; /* QCLASS = IN */
		int qlen = qoff + 4;

		/* Send via UDP */
		extern int sendto(int, const void *, unsigned long, int,
		    const void *, unsigned int);
		extern long recvfrom(int, void *, unsigned long, int,
		    void *, unsigned int *);

		int fd = socket(AF_INET, SOCK_DGRAM, 0);
		if (fd < 0)
			return EAI_FAIL;

		struct panthera_sockaddr_in dst;
		dst.sin_len = sizeof(dst);
		dst.sin_family = AF_INET;
		dst.sin_port = (uint16_t)((53 >> 8) | ((53 & 0xff) << 8)); /* htons(53) */
		dst.sin_addr = ns;

		/* Set receive timeout: 5 seconds */
		struct { long tv_sec; long tv_usec; } tv = { 5, 0 };
		extern int setsockopt(int, int, int, const void *, unsigned int);
		setsockopt(fd, 0xffff /* SOL_SOCKET */, 0x1006 /* SO_RCVTIMEO */,
		    &tv, sizeof(tv));

		if (sendto(fd, qbuf, (unsigned long)qlen, 0,
		    (const void *)&dst, sizeof(dst)) < 0) {
			close(fd);
			return EAI_FAIL;
		}

		uint8_t rbuf[512];
		long rlen = recvfrom(fd, rbuf, sizeof(rbuf), 0, NULL, NULL);
		close(fd);

		if (rlen < 12)
			return EAI_FAIL;

		/* Parse response */
		int rcode = rbuf[3] & 0x0f;
		if (rcode != 0)
			return EAI_NONAME;

		int ancount = (rbuf[6] << 8) | rbuf[7];
		if (ancount == 0)
			return EAI_NONAME;

		/* Skip question section */
		int off = 12;
		while (off < rlen && rbuf[off] != 0) {
			if ((rbuf[off] & 0xc0) == 0xc0) { off += 2; break; }
			off += rbuf[off] + 1;
		}
		if (off < rlen && rbuf[off] == 0) off++;
		off += 4; /* skip QTYPE + QCLASS */

		/* Parse answer RRs */
		struct addrinfo *head = NULL, *tail = NULL;
		for (int i = 0; i < ancount && off + 10 < rlen; i++) {
			/* Skip name (may be compressed) */
			if ((rbuf[off] & 0xc0) == 0xc0) off += 2;
			else { while (off < rlen && rbuf[off] != 0) off += rbuf[off] + 1; off++; }

			if (off + 10 > rlen) break;
			uint16_t rtype = (uint16_t)((rbuf[off] << 8) | rbuf[off+1]);
			uint16_t rdlen = (uint16_t)((rbuf[off+8] << 8) | rbuf[off+9]);
			off += 10;

			if (rtype == 1 && rdlen == 4 && off + 4 <= rlen) {
				/* A record */
				uint32_t ip;
				memcpy(&ip, rbuf + off, 4);
				struct addrinfo *ai = panthera_make_ai(ip, port_nbo,
				    socktype, proto);
				if (ai) {
					if (!head) head = ai;
					if (tail) tail->ai_next = ai;
					tail = ai;
				}
			}
			off += rdlen;
		}

		if (!head)
			return EAI_NONAME;

		*res = head;
		return 0;
	}
}


/* panthera_patch.sh impl: userland/libsystem/build/obj/panthera_inet_ntop.c */
/*
 * inet_ntop — convert binary IP address to presentation string.
 * Based on Apple Libc-1583.40.7/net/inet_ntop.c (APSL 2.0 + ISC license).
 * Simplified for Panthera — standalone, no external dependencies.
 */
/* ---- inet_ntop / if_nametoindex implementations ----
 * Self-contained: no <sys/socket.h> or <netinet/in.h> includes
 * to avoid conflicts with wave3 socket wrappers. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef AF_INET
#define AF_INET 2
#endif
#ifndef AF_INET6
#define AF_INET6 30
#endif

/* Minimal struct definitions for inet_ntop */
struct panthera_in_addr  { uint32_t s_addr; };
struct panthera_in6_u    { uint8_t __u6_addr8[16]; };
struct panthera_in6_addr { union { struct panthera_in6_u __u6_addr; } __u6_addr; };

#define MAX_V4_ADDR_LEN 16
#define NS_IN6ADDRSZ 16
#define NS_INT16SZ 2

const char *
inet_ntop4(const struct panthera_in_addr *addr, char *dst, unsigned int size)
{
	const uint8_t *ap = (const uint8_t *)&addr->s_addr;
	char tmp[MAX_V4_ADDR_LEN];
	int len;

	if (addr == NULL || dst == NULL) {
		errno = ENOSPC;
		return NULL;
	}

	len = snprintf(tmp, sizeof(tmp), "%u.%u.%u.%u",
	    (unsigned)ap[0], (unsigned)ap[1], (unsigned)ap[2], (unsigned)ap[3]);
	if (len < 0 || (unsigned int)(len + 1) > size) {
		errno = ENOSPC;
		return NULL;
	}

	memcpy(dst, tmp, (unsigned int)(len + 1));
	return dst;
}

const char *
inet_ntop6(const struct panthera_in6_addr *addr, char *dst, unsigned int size)
{
	const uint8_t *src = addr->__u6_addr.__u6_addr.__u6_addr8;
	char tmp[sizeof("ffff:ffff:ffff:ffff:ffff:ffff:255.255.255.255")], *tp;
	struct { int base, len; } best, cur;
	unsigned int words[NS_IN6ADDRSZ / NS_INT16SZ];
	int i;

	for (i = 0; i < NS_IN6ADDRSZ; i++)
		words[i / 2] |= (src[i] << ((1 - (i % 2)) << 3));

	best.base = -1; best.len = 0;
	cur.base = -1; cur.len = 0;

	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
		if (words[i] == 0) {
			if (cur.base == -1) { cur.base = i; cur.len = 1; }
			else cur.len++;
		} else {
			if (cur.base != -1) {
				if (best.base == -1 || cur.len > best.len) best = cur;
				cur.base = -1;
			}
		}
	}
	if (cur.base != -1 && (best.base == -1 || cur.len > best.len))
		best = cur;
	if (best.base != -1 && best.len < 2)
		best.base = -1;

	tp = tmp;
	for (i = 0; i < (NS_IN6ADDRSZ / NS_INT16SZ); i++) {
		if (best.base != -1 && i >= best.base && i < (best.base + best.len)) {
			if (i == best.base) *tp++ = ':';
			continue;
		}
		if (i != 0) *tp++ = ':';
		tp += sprintf(tp, "%x", words[i]);
	}
	if (best.base != -1 && (best.base + best.len) == (NS_IN6ADDRSZ / NS_INT16SZ))
		*tp++ = ':';
	*tp++ = '\0';

	if ((unsigned int)(tp - tmp) > size) {
		errno = ENOSPC;
		return NULL;
	}
	strcpy(dst, tmp);
	return dst;
}

const char *
inet_ntop(int af, const void *addr, char *buf, unsigned int len)
{
	if (addr && af == AF_INET6) return inet_ntop6(addr, buf, len);
	if (addr && af == AF_INET)  return inet_ntop4(addr, buf, len);
	errno = EAFNOSUPPORT;
	return NULL;
}

/* if_nametoindex — convert interface name to index via ioctl.
 * We avoid including <net/if.h> which transitively pulls in
 * <sys/socket.h> that conflicts with the wave3 socket wrappers. */
#ifndef IFNAMSIZ
#define IFNAMSIZ 16
#endif
#ifndef SIOCGIFINDEX
#define SIOCGIFINDEX 0xc0206920
#endif

struct panthera_ifreq {
	char ifr_name[IFNAMSIZ];
	int ifr_index;
	char _pad[256]; /* enough for any ifreq variant */
};

#ifndef SOCK_DGRAM
#define SOCK_DGRAM 2
#endif
extern int socket(int, int, int);
extern int ioctl(int, unsigned long, ...);
extern int close(int);

unsigned int
if_nametoindex(const char *ifname)
{
	struct panthera_ifreq ifr;
	int fd;

	if (ifname == NULL) {
		errno = ENXIO;
		return 0;
	}
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0)
		return 0;
	strlcpy(ifr.ifr_name, ifname, IFNAMSIZ);
	if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
		close(fd);
		errno = ENXIO;
		return 0;
	}
	close(fd);
	return (unsigned int)ifr.ifr_index;
}

/* if_indextoname — convert interface index back to name.
 * Needed by si_getaddrinfo (nameinfo path). */
char *
if_indextoname(unsigned int ifindex, char *ifname)
{
	(void)ifindex;
	if (ifname) ifname[0] = '\0';
	errno = ENXIO;
	return NULL;
}

/* _inet_aton_check — parse dotted-quad IP to binary. Used by si_getaddrinfo. */
int
_inet_aton_check(const char *cp, struct panthera_in_addr *addr, int strict)
{
	(void)strict;
	/* Simple dotted-quad parser */
	unsigned int a, b, c, d;
	if (cp == NULL || addr == NULL) return 0;
	if (sscanf(cp, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0;
	if (a > 255 || b > 255 || c > 255 || d > 255) return 0;
	addr->s_addr = (uint32_t)((a) | (b << 8) | (c << 16) | (d << 24));
	return 1;
}

/* dlerror — report the last Panthera runtime loader failure. */
extern const char *panthera_dlopen_last_error(void);
char *dlerror(void) {
	const char *error = panthera_dlopen_last_error();
	return (char *)(error != NULL ? error : "");
}

/* notify_check stub removed — real implementation in libsystem_notify.dylib */

/* _gai_simple already in panthera_extra_stubs.c */


/* panthera_patch.sh impl: /tmp/exp10_impl.c */
#include <math.h>
double __exp10(double x) { return pow(10.0, x); }
float __exp10f(float x) { return powf(10.0f, x); }



/* panthera_patch.sh impl: Tier 2 symbols for libarchive and future packages */

/* === ACL stubs (ACLs not supported on Panthera) === */
typedef void *acl_t;
typedef void *acl_entry_t;
typedef void *acl_permset_t;
typedef void *acl_flagset_t;
typedef int acl_perm_t;
typedef int acl_flag_t;
typedef int acl_tag_t;
typedef int acl_type_t;

acl_t acl_init(int count) { (void)count; return (void*)0; }
acl_t acl_dup(acl_t acl) { (void)acl; return (void*)0; }
acl_t acl_get_fd(int fd) { (void)fd; return (void*)0; }
acl_t acl_get_fd_np(int fd, acl_type_t type) { (void)fd; (void)type; return (void*)0; }
acl_t acl_get_link_np(const char *p, acl_type_t t) { (void)p; (void)t; return (void*)0; }
int acl_set_fd(int fd, acl_t acl) { (void)fd; (void)acl; return -1; }
int acl_set_fd_np(int fd, acl_t acl, acl_type_t t) { (void)fd; (void)acl; (void)t; return -1; }
int acl_set_link_np(const char *p, acl_type_t t, acl_t a) { (void)p; (void)t; (void)a; return -1; }
int acl_get_entry(acl_t a, int i, acl_entry_t *e) { (void)a; (void)i; (void)e; return -1; }
int acl_create_entry(acl_t *a, acl_entry_t *e) { (void)a; (void)e; return -1; }
int acl_get_tag_type(acl_entry_t e, acl_tag_t *t) { (void)e; (void)t; return -1; }
int acl_set_tag_type(acl_entry_t e, acl_tag_t t) { (void)e; (void)t; return -1; }
void *acl_get_qualifier(acl_entry_t e) { (void)e; return (void*)0; }
int acl_set_qualifier(acl_entry_t e, const void *q) { (void)e; (void)q; return -1; }
int acl_get_permset(acl_entry_t e, acl_permset_t *p) { (void)e; (void)p; return -1; }
int acl_clear_perms(acl_permset_t p) { (void)p; return 0; }
int acl_add_perm(acl_permset_t p, acl_perm_t v) { (void)p; (void)v; return 0; }
int acl_get_perm_np(acl_permset_t p, acl_perm_t v) { (void)p; (void)v; return 0; }
int acl_get_flagset_np(void *e, acl_flagset_t *f) { (void)e; (void)f; return -1; }
int acl_clear_flags_np(acl_flagset_t f) { (void)f; return 0; }
int acl_add_flag_np(acl_flagset_t f, acl_flag_t v) { (void)f; (void)v; return 0; }
int acl_get_flag_np(acl_flagset_t f, acl_flag_t v) { (void)f; (void)v; return 0; }

/* === CommonCrypto stubs (return success but zero output) === */
typedef unsigned int CC_LONG;
typedef struct { unsigned char d[96]; } CC_MD5_CTX;
int CC_MD5_Init(CC_MD5_CTX *c) { (void)c; return 1; }
int CC_MD5_Update(CC_MD5_CTX *c, const void *d, CC_LONG l) { (void)c; (void)d; (void)l; return 1; }
int CC_MD5_Final(unsigned char *md, CC_MD5_CTX *c) { (void)c; int i; for(i=0;i<16;i++) md[i]=0; return 1; }

typedef struct { unsigned char d[96]; } CC_SHA1_CTX;
int CC_SHA1_Init(CC_SHA1_CTX *c) { (void)c; return 1; }
int CC_SHA1_Update(CC_SHA1_CTX *c, const void *d, CC_LONG l) { (void)c; (void)d; (void)l; return 1; }
int CC_SHA1_Final(unsigned char *md, CC_SHA1_CTX *c) { (void)c; int i; for(i=0;i<20;i++) md[i]=0; return 1; }

typedef struct { unsigned char d[112]; } CC_SHA256_CTX;
int CC_SHA256_Init(CC_SHA256_CTX *c) { (void)c; return 1; }
int CC_SHA256_Update(CC_SHA256_CTX *c, const void *d, CC_LONG l) { (void)c; (void)d; (void)l; return 1; }
int CC_SHA256_Final(unsigned char *md, CC_SHA256_CTX *c) { (void)c; int i; for(i=0;i<32;i++) md[i]=0; return 1; }

typedef struct { unsigned char d[208]; } CC_SHA512_CTX;
int CC_SHA384_Init(CC_SHA512_CTX *c) { (void)c; return 1; }
int CC_SHA384_Update(CC_SHA512_CTX *c, const void *d, CC_LONG l) { (void)c; (void)d; (void)l; return 1; }
int CC_SHA384_Final(unsigned char *md, CC_SHA512_CTX *c) { (void)c; int i; for(i=0;i<48;i++) md[i]=0; return 1; }
int CC_SHA512_Init(CC_SHA512_CTX *c) { (void)c; return 1; }
int CC_SHA512_Update(CC_SHA512_CTX *c, const void *d, CC_LONG l) { (void)c; (void)d; (void)l; return 1; }
int CC_SHA512_Final(unsigned char *md, CC_SHA512_CTX *c) { (void)c; int i; for(i=0;i<64;i++) md[i]=0; return 1; }

typedef unsigned int CCCryptorStatus;
typedef void *CCCryptorRef;
CCCryptorStatus CCCryptorCreateWithMode(int op, int m, int a, int p, const void *iv,
    const void *k, unsigned long kl, const void *tw, unsigned long twl, int nr, int o, CCCryptorRef *r) {
    (void)op;(void)m;(void)a;(void)p;(void)iv;(void)k;(void)kl;(void)tw;(void)twl;(void)nr;(void)o;(void)r;
    return (CCCryptorStatus)-1;
}
CCCryptorStatus CCCryptorUpdate(CCCryptorRef c, const void *i, unsigned long il, void *o, unsigned long ol, unsigned long *m) {
    (void)c;(void)i;(void)il;(void)o;(void)ol;(void)m; return (CCCryptorStatus)-1;
}
CCCryptorStatus CCCryptorReset(CCCryptorRef c, const void *iv) { (void)c;(void)iv; return (CCCryptorStatus)-1; }
CCCryptorStatus CCCryptorRelease(CCCryptorRef c) { (void)c; return 0; }

typedef struct { unsigned char d[304]; } CCHmacContext;
void CCHmacInit(CCHmacContext *c, int a, const void *k, unsigned long kl) { (void)c;(void)a;(void)k;(void)kl; }
void CCHmacUpdate(CCHmacContext *c, const void *d, unsigned long l) { (void)c;(void)d;(void)l; }
void CCHmacFinal(CCHmacContext *c, void *m) { (void)c; int i; unsigned char *p=(unsigned char*)m; for(i=0;i<32;i++) p[i]=0; }

int CCKeyDerivationPBKDF(int a, const char *p, unsigned long pl, const unsigned char *s, unsigned long sl,
    int prf, unsigned r, unsigned char *dk, unsigned long dkl) {
    (void)a;(void)p;(void)pl;(void)s;(void)sl;(void)prf;(void)r;(void)dk;(void)dkl; return -1;
}

/* === POSIX functions === */
struct timeval {
	long tv_sec;
	int tv_usec;
};

extern int futimes(int, const struct timeval *);
extern int utimes(const char *, const struct timeval *);
extern int fsetattrlist(int, void *, void *, unsigned long, unsigned int);
extern int setattrlistat(int, const char *, void *, void *, unsigned long, unsigned int);
extern int gettimeofday(struct timeval *, void *);

struct panthera_timespec {
	long tv_sec;
	long tv_nsec;
};

struct panthera_attrlist {
	unsigned short bitmapcount;
	unsigned short reserved;
	unsigned int commonattr;
	unsigned int volattr;
	unsigned int dirattr;
	unsigned int fileattr;
	unsigned int forkattr;
};

#ifndef AT_FDCWD
#define AT_FDCWD -2
#endif
#ifndef AT_SYMLINK_NOFOLLOW
#define AT_SYMLINK_NOFOLLOW 0x0020
#endif
#ifndef AT_SYMLINK_NOFOLLOW_ANY
#define AT_SYMLINK_NOFOLLOW_ANY 0x0800
#endif
#ifndef UTIME_NOW
#define UTIME_NOW -1
#endif
#ifndef UTIME_OMIT
#define UTIME_OMIT -2
#endif
#define PANTHERA_ATTR_BIT_MAP_COUNT 5
#define PANTHERA_ATTR_CMN_MODTIME 0x00000400
#define PANTHERA_ATTR_CMN_ACCTIME 0x00001000
#define PANTHERA_ATTR_CMN_ACCESSMASK 0x00020000
#define PANTHERA_FSOPT_NOFOLLOW 0x00000001
#define PANTHERA_FSOPT_UTIMES_NULL 0x00000400
#define PANTHERA_FSOPT_NOFOLLOW_ANY 0x00000800

static int panthera_prepare_utimens_times(
	struct panthera_timespec times_in[2],
	struct panthera_timespec times_out[2],
	unsigned long *times_out_size,
	unsigned int *fs_flags)
{
	struct panthera_timespec *cursor;
	unsigned int attrs;
	int i;

	for (i = 0; i < 2; i++) {
		long nsec = times_in[i].tv_nsec;
		if (nsec == UTIME_NOW || nsec == UTIME_OMIT)
			continue;
		if (nsec < 0 || nsec >= 1000000000L) {
			errno = EINVAL;
			return -1;
		}
	}

	if (times_in[0].tv_nsec == UTIME_OMIT &&
	    times_in[1].tv_nsec == UTIME_OMIT) {
		*times_out_size = 0;
		return 0;
	}

	if (times_in[0].tv_nsec == UTIME_NOW ||
	    times_in[1].tv_nsec == UTIME_NOW) {
		struct timeval now_tv;
		struct panthera_timespec now_ts;

		if (gettimeofday(&now_tv, 0) != 0)
			return -1;
		now_ts.tv_sec = now_tv.tv_sec;
		now_ts.tv_nsec = (long)now_tv.tv_usec * 1000L;
		*fs_flags |= PANTHERA_FSOPT_UTIMES_NULL;
		if (times_in[0].tv_nsec == UTIME_NOW)
			times_in[0] = now_ts;
		if (times_in[1].tv_nsec == UTIME_NOW)
			times_in[1] = now_ts;
	}

	attrs = 0;
	*times_out_size = 0;
	cursor = times_out;
	if (times_in[1].tv_nsec != UTIME_OMIT) {
		attrs |= PANTHERA_ATTR_CMN_MODTIME;
		*cursor++ = times_in[1];
		*times_out_size += sizeof(struct panthera_timespec);
	}
	if (times_in[0].tv_nsec != UTIME_OMIT) {
		attrs |= PANTHERA_ATTR_CMN_ACCTIME;
		*cursor = times_in[0];
		*times_out_size += sizeof(struct panthera_timespec);
	}
	return (int)attrs;
}

static void panthera_init_times_now(struct panthera_timespec times_in[2])
{
	times_in[0].tv_sec = 0;
	times_in[0].tv_nsec = UTIME_NOW;
	times_in[1].tv_sec = 0;
	times_in[1].tv_nsec = UTIME_NOW;
}

int futimens(int fd, const void *ts) {
	struct panthera_timespec times_in[2];
	struct panthera_timespec times_out[2];
	struct panthera_attrlist attrs;
	unsigned long attrbuf_size;
	unsigned int fs_flags;
	int commonattr;

	if (ts != 0)
		memcpy(times_in, ts, sizeof(times_in));
	else
		panthera_init_times_now(times_in);

	fs_flags = 0;
	commonattr = panthera_prepare_utimens_times(times_in, times_out, &attrbuf_size, &fs_flags);
	if (commonattr < 0)
		return -1;

	attrs.bitmapcount = PANTHERA_ATTR_BIT_MAP_COUNT;
	attrs.reserved = 0;
	attrs.commonattr = (unsigned int)commonattr;
	attrs.volattr = 0;
	attrs.dirattr = 0;
	attrs.fileattr = 0;
	attrs.forkattr = 0;
	return fsetattrlist(fd, &attrs, times_out, attrbuf_size, fs_flags);
}

int utimensat(int dirfd, const char *path, const void *ts, int flag) {
	struct panthera_timespec times_in[2];
	struct panthera_timespec times_out[2];
	struct panthera_attrlist attrs;
	unsigned long attrbuf_size;
	unsigned int fs_flags;
	int commonattr;

	if (path == 0) {
		errno = EINVAL;
		return -1;
	}
	if ((flag & ~(AT_SYMLINK_NOFOLLOW | AT_SYMLINK_NOFOLLOW_ANY)) != 0) {
		errno = EINVAL;
		return -1;
	}
	if (ts == 0)
		panthera_init_times_now(times_in);
	else
		memcpy(times_in, ts, sizeof(times_in));

	fs_flags = 0;
	commonattr = panthera_prepare_utimens_times(times_in, times_out, &attrbuf_size, &fs_flags);
	if (commonattr < 0)
		return -1;
	if ((flag & AT_SYMLINK_NOFOLLOW) != 0)
		fs_flags |= PANTHERA_FSOPT_NOFOLLOW;
	if ((flag & AT_SYMLINK_NOFOLLOW_ANY) != 0)
		fs_flags |= PANTHERA_FSOPT_NOFOLLOW_ANY;

	attrs.bitmapcount = PANTHERA_ATTR_BIT_MAP_COUNT;
	attrs.reserved = 0;
	attrs.commonattr = (unsigned int)commonattr;
	attrs.volattr = 0;
	attrs.dirattr = 0;
	attrs.fileattr = 0;
	attrs.forkattr = 0;
	return setattrlistat(dirfd, path, &attrs, times_out, attrbuf_size, fs_flags);
}

/* unlinkat: kernel has ___unlinkat */
extern int __unlinkat(int, const char *, int);
int unlinkat(int dirfd, const char *path, int flag) {
    return __unlinkat(dirfd, path, flag);
}

extern int unlink(const char *);
extern int rmdir(const char *);

int removefile(const char *path, void *state, unsigned int flags) {
    int saved_errno;

    (void)state;
    (void)flags;
    if (path == 0) {
        errno = EINVAL;
        return -1;
    }
    if (unlink(path) == 0)
        return 0;
    saved_errno = errno;
    if (saved_errno == EISDIR || saved_errno == EPERM)
        return rmdir(path);
    errno = saved_errno;
    return -1;
}

int lchflags(const char *path, unsigned int flags) { (void)path; (void)flags; return -1; }
int lchmod(const char *path, unsigned short mode) {
	struct panthera_attrlist attrs;
	int access_mode;

	if (path == 0) {
		errno = EINVAL;
		return -1;
	}
	attrs.bitmapcount = PANTHERA_ATTR_BIT_MAP_COUNT;
	attrs.reserved = 0;
	attrs.commonattr = PANTHERA_ATTR_CMN_ACCESSMASK;
	attrs.volattr = 0;
	attrs.dirattr = 0;
	attrs.fileattr = 0;
	attrs.forkattr = 0;
	access_mode = mode;
	return setattrlistat(AT_FDCWD, path, &attrs, &access_mode,
	    sizeof(access_mode), PANTHERA_FSOPT_NOFOLLOW);
}
int lutimes(const char *path, const void *tv) {
	const struct timeval *in;
	struct panthera_timespec times_in[2];
	struct panthera_timespec times_out[2];
	struct panthera_attrlist attrs;
	unsigned long attrbuf_size;
	unsigned int fs_flags;
	int commonattr;

	if (path == 0) {
		errno = EINVAL;
		return -1;
	}
	if (tv != 0) {
		in = (const struct timeval *)tv;
		times_in[0].tv_sec = in[0].tv_sec;
		times_in[0].tv_nsec = (long)in[0].tv_usec * 1000L;
		times_in[1].tv_sec = in[1].tv_sec;
		times_in[1].tv_nsec = (long)in[1].tv_usec * 1000L;
	} else {
		panthera_init_times_now(times_in);
	}

	fs_flags = PANTHERA_FSOPT_NOFOLLOW;
	commonattr = panthera_prepare_utimens_times(times_in, times_out, &attrbuf_size, &fs_flags);
	if (commonattr < 0)
		return -1;

	attrs.bitmapcount = PANTHERA_ATTR_BIT_MAP_COUNT;
	attrs.reserved = 0;
	attrs.commonattr = (unsigned int)commonattr;
	attrs.volattr = 0;
	attrs.dirattr = 0;
	attrs.fileattr = 0;
	attrs.forkattr = 0;
	return setattrlistat(AT_FDCWD, path, &attrs, times_out, attrbuf_size, fs_flags);
}
int mbr_uuid_to_id(const void *uuid, unsigned int *id, int *type) { (void)uuid; *id = 0; *type = 0; return -1; }

unsigned long regerror(int errcode, const void *preg, char *errbuf, unsigned long errbuf_size) {
    (void)preg;
    const char *msg = (errcode == 0) ? "Success" : "Regex error";
    unsigned long len = 0;
    const char *p = msg;
    while (*p++) len++;
    len++;
    if (errbuf && errbuf_size > 0) {
        unsigned long i;
        for (i = 0; i < errbuf_size - 1 && msg[i]; i++) errbuf[i] = msg[i];
        errbuf[i] = '\0';
    }
    return len;
}


/* panthera_patch.sh impl: arc4random_panthera.c — ChaCha20 CSPRNG */
extern int getentropy(void *buf, unsigned long buflen);

#define _ROTL(a,b) (((a) << (b)) | ((a) >> (32 - (b))))
#define _QR(a, b, c, d) do { \
    a += b; d ^= a; d = _ROTL(d,16); \
    c += d; b ^= c; b = _ROTL(b,12); \
    a += b; d ^= a; d = _ROTL(d, 8); \
    c += d; b ^= c; b = _ROTL(b, 7); \
} while(0)

static void _chacha20_block(uint32_t out[16], const uint32_t in[16]) {
    uint32_t x[16];
    __builtin_memcpy(x, in, 64);
    for (int i = 0; i < 10; i++) {
        _QR(x[0], x[4], x[ 8], x[12]); _QR(x[1], x[5], x[ 9], x[13]);
        _QR(x[2], x[6], x[10], x[14]); _QR(x[3], x[7], x[11], x[15]);
        _QR(x[0], x[5], x[10], x[15]); _QR(x[1], x[6], x[11], x[12]);
        _QR(x[2], x[7], x[ 8], x[13]); _QR(x[3], x[4], x[ 9], x[14]);
    }
    for (int i = 0; i < 16; i++) out[i] = x[i] + in[i];
}

static uint32_t _cc_state[16];
static uint8_t _rs_buf[256];
static int _rs_have = 0, _rs_count = 0, _rs_init = 0;

static void _rs_seed(void) {
    uint8_t seed[40];
    getentropy(seed, 40);
    _cc_state[0]=0x61707865; _cc_state[1]=0x3320646e;
    _cc_state[2]=0x79622d32; _cc_state[3]=0x6b206574;
    __builtin_memcpy(&_cc_state[4], seed, 32);
    _cc_state[12]=0; _cc_state[13]=0;
    __builtin_memcpy(&_cc_state[14], seed+32, 8);
    _rs_have=0; _rs_count=1600000; _rs_init=1;
}

static void _rs_fill(void) {
    uint32_t out[16];
    for (int i=0; i<(int)sizeof(_rs_buf); i+=64) {
        _chacha20_block(out, _cc_state);
        _cc_state[12]++;
        int rem=(int)sizeof(_rs_buf)-i;
        __builtin_memcpy(_rs_buf+i, out, rem<64?rem:64);
    }
    _rs_have=(int)sizeof(_rs_buf);
}

void arc4random_buf(void *buf, size_t n) {
    if (!_rs_init) _rs_seed();
    uint8_t *p=(uint8_t*)buf;
    while (n>0) {
        if (_rs_count<=0) _rs_seed();
        if (_rs_have<=0) _rs_fill();
        size_t take=n<(size_t)_rs_have?n:(size_t)_rs_have;
        __builtin_memcpy(p, _rs_buf+sizeof(_rs_buf)-_rs_have, take);
        _rs_have-=(int)take; _rs_count-=(int)take;
        p+=take; n-=take;
    }
}

uint32_t arc4random(void) {
    uint32_t val;
    arc4random_buf(&val, sizeof(val));
    return val;
}

void arc4random_stir(void) { _rs_seed(); }
void arc4random_addrandom(unsigned char *d, int l) { (void)d; (void)l; }

uint32_t arc4random_uniform(uint32_t upper_bound) {
    uint32_t r, min;
    if (upper_bound < 2) return 0;
    min = -upper_bound % upper_bound;
    for (;;) { r = arc4random(); if (r >= min) break; }
    return r % upper_bound;
}

/* getopt: delegate to libc's getopt$UNIX2003 */
extern int getopt_unix2003(int, char *const *, const char *) __asm__("_getopt$UNIX2003");
int panthera_getopt(int ac, char *const *av, const char *o) __asm__("_getopt");
int panthera_getopt(int ac, char *const *av, const char *o) { return getopt_unix2003(ac, av, o); }

/* panthera_patch.sh impl: /tmp/panthera_icu_math.c */
/* Math functions needed by ICU: expf, tanhf, __sincos_stret */
#include <math.h>

float expf(float x) {
    return (float)exp((double)x);
}

float tanhf(float x) {
    return (float)tanh((double)x);
}

/* x86_64 struct-return sincos: returns sin and cos of x */
struct __sincos_stret_result { double __sinval; double __cosval; };
struct __sincos_stret_result __sincos_stret(double x) {
    struct __sincos_stret_result r;
    r.__sinval = sin(x);
    r.__cosval = cos(x);
    return r;
}


/* pthread_main_thread_np — frozen libsystem_pthread returns NULL.
 * Provide working impl. Avoid #include <pthread.h> (conflicts with wave2). */
extern void *pthread_self(void);
static void *_panthera_main_thread_ptr = 0;

void *pthread_main_thread_np(void) {
    if (!_panthera_main_thread_ptr)
        _panthera_main_thread_ptr = pthread_self();
    return _panthera_main_thread_ptr;
}


/* panthera_patch.sh impl: /tmp/kernelrpc_stubs.c */
/* kernelrpc_stubs.c — MIG client stub fallbacks for Panthera.
 * libsystem_kernel references these as undefined (ordinal -2).
 * On real macOS they're MIG-generated. Here we provide minimal stubs
 * so the cache bindings don't jump to address 0. */
#include <stdint.h>

#ifndef _PANTHERA_MACH_TYPES_DECLARED
#define _PANTHERA_MACH_TYPES_DECLARED
typedef int kern_return_t;
typedef uint32_t mach_port_t;
typedef uint32_t mach_port_name_t;
typedef int mach_port_right_t;
typedef int mach_msg_type_name_t;
typedef int mach_port_delta_t;
typedef uint64_t mach_port_context_t;
typedef uint64_t mach_port_guard_t;

#define KERN_SUCCESS 0
#define KERN_INVALID_ARGUMENT 4
#define KERN_FAILURE 5

#define MACH_PORT_NULL ((mach_port_t)0)
#define MACH_PORT_DEAD ((mach_port_t)~0)

#define MACH_PORT_RIGHT_SEND 0
#define MACH_PORT_RIGHT_RECEIVE 1
#define MACH_PORT_RIGHT_SEND_ONCE 2
#define MACH_PORT_RIGHT_PORT_SET 3
#define MACH_PORT_RIGHT_DEAD_NAME 4

#define MACH_MSG_TYPE_MAKE_SEND 20
#define MACH_MSG_TYPE_MAKE_SEND_ONCE 21
#define MACH_MSG_TYPE_COPY_SEND 19

extern kern_return_t mach_port_allocate(mach_port_t task, mach_port_right_t right, mach_port_name_t *name);
extern kern_return_t mach_port_insert_right(mach_port_t task, mach_port_name_t name, mach_port_t poly, mach_msg_type_name_t polyPoly);
extern kern_return_t mach_port_destroy(mach_port_t task, mach_port_name_t name);
extern kern_return_t mach_port_deallocate(mach_port_t task, mach_port_name_t name);
extern kern_return_t mach_port_set_context(mach_port_t task, mach_port_name_t name, mach_port_context_t context) __attribute__((weak));
extern kern_return_t mach_port_get_context(mach_port_t task, mach_port_name_t name, mach_port_context_t *context);
extern kern_return_t mach_port_mod_refs(mach_port_t task, mach_port_name_t name, mach_port_right_t right, mach_port_delta_t delta);
#endif
/* mach_port_set_attributes — CF needs this to set queue limits. Safe to succeed as no-op. */
kern_return_t _kernelrpc_mach_port_set_attributes(uint32_t task, uint32_t name,
    int flavor, void *info, uint32_t count) {
    (void)task; (void)name; (void)flavor; (void)info; (void)count;
    return KERN_SUCCESS;
}

/* mach_port_get_attributes — CF may query port attributes */
kern_return_t _kernelrpc_mach_port_get_attributes(uint32_t task, uint32_t name,
    int flavor, void *info, uint32_t *count) {
    (void)task; (void)name; (void)flavor; (void)info; (void)count;
    return KERN_FAILURE;
}

/* mach_port_guard / unguard — used by dispatch for port protection */
kern_return_t _kernelrpc_mach_port_guard(uint32_t task, uint32_t name,
    uint64_t guard, int strict) {
    (void)task; (void)name; (void)guard; (void)strict;
    return KERN_SUCCESS;
}
kern_return_t _kernelrpc_mach_port_unguard(uint32_t task, uint32_t name, uint64_t guard) {
    (void)task; (void)name; (void)guard;
    return KERN_SUCCESS;
}
kern_return_t _kernelrpc_mach_port_guard_with_flags(uint32_t task, uint32_t name,
    uint64_t guard, uint64_t flags) {
    (void)task; (void)name; (void)guard; (void)flags;
    return KERN_SUCCESS;
}
kern_return_t _kernelrpc_mach_port_swap_guard(uint32_t task, uint32_t name,
    uint64_t old_guard, uint64_t new_guard) {
    (void)task; (void)name; (void)old_guard; (void)new_guard;
    return KERN_SUCCESS;
}

/* Default stub for everything else — returns KERN_FAILURE */
kern_return_t _kernelrpc_mach_port_allocate(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_allocate_full(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_allocate_name(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_allocate_qos(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_assert_attributes(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_construct(uint32_t task, void *options,
    uint64_t context, uint32_t *name) {
    (void)options;
    if (!name) return KERN_INVALID_ARGUMENT;
    mach_port_t port = MACH_PORT_NULL;
    kern_return_t kr = mach_port_allocate(task, MACH_PORT_RIGHT_RECEIVE, &port);
    if (kr != KERN_SUCCESS) return kr;
    kr = mach_port_insert_right(task, port, port, MACH_MSG_TYPE_MAKE_SEND);
    if (kr != KERN_SUCCESS) {
        mach_port_destroy(task, port);
        return kr;
    }
    if (context != 0 && mach_port_set_context) {
        mach_port_set_context(task, port, (mach_port_context_t)context);
    }
    *name = port;
    return KERN_SUCCESS;
}
kern_return_t _kernelrpc_mach_port_deallocate(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_destroy(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_destruct(uint32_t task, uint32_t name,
    int srdelta, uint64_t guard) {
    (void)srdelta; (void)guard;
    return mach_port_destroy(task, name);
}
kern_return_t _kernelrpc_mach_port_dnrequest_info(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_extract_member(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_extract_right(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_get_context(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_get_refs(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_get_service_port_info(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_get_set_status(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_get_srights(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_insert_member(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_insert_right(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_is_connection_for_service(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_kobject(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_kobject_description(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_mod_refs(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_move_member(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_names(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_peek(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_rename(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_request_notification(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_set_context(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_set_mscount(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_set_seqno(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_space_basic_info(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_space_info(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_special_reply_port_reset_link(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_port_type(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_task_is_self(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_allocate(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_deallocate(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_map(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_protect(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_purgable_control(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_read(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_remap(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_vm_remap_new(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_mach_voucher_extract_attr_recipe(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_task_set_port_space(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_vm_map(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_vm_read(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_vm_remap(void) { return KERN_FAILURE; }
kern_return_t _kernelrpc_vm_remap_new(void) { return KERN_FAILURE; }

/* Other unresolved symbols from libsystem_pthread */
int _os_xbs_chrooted(void) { return 0; }
void *_os_semaphore_create(void) { return (void*)0; }
