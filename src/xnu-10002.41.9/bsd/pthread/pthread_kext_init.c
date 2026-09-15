/*
 * pthread_kext_init.c - Built-in pthread kext registration for Panthera.
 *
 * Register the same pthread-kext surface libpthread expects: thread
 * creation/termination, workqueues, and the psynch primitives used by
 * libpthread-519 mutexes, condition variables, and rwlocks.
 */

#define PTHREAD_INTERNAL 1

#include <kern/thread.h>
#include <kern/debug.h>
#include "pthread_kext_internal.h"

/* Declared in pthread_shims.c */
extern pthread_callbacks_t pthread_kern;

/* Real implementations from pthread_kext_support.c */
extern void _pthread_init(void);
extern void _pth_proc_hashinit(proc_t p);
extern void _pth_proc_hashdelete(proc_t p);
extern int _bsdthread_create(struct proc *p, user_addr_t user_func, user_addr_t user_funcarg, user_addr_t user_stack, user_addr_t user_pthread, uint32_t flags, user_addr_t *retval);
extern int _bsdthread_register(struct proc *p, user_addr_t threadstart, user_addr_t wqthread, int pthsize, user_addr_t dummy_value, user_addr_t targetconc_ptr, uint64_t dispatchqueue_offset, int32_t *retval);
extern int _bsdthread_terminate(struct proc *p, user_addr_t stackaddr, size_t size, uint32_t kthport, uint32_t sem, int32_t *retval);
extern int _thread_selfid(struct proc *p, uint64_t *retval);
extern int _psynch_mutexwait(proc_t p, user_addr_t mutex, uint32_t mgen, uint32_t ugen, uint64_t tid, uint32_t flags, uint32_t *retval);
extern int _psynch_mutexdrop(proc_t p, user_addr_t mutex, uint32_t mgen, uint32_t ugen, uint64_t tid, uint32_t flags, uint32_t *retval);
extern int _psynch_cvbroad(proc_t p, user_addr_t cv, uint64_t cvlsgen, uint64_t cvudgen, uint32_t flags, user_addr_t mutex, uint64_t mugen, uint64_t tid, uint32_t *retval);
extern int _psynch_cvsignal(proc_t p, user_addr_t cv, uint64_t cvlsgen, uint32_t cvugen, int thread_port, user_addr_t mutex, uint64_t mugen, uint64_t tid, uint32_t flags, uint32_t *retval);
extern int _psynch_cvwait(proc_t p, user_addr_t cv, uint64_t cvlsgen, uint32_t cvugen, user_addr_t mutex, uint64_t mugen, uint32_t flags, int64_t sec, uint32_t nsec, uint32_t *retval);
extern int _psynch_cvclrprepost(proc_t p, user_addr_t cv, uint32_t cvgen, uint32_t cvugen, uint32_t cvsgen, uint32_t prepocnt, uint32_t preposeq, uint32_t flags, int *retval);
extern int _psynch_rw_longrdlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
extern int _psynch_rw_rdlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
extern int _psynch_rw_unlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
extern int _psynch_rw_wrlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);
extern int _psynch_rw_yieldwrlock(proc_t p, user_addr_t rwlock, uint32_t lgenval, uint32_t ugenval, uint32_t rw_wc, int flags, uint32_t *retval);

extern int workq_create_threadstack(proc_t p, vm_map_t vmap, mach_vm_offset_t *out_addr);
extern int workq_destroy_threadstack(proc_t p, vm_map_t vmap, mach_vm_offset_t stackaddr);
extern void workq_setup_thread(proc_t p, thread_t th, vm_map_t map, user_addr_t stackaddr, mach_port_name_t kport, int th_qos, int setup_flags, int upcall_flags);
extern int workq_handle_stack_events(proc_t p, thread_t th, vm_map_t map, user_addr_t stackaddr, mach_port_name_t kport, user_addr_t events, int nevents, int upcall_flags);
extern void workq_markfree_threadstack(proc_t p, thread_t th, vm_map_t vmap, user_addr_t stackaddr);

static void pthread_find_owner_stub(thread_t t __unused, struct stackshot_thread_waitinfo *w __unused) { }
static void *pthread_get_thread_kwq_stub(thread_t t __unused) { return NULL; }

static const struct pthread_functions_s pthread_internal_functions = {
	.pthread_init = _pthread_init,
	.pth_proc_hashinit = _pth_proc_hashinit,
	.pth_proc_hashdelete = _pth_proc_hashdelete,
	.bsdthread_create = _bsdthread_create,
	.bsdthread_register = _bsdthread_register,
	.bsdthread_terminate = _bsdthread_terminate,
	.thread_selfid = _thread_selfid,

	.psynch_mutexwait = _psynch_mutexwait,
	.psynch_mutexdrop = _psynch_mutexdrop,
	.psynch_cvbroad = _psynch_cvbroad,
	.psynch_cvsignal = _psynch_cvsignal,
	.psynch_cvwait = _psynch_cvwait,
	.psynch_cvclrprepost = _psynch_cvclrprepost,
	.psynch_rw_longrdlock = _psynch_rw_longrdlock,
	.psynch_rw_rdlock = _psynch_rw_rdlock,
	.psynch_rw_unlock = _psynch_rw_unlock,
	.psynch_rw_wrlock = _psynch_rw_wrlock,
	.psynch_rw_yieldwrlock = _psynch_rw_yieldwrlock,

	.pthread_find_owner = pthread_find_owner_stub,
	.pthread_get_thread_kwq = pthread_get_thread_kwq_stub,

	.workq_create_threadstack = workq_create_threadstack,
	.workq_destroy_threadstack = workq_destroy_threadstack,
	.workq_setup_thread = workq_setup_thread,
	.workq_handle_stack_events = workq_handle_stack_events,
	.workq_markfree_threadstack = workq_markfree_threadstack,
};

void pthread_kext_builtin_init(void);
void
pthread_kext_builtin_init(void)
{
	pthread_kext_register((pthread_functions_t)&pthread_internal_functions, &pthread_kern);
}
