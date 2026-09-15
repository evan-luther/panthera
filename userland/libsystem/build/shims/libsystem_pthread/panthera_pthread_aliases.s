.text

.globl _pthread_cond_init
_pthread_cond_init: jmp "_pthread_cond_init$UNIX2003"
.globl _pthread_cond_timedwait
_pthread_cond_timedwait: jmp "_pthread_cond_timedwait$UNIX2003"
.globl _pthread_cond_wait
_pthread_cond_wait: jmp "_pthread_cond_wait$UNIX2003"
.globl _pthread_join
_pthread_join: jmp "_pthread_join$UNIX2003"
.globl _pthread_mutexattr_destroy
_pthread_mutexattr_destroy: jmp "_pthread_mutexattr_destroy$UNIX2003"
.globl _pthread_rwlock_destroy
_pthread_rwlock_destroy: jmp "_pthread_rwlock_destroy$UNIX2003"
.globl _pthread_rwlock_init
_pthread_rwlock_init: jmp "_pthread_rwlock_init$UNIX2003"
.globl _pthread_rwlock_rdlock
_pthread_rwlock_rdlock: jmp "_pthread_rwlock_rdlock$UNIX2003"
.globl _pthread_rwlock_unlock
_pthread_rwlock_unlock: jmp "_pthread_rwlock_unlock$UNIX2003"
.globl _pthread_rwlock_wrlock
_pthread_rwlock_wrlock: jmp "_pthread_rwlock_wrlock$UNIX2003"
.globl _pthread_sigmask
_pthread_sigmask: jmp "_pthread_sigmask$UNIX2003"
