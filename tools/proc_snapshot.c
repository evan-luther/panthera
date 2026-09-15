#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/proc_info.h>
#include <unistd.h>

extern int proc_listpids(uint32_t type, uint32_t typeinfo, void *buffer, int buffersize);
extern int proc_pidinfo(int pid, int flavor, uint64_t arg, void *buffer, int buffersize);

int
main(void)
{
	int pids[512];
	int bytes = proc_listpids(PROC_ALL_PIDS, 0, pids, sizeof(pids));
	int count, i;

	if (bytes < 0) {
		printf("proc_snapshot: proc_listpids failed errno=%d (%s)\n",
		    errno, strerror(errno));
		return 1;
	}
	count = bytes / (int)sizeof(pids[0]);
	printf("proc_snapshot: count=%d\n", count);
	printf("pid ppid pgid status flags uid gid comm\n");
	for (i = 0; i < count; i++) {
		struct proc_bsdshortinfo info;
		int pid = pids[i];

		if (pid <= 0)
			continue;
		memset(&info, 0, sizeof(info));
		if (proc_pidinfo(pid, PROC_PIDT_SHORTBSDINFO, 0,
		    &info, sizeof(info)) != (int)sizeof(info)) {
			printf("%d ? ? ? ? ? ? <proc_pidinfo errno=%d>\n",
			    pid, errno);
			continue;
		}
		printf("%u %u %u %u 0x%x %u %u %s\n",
		    info.pbsi_pid, info.pbsi_ppid, info.pbsi_pgid,
		    info.pbsi_status, info.pbsi_flags, info.pbsi_uid,
		    info.pbsi_gid, info.pbsi_comm);
	}
	return 0;
}
