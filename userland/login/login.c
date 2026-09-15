#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <readpassphrase.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define LOGIN_PATH_MAX 1024

static const char g_path_env[] = "PATH=/bin:/sbin:/usr/bin:/usr/sbin";
static const char g_default_term_env[] = "TERM=vt100";

static void
trim_newline(char *s)
{
	size_t len;

	if (s == NULL) {
		return;
	}
	len = strlen(s);
	while (len > 0 && (s[len - 1] == '\n' || s[len - 1] == '\r')) {
		s[--len] = '\0';
	}
}

static int
ensure_current_directory(const char *home_dir)
{
	if (home_dir == NULL || home_dir[0] == '\0') {
		return chdir("/");
	}
	if (chdir(home_dir) == 0) {
		return 0;
	}
	return chdir("/");
}

static void
shell_basename(const char *shell_path, char *buf, size_t buf_size)
{
	const char *name;

	if (buf_size == 0) {
		return;
	}
	name = strrchr(shell_path, '/');
	if (name != NULL) {
		name++;
	} else {
		name = shell_path;
	}
	/*
	 * Start an interactive non-login shell for now. zsh login-shell startup
	 * currently blocks before the prompt on Panthera's minimal runtime path.
	 */
	snprintf(buf, buf_size, "%s", name);
}

static bool
password_matches(const struct passwd *pw, const char *password)
{
	char *hashed;

	if (pw == NULL || pw->pw_passwd == NULL) {
		return false;
	}
	if (pw->pw_passwd[0] == '\0') {
		return (password[0] == '\0');
	}
	if (strcmp(pw->pw_passwd, "*") == 0) {
		return false;
	}
	hashed = crypt(password, pw->pw_passwd);
	return hashed != NULL && strcmp(hashed, pw->pw_passwd) == 0;
}

static int
launch_user_shell(const struct passwd *pw)
{
	char home_env[LOGIN_PATH_MAX];
	char user_env[256];
	char logname_env[256];
	char shell_env[LOGIN_PATH_MAX];
	char term_env[256];
	char login_argv0[128];
	char *envp[7];
	char *argv[3];
	const char *term_value;

	/*
	 * Supplementary group setup is not safe yet in the Panthera minimal
	 * user database path; initgroups() can block before the shell exec.
	 * Use the primary gid until group enumeration is verified.
	 */
	if (setgid(pw->pw_gid) != 0) {
		perror("setgid");
		return 1;
	}
	if (setuid(pw->pw_uid) != 0) {
		perror("setuid");
		return 1;
	}

	umask(022);
	(void)ensure_current_directory(pw->pw_dir);

	snprintf(home_env, sizeof(home_env), "HOME=%s", pw->pw_dir);
	snprintf(user_env, sizeof(user_env), "USER=%s", pw->pw_name);
	snprintf(logname_env, sizeof(logname_env), "LOGNAME=%s", pw->pw_name);
	snprintf(shell_env, sizeof(shell_env), "SHELL=%s", pw->pw_shell);
	term_value = getenv("TERM");
	if (term_value == NULL || term_value[0] == '\0') {
		term_value = NULL;
	} else {
		snprintf(term_env, sizeof(term_env), "TERM=%s", term_value);
	}
	shell_basename(pw->pw_shell, login_argv0, sizeof(login_argv0));

	envp[0] = (char *)g_path_env;
	envp[1] = home_env;
	envp[2] = user_env;
	envp[3] = logname_env;
	envp[4] = shell_env;
	envp[5] = term_value != NULL ? term_env : (char *)g_default_term_env;
	envp[6] = NULL;

	argv[0] = login_argv0;
	argv[1] = "-i";
	argv[2] = NULL;

	execve(pw->pw_shell, argv, envp);
	perror("execve");
	return 127;
}

static void
set_hostname_from_etc(void)
{
	FILE *f;
	char buf[256];

	f = fopen("/etc/hostname", "r");
	if (f == NULL)
		return;
	if (fgets(buf, sizeof(buf), f) != NULL) {
		trim_newline(buf);
		if (buf[0] != '\0')
			sethostname(buf, strlen(buf));
	}
	fclose(f);
}

int
main(void)
{
	char username[128];
	char password[256];
	int first_prompt = 1;

	set_hostname_from_etc();

	for (;;) {
		struct passwd *pw;

		if (first_prompt) {
			/* Wait for boot messages to settle, then clear screen */
			sleep(2);
			fprintf(stdout, "\033[2J\033[H");
			if (access("/etc/motd", F_OK) == 0) {
				FILE *motd = fopen("/etc/motd", "r");
				if (motd) {
					char line[256];
					while (fgets(line, sizeof(line), motd))
						fputs(line, stdout);
					fclose(motd);
				}
			}
			fprintf(stdout, "\n");
			first_prompt = 0;
		}

		fprintf(stdout, "login: ");
		fflush(stdout);

		if (fgets(username, sizeof(username), stdin) == NULL) {
			if (feof(stdin)) {
				clearerr(stdin);
				sleep(1);
			}
			fprintf(stdout, "\n");
			continue;
		}
		trim_newline(username);
		if (username[0] == '\0') {
			continue;
		}

		pw = getpwnam(username);
		if (pw == NULL) {
			fprintf(stdout, "Login incorrect\n");
			sleep(1);
			continue;
		}

		if (readpassphrase("Password: ", password, sizeof(password), RPP_ECHO_OFF) == NULL) {
			fprintf(stdout, "\n");
			continue;
		}
		fprintf(stdout, "\n");

		if (!password_matches(pw, password)) {
			fprintf(stdout, "Login incorrect\n");
			sleep(1);
			continue;
		}

		return launch_user_shell(pw);
	}
}
