/* POSIX/UNIX2003 syscall names are owned by libsystem_kernel. */
.text
.globl _accept$UNIX2003
.set _accept$UNIX2003, ___accept
.globl _bind$UNIX2003
.set _bind$UNIX2003, ___bind
.globl _listen$UNIX2003
.set _listen$UNIX2003, ___listen
.globl _getpeername$UNIX2003
.set _getpeername$UNIX2003, ___getpeername
.globl _getsockname$UNIX2003
.set _getsockname$UNIX2003, ___getsockname
.globl _recvfrom$UNIX2003
.set _recvfrom$UNIX2003, ___recvfrom
.globl _recvmsg$UNIX2003
.set _recvmsg$UNIX2003, ___recvmsg
.globl _sendto$UNIX2003
.set _sendto$UNIX2003, ___sendto
.globl _sendmsg$UNIX2003
.set _sendmsg$UNIX2003, ___sendmsg
.globl _fsync$UNIX2003
.set _fsync$UNIX2003, _fsync
.globl _read$UNIX2003
.set _read$UNIX2003, _read
.globl _write$UNIX2003
.set _write$UNIX2003, _write
.globl _pread$UNIX2003
.set _pread$UNIX2003, _pread
.globl _pwrite$UNIX2003
.set _pwrite$UNIX2003, _pwrite

/* pselect is generated as the private syscall veneer ___pselect.  The
 * public Darwin header maps most LP64 userland callers to _pselect$1050,
 * so libsystem_kernel must export the public aliases too. */
.globl _pselect
.set _pselect, ___pselect
.globl _pselect$1050
.set _pselect$1050, ___pselect
.globl _pselect$DARWIN_EXTSN
.set _pselect$DARWIN_EXTSN, ___pselect
.globl _pselect$NOCANCEL
.set _pselect$NOCANCEL, ___pselect_nocancel
.globl _pselect$1050$NOCANCEL
.set _pselect$1050$NOCANCEL, ___pselect_nocancel
.globl _pselect$DARWIN_EXTSN$NOCANCEL
.set _pselect$DARWIN_EXTSN$NOCANCEL, ___pselect_nocancel
