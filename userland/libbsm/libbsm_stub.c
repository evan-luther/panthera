#include <bsm/audit.h>
#include <errno.h>
#include <mach/message.h>
#include <string.h>
#include <unistd.h>

int
audit(const void *record, int length)
{
	(void)record;
	(void)length;
	errno = ENOSYS;
	return -1;
}

int
auditon(int cmd, void *data, int length)
{
	(void)cmd;
	(void)data;
	(void)length;
	errno = ENOSYS;
	return -1;
}

int
auditctl(const char *path)
{
	(void)path;
	errno = ENOSYS;
	return -1;
}

int
getauid(au_id_t *auid)
{
	if (!auid) {
		errno = EINVAL;
		return -1;
	}
	*auid = 0;
	return 0;
}

int
setauid(const au_id_t *auid)
{
	if (!auid) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

int
getaudit(struct auditinfo *info)
{
	if (!info) {
		errno = EINVAL;
		return -1;
	}
	memset(info, 0, sizeof(*info));
	return 0;
}

int
setaudit(const struct auditinfo *info)
{
	if (!info) {
		errno = EINVAL;
		return -1;
	}
	return 0;
}

void
audit_token_to_au32(
	audit_token_t atoken,
	uid_t *auid,
	uid_t *euid,
	gid_t *egid,
	uid_t *ruid,
	gid_t *rgid,
	pid_t *pid,
	au_asid_t *asid,
	au_tid_t *tid)
{
	if (auid) {
		*auid = atoken.val[0];
	}
	if (euid) {
		*euid = atoken.val[1];
	}
	if (egid) {
		*egid = atoken.val[2];
	}
	if (ruid) {
		*ruid = atoken.val[3];
	}
	if (rgid) {
		*rgid = atoken.val[4];
	}
	if (pid) {
		*pid = (pid_t)atoken.val[5];
	}
	if (asid) {
		*asid = (au_asid_t)atoken.val[6];
	}
	if (tid) {
		tid->port = (dev_t)atoken.val[7];
		tid->machine = (u_int32_t)atoken.val[7];
	}
}
