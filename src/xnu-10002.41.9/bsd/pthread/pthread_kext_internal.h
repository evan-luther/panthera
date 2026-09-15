/*
 * pthread_kext_internal.h — Internal header for built-in pthread kext
 *
 * Adapted from libpthread-519/kern/kern_internal.h for in-kernel compilation.
 */

#ifndef _SYS_PTHREAD_KEXT_INTERNAL_H_
#define _SYS_PTHREAD_KEXT_INTERNAL_H_

/* Provide TargetConditionals for the original header's use */
#ifndef TARGET_OS_OSX
#define TARGET_OS_OSX 1
#endif
#ifndef TARGET_CPU_X86_64
#define TARGET_CPU_X86_64 1
#endif
#ifndef TARGET_CPU_ARM64
#define TARGET_CPU_ARM64 0
#endif

/* XNU-internal includes for bsdthread/priority/workqueue private headers */
#include <pthread/bsdthread_private.h>
#include <pthread/priority_private.h>
#include <pthread/workqueue_syscalls.h>

#ifdef KERNEL
#include <stdatomic.h>
#include <kern/thread_call.h>
#include <kern/kcdata.h>
#include <sys/queue.h>
#include <kern/thread.h>

typedef struct ksyn_waitq_element * ksyn_waitq_element_t;

/* Forward-declare kq_index_t before workqueue_internal.h needs it */
#ifndef _KQ_INDEX_T_DEFINED
#define _KQ_INDEX_T_DEFINED
typedef uint8_t kq_index_t;
#endif
#include <pthread/workqueue_internal.h>
#include <sys/pthread_shims.h>
#include <sys/proc_info.h>
#endif /* KERNEL */

#include "synch_internal.h"
#include "kern_trace.h"

/* QoS types are provided by priority_private.h above */

/* Feature flags */
#define PTHREAD_FEATURE_DISPATCHFUNC	0x01
#define PTHREAD_FEATURE_FINEPRIO		0x02
#define PTHREAD_FEATURE_BSDTHREADCTL	0x04
#define PTHREAD_FEATURE_SETSELF			0x08
#define PTHREAD_FEATURE_QOS_MAINTENANCE	0x10
#define PTHREAD_FEATURE_RESERVED		0x20
#define PTHREAD_FEATURE_KEVENT          0x40
#define PTHREAD_FEATURE_WORKLOOP          0x80
#define PTHREAD_FEATURE_QOS_DEFAULT		0x40000000
#define PTHREAD_FEATURE_COOPERATIVE_WORKQ	0x100
#define PTHREAD_FEATURE_JIT_ALLOWLIST	0x200
#define PTHREAD_FEATURE_JIT_FREEZE_LATE	0x00000800

#define PTHREAD_FEATURE_SUPPORTED	( \
	PTHREAD_FEATURE_DISPATCHFUNC | \
	PTHREAD_FEATURE_FINEPRIO | \
	PTHREAD_FEATURE_BSDTHREADCTL | \
	PTHREAD_FEATURE_SETSELF | \
	PTHREAD_FEATURE_QOS_MAINTENANCE | \
	PTHREAD_FEATURE_QOS_DEFAULT | \
	PTHREAD_FEATURE_KEVENT | \
	PTHREAD_FEATURE_WORKLOOP |  \
	PTHREAD_FEATURE_COOPERATIVE_WORKQ)

/* Registration data struct for userspace <-> kernel init */
struct _pthread_registration_data {
	uint64_t version;
	uint64_t dispatch_queue_offset;
	uint64_t main_qos;
	uint32_t tsd_offset;
	uint32_t return_to_kernel_offset;
	uint32_t mach_thread_self_offset;
	mach_vm_address_t stack_addr_hint;
#define _PTHREAD_REG_DEFAULT_POLICY_MASK 0xff
#define _PTHREAD_REG_DEFAULT_USE_ULOCK 0x100
#define _PTHREAD_REG_DEFAULT_USE_ADAPTIVE_SPIN 0x200
	uint32_t mutex_default_policy;
	uint32_t joinable_offset_bits;
	uint32_t wq_quantum_expiry_offset;
} __attribute__ ((packed));

#define ECVCLEARED	0x100
#define ECVPREPOST	0x200

#ifndef VARIANT_DYLD
#define VARIANT_DYLD 0
#endif

#define _PTHREAD_CONFIG_JIT_WRITE_PROTECT 0

#ifdef KERNEL

extern pthread_callbacks_t pthread_kern;

#define PTH_DEFAULT_STACKSIZE 512*1024
#define MAX_PTHREAD_SIZE 64*1024

/* port_name_to_thread is declared in kern/thread.h with 2 args in this XNU version */

/* VM_MAKE_TAG is only defined for !XNU_KERNEL_PRIVATE, but the kext code needs it */
#ifndef VM_MAKE_TAG
#define VM_MAKE_TAG(tag) ((tag) << 24)
#endif

/* Function declarations */
void _pthread_init(void);
void _pth_proc_hashinit(proc_t p);
void _pth_proc_hashdelete(proc_t p);
void pth_global_hashinit(void);
void psynch_wq_cleanup(void*, void*);

int _bsdthread_create(struct proc *p, user_addr_t user_func, user_addr_t user_funcarg, user_addr_t user_stack, user_addr_t user_pthread, uint32_t flags, user_addr_t *retval);
int _bsdthread_register(struct proc *p, user_addr_t threadstart, user_addr_t wqthread, int pthsize, user_addr_t dummy_value, user_addr_t targetconc_ptr, uint64_t dispatchqueue_offset, int32_t *retval);
int _bsdthread_terminate(struct proc *p, user_addr_t stackaddr, size_t size, uint32_t kthport, uint32_t sem, int32_t *retval);
int _bsdthread_ctl(struct proc *p, user_addr_t cmd, user_addr_t arg1, user_addr_t arg2, user_addr_t arg3, int *retval);
int _thread_selfid(struct proc *p, uint64_t *retval);
int _workq_kernreturn(struct proc *p, int options, user_addr_t item, int arg2, int arg3, int32_t *retval);
int _workq_open(struct proc *p, int32_t *retval);

int _psynch_mutexwait(proc_t p, user_addr_t mutex, uint32_t mgen, uint32_t ugen, uint64_t tid, uint32_t flags, uint32_t *retval);
int _psynch_mutexdrop(proc_t p, user_addr_t mutex, uint32_t mgen, uint32_t ugen, uint64_t tid, uint32_t flags, uint32_t *retval);
int _psynch_cvbroad(proc_t p, user_addr_t cv, uint64_t cvlsgen, uint64_t cvudgen, uint32_t flags, user_addr_t mutex, uint64_t mugen, uint64_t tid, uint32_t *retval);
int _psynch_cvsignal(proc_t p, user_addr_t cv, uint64_t cvlsgen, uint32_t cvugen, int thread_port, user_addr_t mutex, uint64_t mugen, uint64_t tid, uint32_t flags, uint32_t *retval);
int _psynch_cvwait(proc_t p, user_addr_t cv, uint64_t cvlsgen, uint32_t cvugen, user_addr_t mutex, uint64_t mugen, uint32_t flags, int64_t sec, uint32_t nsec, uint32_t *retval);
int _psynch_cvclrprepost(proc_t p, user_addr_t cv, uint32_t cvgen, uint32_t cvugen, uint32_t cvsgen, uint32_t prepocnt, uint32_t preposeq, uint32_t flags, int *retval);
int _psynch_rw_longrdlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
int _psynch_rw_rdlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
int _psynch_rw_unlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
int _psynch_rw_wrlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
int _psynch_rw_yieldwrlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);

void _pthread_find_owner(thread_t thread, struct stackshot_thread_waitinfo *waitinfo);
void *_pthread_get_thread_kwq(thread_t thread);

extern lck_grp_attr_t *pthread_lck_grp_attr;
extern lck_grp_t *pthread_lck_grp;
extern lck_attr_t *pthread_lck_attr;
extern lck_mtx_t *pthread_list_mlock;
extern thread_call_t psynch_thcall;

struct uthread* current_uthread(void);

int workq_create_threadstack(proc_t p, vm_map_t vmap, mach_vm_offset_t *out_addr);
int workq_destroy_threadstack(proc_t p, vm_map_t vmap, mach_vm_offset_t stackaddr);
void workq_setup_thread(proc_t p, thread_t th, vm_map_t map, user_addr_t stackaddr, mach_port_name_t kport, int th_qos, int setup_flags, int upcall_flags);
int workq_handle_stack_events(proc_t p, thread_t th, vm_map_t map, user_addr_t stackaddr, mach_port_name_t kport, user_addr_t events, int nevents, int upcall_flags);
void workq_markfree_threadstack(proc_t p, thread_t th, vm_map_t vmap, user_addr_t stackaddr);

#endif /* KERNEL */

#define WORKQ_EXIT_THREAD_NKEVENT   (-1)

#endif /* _SYS_PTHREAD_KEXT_INTERNAL_H_ */
