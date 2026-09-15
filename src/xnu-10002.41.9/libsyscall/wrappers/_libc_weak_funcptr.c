//
//  _libc_weak_funcptr.c
//  Libsyscall_static
//
//  Created by Ian Fang on 11/30/21.
//
//  dyld needs the following definitions to link against Libsyscall_static.
//  When building Libsyscall_dynamic, the weak symbols below will get overridden
//  by actual implementation.
//

#include "_libkernel_init.h"

__attribute__((weak))
void *
malloc(__unused size_t size)
{
	return NULL;
}

__attribute__((weak))
mach_msg_size_t
voucher_mach_msg_fill_aux(__unused mach_msg_aux_header_t *aux_hdr,
    __unused mach_msg_size_t sz)
{
	return 0;
}

__attribute__((weak))
voucher_mach_msg_state_t
voucher_mach_msg_adopt(__unused mach_msg_header_t *msg)
{
	return VOUCHER_MACH_MSG_STATE_UNCHANGED;
}

__attribute__((weak))
void
voucher_mach_msg_revert(__unused voucher_mach_msg_state_t state)
{
}

__attribute__((weak))
boolean_t
voucher_mach_msg_fill_aux_supported(void)
{
	return FALSE;
}
