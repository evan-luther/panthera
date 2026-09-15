#include <mach/mach.h>

boolean_t
voucher_mach_msg_set(mach_msg_header_t *msg)
{
	(void)msg;
	return FALSE;
}

void
voucher_mach_msg_clear(mach_msg_header_t *msg)
{
	(void)msg;
}
