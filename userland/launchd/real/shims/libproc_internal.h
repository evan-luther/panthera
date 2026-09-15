#ifndef _LIBPROC_INTERNAL_H
#define _LIBPROC_INTERNAL_H

#include <stdint.h>
#include <uuid/uuid.h>

/* Proc info structures used by runtime.c and core.c. */
#ifndef PROC_PIDUNIQIDENTIFIERINFO
#define PROC_PIDUNIQIDENTIFIERINFO 17
#define PROC_PIDUNIQIDENTIFIERINFO_SIZE (sizeof(struct proc_uniqidentifierinfo))

struct proc_uniqidentifierinfo {
    uint8_t  p_uuid[16];
    uint64_t p_uniqueid;
    uint64_t p_puniqueid;
    int32_t  p_reserve2;
    int32_t  p_reserve3;
    int32_t  p_reserve4;
};
#endif

#ifndef RUSAGE_INFO_V1
#define RUSAGE_INFO_V1 1
struct rusage_info_v1 {
    uint8_t  ri_uuid[16];
    uint64_t ri_user_time;
    uint64_t ri_system_time;
    uint64_t ri_pkg_idle_wkups;
    uint64_t ri_interrupt_wkups;
    uint64_t ri_pageins;
    uint64_t ri_wired_size;
    uint64_t ri_resident_size;
    uint64_t ri_phys_footprint;
    uint64_t ri_proc_start_abstime;
    uint64_t ri_proc_exit_abstime;
    uint64_t ri_child_user_time;
    uint64_t ri_child_system_time;
    uint64_t ri_child_pkg_idle_wkups;
    uint64_t ri_child_interrupt_wkups;
    uint64_t ri_child_pageins;
    uint64_t ri_child_elapsed_abstime;
};
#endif

#endif /* _LIBPROC_INTERNAL_H */
