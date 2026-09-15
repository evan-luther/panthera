#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach/mach.h>
#include <mach/ndr.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

extern mach_port_t bootstrap_port;
extern kern_return_t bootstrap_look_up(mach_port_t bp, const char *service_name,
    mach_port_t *sp);

#define PANTHERA_IPCONFIG_SERVER "com.apple.network.IPConfiguration"
#define PANTHERA_IPCONFIG_IF_COUNT_MSG_ID 20000
#define PANTHERA_IPCONFIG_IF_COUNT_REPLY_ID 20100
#define PANTHERA_IPCONFIG_CONTROL_ATTEMPTS 20
#define PANTHERA_IPCONFIG_CONTROL_RPC_TIMEOUT_MS 2500

static const char *
base_name(const char *path)
{
	const char *slash;

	if (path == NULL)
		return "";
	slash = strrchr(path, '/');
	return slash == NULL ? path : slash + 1;
}

static kern_return_t
ipconfig_control_if_count(mach_port_t server, int *count)
{
	typedef struct {
		mach_msg_header_t head;
	} Request;
	typedef struct {
		mach_msg_header_t head;
		NDR_record_t ndr;
		kern_return_t retcode;
		int count;
		mach_msg_trailer_t trailer;
	} Reply;
	union {
		Request request;
		Reply reply;
	} message;
	mach_port_t reply_port = MACH_PORT_NULL;
	kern_return_t kr;
	mach_msg_return_t mr;

	memset(&message, 0, sizeof(message));
	kr = mach_port_allocate(mach_task_self(), MACH_PORT_RIGHT_RECEIVE,
	    &reply_port);
	if (kr != KERN_SUCCESS)
		return kr;

	message.request.head.msgh_bits =
	    MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, MACH_MSG_TYPE_MAKE_SEND_ONCE);
	message.request.head.msgh_size = (mach_msg_size_t)sizeof(Request);
	message.request.head.msgh_remote_port = server;
	message.request.head.msgh_local_port = reply_port;
	message.request.head.msgh_id = PANTHERA_IPCONFIG_IF_COUNT_MSG_ID;

	mr = mach_msg(&message.request.head,
	    MACH_SEND_MSG | MACH_RCV_MSG | MACH_SEND_TIMEOUT | MACH_RCV_TIMEOUT,
	    (mach_msg_size_t)sizeof(Request), (mach_msg_size_t)sizeof(Reply),
	    reply_port, PANTHERA_IPCONFIG_CONTROL_RPC_TIMEOUT_MS, MACH_PORT_NULL);
	if (mr != MACH_MSG_SUCCESS) {
		(void)mach_port_deallocate(mach_task_self(), reply_port);
		return mr;
	}
	if (message.reply.head.msgh_id != PANTHERA_IPCONFIG_IF_COUNT_REPLY_ID)
		return MIG_REPLY_MISMATCH;
	if (message.reply.retcode != KERN_SUCCESS)
		return message.reply.retcode;

	*count = message.reply.count;
	return KERN_SUCCESS;
}

static void
mark_check_stage(const char *name, const char *stage)
{
	char line[192];
	int n;

	n = snprintf(line, sizeof(line), "PANTHERA_BOOT_VERIFY_STAGE:%s:%s\n",
	    name, stage);
	if (n <= 0 || (size_t)n >= sizeof(line))
		return;
	(void)write(STDOUT_FILENO, line, (size_t)n);
}

static int
run_check(const char *name, char *const argv[], int timeout_seconds)
{
	pid_t pid;
	int status = 127;
	int rc = 127;
	struct timeval start;
	struct timeval now;
	long deadline_ms;

	printf("PANTHERA_BOOT_VERIFY_CMD:%s\n", name);
	fflush(stdout);
	mark_check_stage(name, "before_fork");

	pid = fork();
	if (pid == 0) {
		mark_check_stage(name, "child_before_exec");
		if (strcmp(name, "dispatch_timer") == 0 &&
		    getenv("PANTHERA_BOOT_VERIFY_DISPATCH_TIMER_AVOID_SHARED_REGION") != NULL) {
			setenv("DYLD_SHARED_REGION", "avoid", 1);
		}
		execv(argv[0], argv);
		mark_check_stage(name, "child_exec_failed");
		fprintf(stderr, "PANTHERA_BOOT_VERIFY_EXEC_ERROR:%s:%s\n",
		    name, strerror(errno));
		_exit(127);
	}
	if (pid < 0) {
		mark_check_stage(name, "fork_failed");
		fprintf(stderr, "PANTHERA_BOOT_VERIFY_FORK_ERROR:%s:%s\n",
		    name, strerror(errno));
		printf("__PANTHERA_STATUS_%s:127__\n", name);
		fflush(stdout);
		return 127;
	}
	mark_check_stage(name, "after_fork_parent");

	if (gettimeofday(&start, NULL) != 0) {
		fprintf(stderr, "PANTHERA_BOOT_VERIFY_TIME_ERROR:%s:%s\n",
		    name, strerror(errno));
		start.tv_sec = 0;
		start.tv_usec = 0;
	}

	if (timeout_seconds <= 0) {
		pid_t waited = waitpid(pid, &status, 0);

		if (waited == pid) {
			mark_check_stage(name, "child_reaped");
			if (WIFEXITED(status)) {
				rc = WEXITSTATUS(status);
			} else if (WIFSIGNALED(status)) {
				rc = WTERMSIG(status) == SIGKILL ? 124 :
				    128 + WTERMSIG(status);
			}
		} else {
			fprintf(stderr, "PANTHERA_BOOT_VERIFY_WAIT_ERROR:%s:%s\n",
			    name, strerror(errno));
			rc = 127;
		}
		goto status_out;
	}

	deadline_ms = ((long)start.tv_sec * 1000L) + ((long)start.tv_usec / 1000L) +
	    ((long)timeout_seconds * 1000L);

	for (;;) {
		pid_t waited;
		struct timeval nap;
		long now_ms;

			waited = waitpid(pid, &status, WNOHANG);
			if (waited == pid) {
				mark_check_stage(name, "child_reaped");
				if (WIFEXITED(status)) {
					rc = WEXITSTATUS(status);
				} else if (WIFSIGNALED(status)) {
				rc = WTERMSIG(status) == SIGKILL ? 124 : 128 + WTERMSIG(status);
			}
			break;
		}
		if (waited < 0) {
			fprintf(stderr, "PANTHERA_BOOT_VERIFY_WAIT_ERROR:%s:%s\n",
			    name, strerror(errno));
			rc = 127;
			break;
		}

		if (gettimeofday(&now, NULL) != 0) {
			fprintf(stderr, "PANTHERA_BOOT_VERIFY_TIME_ERROR:%s:%s\n",
			    name, strerror(errno));
			rc = 127;
			break;
		}
		now_ms = ((long)now.tv_sec * 1000L) + ((long)now.tv_usec / 1000L);
			if (now_ms >= deadline_ms) {
				mark_check_stage(name, "timeout");
				kill(pid, SIGKILL);
				(void)waitpid(pid, &status, WNOHANG);
				rc = 124;
			break;
		}

			if (timeout_seconds > 8) {
				nap.tv_sec = 0;
				nap.tv_usec = 100000;
				(void)select(0, NULL, NULL, NULL, &nap);
			}
		}
	if (rc == 124)
		fprintf(stderr, "PANTHERA_BOOT_VERIFY_TIMEOUT:%s\n", name);

status_out:
	mark_check_stage(name, "before_status");
	printf("__PANTHERA_STATUS_%s:%d__\n", name, rc);
	fflush(stdout);
	mark_check_stage(name, "after_status");
	return rc;
}

static int
run_identity_check(void)
{
	int rc;

	printf("PANTHERA_BOOT_VERIFY_CMD:id\n");
	printf("uid=%d(root) gid=%d\n", (int)getuid(), (int)getgid());
	rc = getuid() == 0 ? 0 : 1;
	printf("__PANTHERA_STATUS_id:%d__\n", rc);
	fflush(stdout);
	return rc;
}

#ifndef PANTHERA_BOOT_VERIFY_RUN_NETWORK
static void
mark_skipped_check(const char *name, const char *reason)
{
	printf("PANTHERA_BOOT_VERIFY_CMD:%s\n", name);
	printf("PANTHERA_BOOT_VERIFY_EXPECTED_SKIP:%s:%s\n", name, reason);
	printf("__PANTHERA_STATUS_%s:99__\n", name);
	fflush(stdout);
}
#endif

static int
run_daemon_network_checks(void)
{
	char *ifconfig_argv[] = {
		"/sbin/netprobe_step", "en0", "verify_addr_up", "10.0.2.15",
		NULL
	};
	char *netstat_argv[] = { "/sbin/netstat", "-rn", NULL };
	char *sc_probe_argv[] = { "/usr/bin/sc_dynamic_store_probe", NULL };
	char *ping_argv[] = { "/sbin/ping", "-c", "1", "10.0.2.2", NULL };
	char *dnsprobe_argv[] = {
		"/sbin/dnsprobe", "localhost", "10.0.2.3", NULL
	};
	char *ipconfig_control_argv[] = {
		"/usr/bin/boot_verify_ipconfig_control", NULL
	};
	int failed = 0;

	printf("PANTHERA_BOOT_VERIFY_DAEMON_NETWORK_BEGIN\n");
	fflush(stdout);
	if (run_check("ipconfiguration_daemon_ifconfig", ifconfig_argv, 30) != 0)
		failed = 1;
	setenv("PANTHERA_SC_PROBE_NETWORK", "1", 1);
	if (run_check("ipconfiguration_daemon_sc_probe", sc_probe_argv, 30) != 0)
		failed = 1;
	if (run_check("ipconfiguration_daemon_gateway_ping", ping_argv, 45) != 0)
		failed = 1;
	if (run_check("ipconfiguration_daemon_route", netstat_argv, 30) != 0)
		failed = 1;
	if (run_check("ipconfiguration_daemon_dns_tcp", dnsprobe_argv, 45) != 0)
		failed = 1;
	if (run_check("ipconfiguration_control", ipconfig_control_argv, 120) != 0)
		failed = 1;
	printf("PANTHERA_BOOT_VERIFY_DAEMON_NETWORK_END\n");
	printf("__PANTHERA_STATUS_ipconfiguration_daemon_network:%d__\n", failed);
	fflush(stdout);
	return failed;
}

#define PANTHERA_DAEMON_NETWORK_MAX_ATTEMPTS 60
#define PANTHERA_DAEMON_NETWORK_RETRY_DELAY_SEC 3

static int
run_daemon_network_retry_worker(void)
{
	int rc = 1;

	for (int attempt = 1; attempt <= PANTHERA_DAEMON_NETWORK_MAX_ATTEMPTS;
	    attempt++) {
		rc = run_daemon_network_checks();
		if (rc == 0)
			return 0;
		if (attempt < PANTHERA_DAEMON_NETWORK_MAX_ATTEMPTS)
			sleep(PANTHERA_DAEMON_NETWORK_RETRY_DELAY_SEC);
	}
	return rc;
}

static int
run_ipconfig_control_check(void)
{
	kern_return_t last_kr = KERN_FAILURE;

	printf("PANTHERA_IPCONFIG_CONTROL_BEGIN\n");
	fflush(stdout);

	for (int attempt = 1; attempt <= PANTHERA_IPCONFIG_CONTROL_ATTEMPTS;
	    attempt++) {
		mach_port_t server = MACH_PORT_NULL;
		kern_return_t kr;
		int count = -1;

		kr = bootstrap_look_up(bootstrap_port, PANTHERA_IPCONFIG_SERVER,
		    &server);
		printf("PANTHERA_IPCONFIG_CONTROL:lookup attempt=%d kr=%d port=%u\n",
		    attempt, kr, (unsigned int)server);
		fflush(stdout);
		if (kr != KERN_SUCCESS) {
			last_kr = kr;
			sleep(1);
			continue;
		}

		kr = ipconfig_control_if_count(server, &count);
		printf("PANTHERA_IPCONFIG_CONTROL:if_count attempt=%d kr=%d count=%d\n",
		    attempt, kr, count);
		fflush(stdout);
		if (server != MACH_PORT_NULL)
			(void)mach_port_deallocate(mach_task_self(), server);
		if (kr == KERN_SUCCESS && count >= 1) {
			printf("PANTHERA_IPCONFIG_CONTROL_END\n");
			fflush(stdout);
			return 0;
		}
		last_kr = kr;
		sleep(1);
	}

	printf("PANTHERA_IPCONFIG_CONTROL_END\n");
	fflush(stdout);
	return last_kr == KERN_SUCCESS ? 1 : 2;
}

int
main(int argc, char **argv)
{
	char *echo_argv[] = { "/bin/echo", "PANTHERA_BOOT_VERIFY_EXEC", NULL };
	#ifdef PANTHERA_BOOT_VERIFY_RUN_NETWORK
		char *net_flags_argv[] = {
			"/sbin/netprobe_step", "en0", "flags", NULL
		};
		char *net_addr_argv[] = {
			"/sbin/netprobe_step", "en0", "addr", "10.0.2.15", NULL
		};
		char *ifconfig_argv[] = {
			"/sbin/netprobe_step", "en0", "verify_addr", "10.0.2.15",
			NULL
		};
		char *net_eflags_argv[] = {
			"/sbin/netprobe_step", "en0", "eflags", NULL
		};
		char *net_route_argv[] = {
			"/sbin/netprobe_step", "en0", "route", "10.0.2.2", NULL
		};
		char *net_arp_argv[] = {
			"/sbin/netprobe_step", "en0", "arp_on", NULL
		};
		char *net_proto_argv[] = {
			"/sbin/netprobe_step", "en0", "proto_attach", NULL
		};
		char *net_addr_finalize_argv[] = {
			"/sbin/netprobe_step", "en0", "addr", "10.0.2.15", NULL
		};
		char *net_static_arp_argv[] = {
			"/sbin/netprobe_step", "en0", "arp", "10.0.2.2",
			"52:56:00:00:00:02", NULL
		};
		char *netstat_argv[] = { "/sbin/netstat", "-rn", NULL };
		char *ping_argv[] = { "/sbin/ping", "-c", "1", "10.0.2.2", NULL };
		char *netstat_after_ping_argv[] = { "/sbin/netstat", "-rn", NULL };
		char *dnsprobe_argv[] = {
			"/sbin/dnsprobe", "example.com", "10.0.2.3", NULL
		};
#endif
	char *bootstrap_argv[] = { "/usr/bin/test_bootstrap_simple", NULL };
	char *cf_dictionary_argv[] = { "/usr/bin/test_cf", NULL };
	char *dispatch_timer_argv[] = { "/usr/bin/test_dispatch_timer", NULL };
	const char *dispatch_timer_env;
	const char *daemon_network_env;
	const char *prog;

	dispatch_timer_env = getenv("PANTHERA_BOOT_VERIFY_RUN_DISPATCH_TIMER");
	daemon_network_env = getenv("PANTHERA_BOOT_VERIFY_RUN_DAEMON_NETWORK");
	prog = base_name(argc > 0 ? argv[0] : NULL);

	if (strcmp(prog, "boot_verify_daemon_network") == 0)
		return run_daemon_network_checks();
	if (strcmp(prog, "boot_verify_ipconfig_control") == 0)
		return run_ipconfig_control_check();
	if (argc == 2 && strcmp(argv[1], "--daemon-network") == 0)
		return run_daemon_network_checks();
	if (argc == 2 && strcmp(argv[1], "--ipconfig-control") == 0)
		return run_ipconfig_control_check();
	if (argc == 2 && strcmp(argv[1], "--dispatch-timer") == 0)
		return run_check("dispatch_timer", dispatch_timer_argv, 10);
	if (argc == 2 && (strcmp(argv[1], "--daemon-network-worker") == 0 ||
	    strcmp(argv[1], "--daemon-network-retry") == 0))
		return run_daemon_network_retry_worker();
	printf("PANTHERA_BOOT_VERIFY_GUEST_BEGIN\n");
	fflush(stdout);

	run_check("external_command", echo_argv, 30);
	run_identity_check();
	run_check("cf_dictionary", cf_dictionary_argv, 30);
	run_check("bootstrap", bootstrap_argv, 30);
	if (dispatch_timer_env != NULL && strcmp(dispatch_timer_env, "1") == 0)
		run_check("dispatch_timer", dispatch_timer_argv, 10);
		#ifdef PANTHERA_BOOT_VERIFY_RUN_NETWORK
			printf("PANTHERA_BOOT_VERIFY_NETWORK_PROBE_BEGIN\n");
			fflush(stdout);
			run_check("net_flags", net_flags_argv, 0);
			run_check("netbringup", net_addr_argv, 0);
			run_check("ifconfig", ifconfig_argv, 30);
			run_check("net_eflags", net_eflags_argv, 30);
			run_check("net_route", net_route_argv, 45);
			run_check("net_arp", net_arp_argv, 45);
			run_check("net_proto", net_proto_argv, 45);
			run_check("net_addr_finalize", net_addr_finalize_argv, 45);
			run_check("net_static_arp", net_static_arp_argv, 45);
			run_check("netstat", netstat_argv, 30);
			run_check("gateway_ping", ping_argv, 45);
			run_check("netstat_after_ping", netstat_after_ping_argv, 30);
			run_check("dns_tcp", dnsprobe_argv, 45);
#else
	mark_skipped_check("netbringup", "network_ioctl_path_not_bounded");
	mark_skipped_check("net_route", "network_ioctl_path_not_bounded");
	mark_skipped_check("ifconfig", "network_ioctl_path_not_bounded");
	mark_skipped_check("gateway_ping", "network_ioctl_path_not_bounded");
	mark_skipped_check("dns_tcp", "network_ioctl_path_not_bounded");
#endif

	if (daemon_network_env != NULL && strcmp(daemon_network_env, "1") == 0)
		run_daemon_network_retry_worker();
	printf("PANTHERA_BOOT_VERIFY_GUEST_END\n");
	fflush(stdout);
	return 0;
}
