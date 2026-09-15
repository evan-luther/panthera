/* Panthera shim: kern/restartable.h
 * Task restartable ranges — disabled via HAVE_TASK_RESTARTABLE_RANGES=0
 */
#ifndef _KERN_RESTARTABLE_H
#define _KERN_RESTARTABLE_H

#include <mach/kern_return.h>
#include <mach/mach_types.h>
#include <stdint.h>

typedef struct {
    uint64_t location;
    unsigned short length;
    unsigned short recovery_offs;
    unsigned int flags;
} task_restartable_range_t;

static inline kern_return_t
task_restartable_ranges_register(task_t task,
                                  task_restartable_range_t *ranges,
                                  unsigned int count)
{
    (void)task; (void)ranges; (void)count;
    return KERN_SUCCESS;
}

static inline kern_return_t
task_restartable_ranges_synchronize(task_t task)
{
    (void)task;
    return KERN_SUCCESS;
}

#endif /* _KERN_RESTARTABLE_H */
