/*
 * System/sys/proc_info.h — Private proc_info structures for Panthera.
 */
#ifndef _SYSTEM_SYS_PROC_INFO_H
#define _SYSTEM_SYS_PROC_INFO_H

#include <stdint.h>

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

#endif /* _SYSTEM_SYS_PROC_INFO_H */
