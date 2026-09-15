#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <unistd.h>

static size_t
sa_advance(const struct sockaddr *sa)
{
	size_t len;

	if (sa == NULL)
		return sizeof(long);
	len = sa->sa_len;
	if (len < sizeof(long))
		len = sizeof(long);
	return (len + sizeof(long) - 1) & ~(sizeof(long) - 1);
}

static void
print_interfaces(void)
{
	struct ifaddrs *ifa_list;
	struct ifaddrs *ifa;
	char addr[INET_ADDRSTRLEN];

	if (getifaddrs(&ifa_list) != 0) {
		perror("getifaddrs");
		exit(1);
	}

	printf("Name  Address         Flags\n");
	for (ifa = ifa_list; ifa != NULL; ifa = ifa->ifa_next) {
		if (ifa->ifa_name == NULL || ifa->ifa_addr == NULL)
			continue;
		if (ifa->ifa_addr->sa_family != AF_INET)
			continue;
		if (inet_ntop(AF_INET,
		    &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr,
		    addr, sizeof(addr)) == NULL) {
			strlcpy(addr, "-", sizeof(addr));
		}
		printf("%-5s %-15s 0x%x\n", ifa->ifa_name, addr, ifa->ifa_flags);
	}

	freeifaddrs(ifa_list);
}

static void
print_routes(void)
{
	int mib[6];
	size_t needed = 0;
	char *buf;
	char *next;
	char *lim;

	mib[0] = CTL_NET;
	mib[1] = PF_ROUTE;
	mib[2] = 0;
	mib[3] = AF_INET;
	mib[4] = NET_RT_DUMP;
	mib[5] = 0;

	if (sysctl(mib, 6, NULL, &needed, NULL, 0) != 0) {
		perror("sysctl(NET_RT_DUMP)");
		return;
	}
	buf = malloc(needed);
	if (buf == NULL) {
		perror("malloc");
		return;
	}
	if (sysctl(mib, 6, buf, &needed, NULL, 0) != 0) {
		perror("sysctl(NET_RT_DUMP)");
		free(buf);
		return;
	}

	printf("Destination      Gateway          Flags\n");
	lim = buf + needed;
	for (next = buf; next < lim; ) {
		struct rt_msghdr *rtm = (struct rt_msghdr *)next;
		struct sockaddr *sa = (struct sockaddr *)(rtm + 1);
		struct sockaddr *dst = NULL;
		struct sockaddr *gw = NULL;
		char dstbuf[INET_ADDRSTRLEN] = "-";
		char gwbuf[INET_ADDRSTRLEN] = "-";
		int i;

		for (i = 0; i < RTAX_MAX; i++) {
			if ((rtm->rtm_addrs & (1 << i)) == 0)
				continue;
			if (i == RTAX_DST)
				dst = sa;
			else if (i == RTAX_GATEWAY)
				gw = sa;
			sa = (struct sockaddr *)((char *)sa + sa_advance(sa));
		}

		if (dst != NULL && dst->sa_family == AF_INET) {
			inet_ntop(AF_INET, &((struct sockaddr_in *)dst)->sin_addr,
			    dstbuf, sizeof(dstbuf));
		}
		if (gw != NULL && gw->sa_family == AF_INET) {
			inet_ntop(AF_INET, &((struct sockaddr_in *)gw)->sin_addr,
			    gwbuf, sizeof(gwbuf));
		}

		printf("%-16s %-16s 0x%x\n", dstbuf, gwbuf, rtm->rtm_flags);
		next += rtm->rtm_msglen;
	}

	free(buf);
}

int
main(int argc, char **argv)
{
	if (argc == 1) {
		print_interfaces();
		return 0;
	}
	if (argc == 2 && strcmp(argv[1], "-rn") == 0) {
		print_routes();
		return 0;
	}

	fprintf(stderr, "usage: netstat [-rn]\n");
	return 1;
}
