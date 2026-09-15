/*
 * panthera_boot.c — Panthera-specific boot infrastructure for launchd.
 *
 * This file contains boot-time operations that Apple's launchd doesn't
 * perform but are required for Panthera:
 *   - Console setup (open /dev/console for stdio)
 *   - Remount root filesystem read-write
 *   - Fix dyld shared cache ownership and flags
 *   - Fix SUID permissions on key binaries
 *   - Set hostname from /etc/hostname
 *
 * Called from launchd_runtime() before entering the event loop.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Low-level write that works before stdio is ready */
static void
panthera_boot_puts(const char *s)
{
	size_t len = 0;
	while (s[len] != '\0')
		len++;
	if (write(2, s, len) < 0) {
		int fd = open("/dev/console", O_WRONLY | O_NOCTTY);
		if (fd >= 0) {
			write(fd, s, len);
			close(fd);
		}
	}
}

/*
 * Open /dev/console as stdin/stdout/stderr for PID 1.
 */
static void
panthera_console_setup(void)
{
	int fd = open("/dev/console", O_RDWR | O_NOCTTY, 0);
	if (fd < 0)
		return;
	if (fd != 0)
		dup2(fd, 0);
	dup2(0, 1);
	dup2(0, 2);
	if (fd > 2)
		close(fd);
}

/*
 * Remount / read-write. The kernel mounts root read-only at boot.
 */
static void
panthera_ensure_boot_dirs(void)
{
	mkdir("/tmp", 0777);
	mkdir("/var/tmp", 0777);
	mkdir("/var/run", 0755);
	mkdir("/var/log", 0755);
	mkdir("/var/db", 0755);
	mkdir("/var/db/launchd.db", 0755);
	mkdir("/var/db/launchd.db/com.apple.launchd", 0755);
}

static int
panthera_remount_hfs_root_rw(void)
{
	struct {
		char *fspec;
		int32_t hfs_uid;
		int32_t hfs_gid;
		int16_t hfs_mask;
		int16_t _pad;
		int32_t hfs_encoding;
		int32_t tz_minuteswest;
		int32_t tz_dsttime;
		int32_t flags;
		int32_t journal_tbuffer_size;
		int32_t journal_flags;
		int32_t journal_disable;
	} args;

	memset(&args, 0, sizeof(args));

	return mount("hfs", "/", MNT_UPDATE, &args);
}

static int
panthera_remount_zfs_root_rw(const char *mounted_from)
{
	struct {
		const char *fspec;
		int mflag;
		const char *optptr;
		int optlen;
		int struct_size;
	} args;
	static const char optstr[] = "remount";

	memset(&args, 0, sizeof(args));
	args.fspec = mounted_from;
	args.mflag = MNT_UPDATE;
	args.optptr = optstr;
	args.optlen = sizeof(optstr);
	args.struct_size = sizeof(args);

	return mount("zfs", "/", MNT_UPDATE, &args);
}

static void
panthera_remount_root_rw(void)
{
	struct statfs sfs;
	int rc = -1;

	memset(&sfs, 0, sizeof(sfs));
	if (statfs("/", &sfs) != 0) {
		panthera_boot_puts("launchd: root statfs failed before remount\n");
		return;
	}

	if (strcmp(sfs.f_fstypename, "hfs") == 0) {
		rc = panthera_remount_hfs_root_rw();
	} else if (strcmp(sfs.f_fstypename, "zfs") == 0) {
		rc = panthera_remount_zfs_root_rw(sfs.f_mntfromname);
	} else {
		panthera_boot_puts("launchd: unknown root filesystem for rw remount\n");
		return;
	}

	if (rc == 0)
		panthera_ensure_boot_dirs();
	else
		panthera_boot_puts("launchd: root remount rw failed (continuing read-only)\n");
}

/*
 * Fix dyld shared cache ownership (uid 0, gid 0) and SF_RESTRICTED flag.
 * The shared_region_map_and_slide_2_np syscall requires this.
 */
static void
panthera_fix_shared_cache_ownership(void)
{
	static const char *cache_path =
	    "/System/Library/dyld/dyld_shared_cache_x86_64";
	struct stat sb;

	if (stat(cache_path, &sb) != 0)
		return;

	if (sb.st_uid != 0 || sb.st_gid != 0)
		(void)chown(cache_path, 0, 0);

	if (!(sb.st_flags & SF_RESTRICTED))
		(void)chflags(cache_path, sb.st_flags | SF_RESTRICTED);
}

/*
 * Fix SUID permissions on key binaries.
 */
static void
panthera_fix_suid_permissions(void)
{
	static const char *ping_path = "/sbin/ping";
	struct stat sb;

	if (stat(ping_path, &sb) != 0)
		return;

	if (sb.st_uid != 0 || sb.st_gid != 0)
		(void)chown(ping_path, 0, 0);

	if ((sb.st_mode & 04755) != 04555)
		(void)chmod(ping_path, 04555);
}

/*
 * Set hostname from /etc/hostname.
 */
static void
panthera_set_hostname(void)
{
	int fd = open("/etc/hostname", O_RDONLY, 0);
	if (fd < 0)
		return;

	char buf[256];
	ssize_t len = read(fd, buf, sizeof(buf) - 1);
	close(fd);

	if (len > 0) {
		while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
			len--;
		buf[len] = '\0';
		sethostname(buf, (int)len);
	}
}

static int
panthera_exec_wait(char *const argv[])
{
	pid_t pid;
	int status;

	pid = fork();
	if (pid < 0)
		return (-1);
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}

	for (;;) {
		if (waitpid(pid, &status, 0) >= 0)
			break;
		if (errno != EINTR)
			return (-1);
	}

	if (WIFEXITED(status))
		return (WEXITSTATUS(status));
	if (WIFSIGNALED(status))
		return (128 + WTERMSIG(status));
	return (1);
}

/*
 * Import and mount ZFS-backed state datasets before launchd imports and
 * dispatches normal daemon jobs. This is opt-in through the staged config.
 */
static void
panthera_mount_zfs_hybrid_state(void)
{
	static const char *config = "/etc/panthera/zfs-hybrid.conf";
	static const char *helper = "/sbin/panthera_zfs_hybrid_mount";
	char *const argv[] = {
		(char *)helper,
		(char *)config,
		NULL
	};
	int rc;
	char msg[96];

	if (access(config, R_OK) != 0)
		return;

	if (access(helper, X_OK) != 0) {
		panthera_boot_puts("launchd: ZFS hybrid config present but helper missing\n");
		return;
	}

	panthera_boot_puts("launchd: mounting ZFS hybrid state\n");
	rc = panthera_exec_wait(argv);
	if (rc != 0) {
		snprintf(msg, sizeof(msg),
		    "launchd: ZFS hybrid state mount failed rc=%d\n", rc);
		panthera_boot_puts(msg);
	} else {
		panthera_boot_puts("launchd: ZFS hybrid state mounted\n");
	}
}

/*
 * panthera_boot_init — master entry point for all Panthera boot operations.
 * Called from main() before any other initialization.
 */
void
panthera_boot_init(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = SIG_IGN;
	sa.sa_flags = 0;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGUSR1, &sa, NULL);

	panthera_console_setup();
	if (getenv("PANTHERA_LAUNCHD_TRACE") != NULL) {
		panthera_boot_puts("*** launchd: Panthera boot init ***\n");
	}
	panthera_remount_root_rw();
	panthera_fix_shared_cache_ownership();
	panthera_mount_zfs_hybrid_state();
	panthera_fix_suid_permissions();
	panthera_set_hostname();
	if (getenv("PANTHERA_LAUNCHD_TRACE") != NULL) {
		panthera_boot_puts("launchd: boot init complete\n");
	}
}

/*
 * panthera_load_jobs — scan /System/Library/LaunchDaemons and load plists.
 *
 * On real macOS, launchctl does this. On Panthera, we load directly
 * using a minimal XML plist parser (no CoreFoundation dependency) and
 * job_import() from core.c.
 *
 * Called from main() after jobmgr_init().
 */

#include <dirent.h>
#include <stdlib.h>

/* From core.h */
typedef struct job_s *job_t;
extern job_t job_import(void *pload);
extern job_t panthera_job_import_deferred(void *pload);
extern void *job_find(void *jm, const char *label);

/* From launch.h — launch_data API */
typedef void *launch_data_t;
extern launch_data_t launch_data_alloc(int type);
extern launch_data_t launch_data_new_string(const char *);
extern launch_data_t launch_data_new_bool(int);
extern launch_data_t launch_data_new_integer(long long);
extern void launch_data_dict_insert(launch_data_t, launch_data_t, const char *);
extern launch_data_t launch_data_array_set_index(launch_data_t, launch_data_t, size_t);
#define LAUNCH_DATA_DICTIONARY 1
#define LAUNCH_DATA_ARRAY 2

/*
 * Minimal XML plist parser — extracts key/value pairs from the top-level
 * <dict> element. Handles <string>, <true/>, <false/>, <integer>, <array>.
 * No external dependencies.
 */

static const char *skip_ws(const char *p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
	return p;
}

static const char *find_tag_end(const char *p) {
	while (*p && *p != '>') p++;
	return *p ? p + 1 : p;
}

static const char *extract_text(const char *start, const char *end, char *buf, size_t bufsz) {
	size_t len = (size_t)(end - start);
	if (len >= bufsz) len = bufsz - 1;
	memcpy(buf, start, len);
	buf[len] = '\0';
	/* Unescape basic XML entities */
	char *r = buf, *w = buf;
	while (*r) {
		if (*r == '&') {
			if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; }
			else if (strncmp(r, "&lt;", 4) == 0) { *w++ = '<'; r += 4; }
			else if (strncmp(r, "&gt;", 4) == 0) { *w++ = '>'; r += 4; }
			else { *w++ = *r++; }
		} else {
			*w++ = *r++;
		}
	}
	*w = '\0';
	return buf;
}

/* Forward declaration */
static launch_data_t parse_plist_value(const char **pp);

static launch_data_t parse_plist_array(const char **pp) {
	launch_data_t arr = launch_data_alloc(LAUNCH_DATA_ARRAY);
	size_t idx = 0;
	const char *p = *pp;

	while (*p) {
		p = skip_ws(p);
		if (strncmp(p, "</array>", 8) == 0) {
			*pp = p + 8;
			return arr;
		}
		launch_data_t val = parse_plist_value(&p);
		if (val)
			launch_data_array_set_index(arr, val, idx++);
	}
	*pp = p;
	return arr;
}

static launch_data_t parse_plist_dict(const char **pp) {
	launch_data_t dict = launch_data_alloc(LAUNCH_DATA_DICTIONARY);
	const char *p = *pp;
	char keybuf[256];

	while (*p) {
		p = skip_ws(p);
		if (strncmp(p, "</dict>", 7) == 0) {
			*pp = p + 7;
			return dict;
		}

		/* Expect <key>...</key> */
		if (strncmp(p, "<key>", 5) != 0) {
			p = find_tag_end(p);
			continue;
		}
		p += 5;
		const char *kend = strstr(p, "</key>");
		if (!kend) break;
		extract_text(p, kend, keybuf, sizeof(keybuf));
		p = kend + 6;

		/* Parse value */
		p = skip_ws(p);
		launch_data_t val = parse_plist_value(&p);
		if (val)
			launch_data_dict_insert(dict, val, keybuf);
	}
	*pp = p;
	return dict;
}

static launch_data_t parse_plist_value(const char **pp) {
	const char *p = skip_ws(*pp);

	if (strncmp(p, "<string>", 8) == 0) {
		p += 8;
		const char *end = strstr(p, "</string>");
		if (!end) { *pp = p; return NULL; }
		char buf[4096];
		extract_text(p, end, buf, sizeof(buf));
		*pp = end + 9;
		return launch_data_new_string(buf);
	} else if (strncmp(p, "<true/>", 7) == 0) {
		*pp = p + 7;
		return launch_data_new_bool(1);
	} else if (strncmp(p, "<false/>", 8) == 0) {
		*pp = p + 8;
		return launch_data_new_bool(0);
	} else if (strncmp(p, "<integer>", 9) == 0) {
		p += 9;
		const char *end = strstr(p, "</integer>");
		if (!end) { *pp = p; return NULL; }
		char buf[64];
		extract_text(p, end, buf, sizeof(buf));
		*pp = end + 10;
		return launch_data_new_integer(strtoll(buf, NULL, 10));
	} else if (strncmp(p, "<array>", 7) == 0) {
		p += 7;
		*pp = p;
		return parse_plist_array(pp);
	} else if (strncmp(p, "<array/>", 8) == 0) {
		*pp = p + 8;
		return launch_data_alloc(LAUNCH_DATA_ARRAY);
	} else if (strncmp(p, "<dict>", 6) == 0) {
		p += 6;
		*pp = p;
		return parse_plist_dict(pp);
	} else if (strncmp(p, "<dict/>", 7) == 0) {
		*pp = p + 7;
		return launch_data_alloc(LAUNCH_DATA_DICTIONARY);
	} else {
		/* Skip unknown tags */
		*pp = find_tag_end(p);
		return NULL;
	}
}

void
panthera_load_jobs(void)
{
	static const char *daemon_dir = "/System/Library/LaunchDaemons";
	DIR *dp = opendir(daemon_dir);
	if (!dp) {
		panthera_boot_puts("launchd: no LaunchDaemons directory\n");
		return;
	}

	int loaded = 0;
	struct dirent *ent;
	while ((ent = readdir(dp)) != NULL) {
		size_t nlen = strlen(ent->d_name);
		if (nlen < 7 || strcmp(ent->d_name + nlen - 6, ".plist") != 0)
			continue;

		char path[1024];
		snprintf(path, sizeof(path), "%s/%s", daemon_dir, ent->d_name);

		int fd = open(path, O_RDONLY);
		if (fd < 0)
			continue;

		struct stat sb;
		if (fstat(fd, &sb) != 0 || sb.st_size < 10 || sb.st_size > 1048576) {
			close(fd);
			continue;
		}

		char *buf = malloc((size_t)sb.st_size + 1);
		if (!buf) {
			close(fd);
			continue;
		}

		ssize_t rd = read(fd, buf, (size_t)sb.st_size);
		close(fd);
		if (rd != sb.st_size) {
			free(buf);
			continue;
		}
		buf[sb.st_size] = '\0';

		/* Find <dict> at top level */
		const char *p = strstr(buf, "<dict>");
		if (!p) {
			free(buf);
			continue;
		}
		p += 6;

		launch_data_t dict = parse_plist_dict(&p);
		free(buf);

		if (!dict)
			continue;

		if (getenv("PANTHERA_LAUNCHD_TRACE") != NULL) {
			char import_msg[256];
			snprintf(import_msg, sizeof(import_msg),
			    "PANTHERA:launchd import plist=%s\n", ent->d_name);
			panthera_boot_puts(import_msg);
		}

		job_t j = panthera_job_import_deferred(dict);
		if (j) {
			loaded++;
		} else {
			char fail_msg[256];
			snprintf(fail_msg, sizeof(fail_msg),
			    "PANTHERA:launchd import failed plist=%s\n", ent->d_name);
			panthera_boot_puts(fail_msg);
		}
	}

	closedir(dp);

	char msg[80];
	snprintf(msg, sizeof(msg), "launchd: loaded %d jobs from plists\n", loaded);
	panthera_boot_puts(msg);
}
