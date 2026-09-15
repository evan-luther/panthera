#include "internal.h"

void
_os_workgroup_tsd_cleanup(void *ctxt)
{
	(void)ctxt;
}

void
_workgroup_init(void)
{
}

int
os_workgroup_join(os_workgroup_t wg, os_workgroup_join_token_t token_out)
{
	(void)wg;
	if (token_out) {
		bzero(token_out, sizeof(*token_out));
	}
	return 0;
}

void
os_workgroup_leave(os_workgroup_t wg, os_workgroup_join_token_t token)
{
	(void)wg;
	(void)token;
}
