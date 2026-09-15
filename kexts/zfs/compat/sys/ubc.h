/*
 * Keep OpenZFS SPL's Solaris-style proc_t macro out of XNU UBC declarations.
 */
#ifndef PANTHERA_ZFS_COMPAT_SYS_UBC_H
#define PANTHERA_ZFS_COMPAT_SYS_UBC_H

#pragma push_macro("proc_t")
#undef proc_t
#define proc_t struct proc *
#include_next <sys/ubc.h>
#if defined(XNU_KERNEL_PRIVATE) && XNU_KERNEL_PRIVATE
int ubc_create_upl(vnode_t, off_t, int, upl_t *, upl_page_info_t **, int);
#endif
#undef proc_t
#pragma pop_macro("proc_t")

#endif /* PANTHERA_ZFS_COMPAT_SYS_UBC_H */
