.text

/* stdio aliases */
.globl _fclose$DARWIN_EXTSN
_fclose$DARWIN_EXTSN: jmp _fclose
.globl _fdopen
_fdopen: jmp "_fdopen$DARWIN_EXTSN"
.globl "_fdopen$UNIX2003"
"_fdopen$UNIX2003": jmp "_fdopen$DARWIN_EXTSN"
.globl _fgets$DARWIN_EXTSN
_fgets$DARWIN_EXTSN: jmp _fgets
.globl _fopen
_fopen: jmp "_fopen$DARWIN_EXTSN"
.globl "_fopen$UNIX2003"
"_fopen$UNIX2003": jmp "_fopen$DARWIN_EXTSN"
.globl _fputs
_fputs: jmp "_fputs$UNIX2003"
.globl _fputs$DARWIN_EXTSN
_fputs$DARWIN_EXTSN: jmp "_fputs$UNIX2003"
.globl _fread$DARWIN_EXTSN
_fread$DARWIN_EXTSN: jmp _fread
.globl _fwrite
_fwrite: jmp "_fwrite$UNIX2003"
.globl _fwrite$DARWIN_EXTSN
_fwrite$DARWIN_EXTSN: jmp "_fwrite$UNIX2003"

/* directory aliases */
.globl _closedir
_closedir: jmp "_closedir$UNIX2003"
.globl _fdopendir
_fdopendir: jmp "_fdopendir$INODE64$UNIX2003"
.globl _fdopendir$INODE64
_fdopendir$INODE64: jmp "_fdopendir$INODE64$UNIX2003"
.globl _opendir
_opendir: jmp "_opendir$INODE64$UNIX2003"
.globl "_opendir$INODE64"
"_opendir$INODE64": jmp "_opendir$INODE64$UNIX2003"
.globl ___opendir2
___opendir2: jmp "___opendir2$INODE64$UNIX2003"
.globl "___opendir2$INODE64"
"___opendir2$INODE64": jmp "___opendir2$INODE64$UNIX2003"
.globl _readdir
_readdir: jmp _readdir$INODE64
.globl _readdir_r
_readdir_r: jmp _readdir_r$INODE64

/* time and locale aliases */
.globl _mktime
_mktime: jmp "_mktime$UNIX2003"
.globl _memccpy
_memccpy: jmp __platform_memccpy
.globl _nanosleep
_nanosleep: jmp "_nanosleep$UNIX2003"
.globl _nice
_nice: jmp "_nice$UNIX2003"
.globl _strlcat
_strlcat: jmp __platform_strlcat
.globl _strerror
_strerror: jmp "_strerror$UNIX2003"
.globl _strftime
_strftime: jmp "_strftime$UNIX2003"
.globl _strnlen
_strnlen: jmp __platform_strnlen
.globl _strptime
_strptime: jmp "_strptime$UNIX2003"
.globl _strtod
_strtod: jmp "_strtod$UNIX2003"
.globl _usleep
_usleep: jmp "_usleep$UNIX2003"

/* stdlib/process aliases */
.globl _daemon
_daemon: jmp "_daemon$1050"
.globl _fchmod
_fchmod: jmp "_fchmod$UNIX2003"
.globl _fcntl
_fcntl: jmp "_fcntl$UNIX2003"
.globl _getopt
_getopt: jmp "_getopt$UNIX2003"
.globl _getrlimit
_getrlimit: jmp "_getrlimit$UNIX2003"
.globl _index
_index: jmp _strchr
.globl _kill
_kill: jmp "_kill$UNIX2003"
.globl _killpg
_killpg: jmp "_killpg$UNIX2003"
.globl _mmap
_mmap: jmp "_mmap$UNIX2003"
.globl _munmap
_munmap: jmp "_munmap$UNIX2003"
.globl _open
_open: jmp "_open$UNIX2003"
.globl _putenv
_putenv: jmp "_putenv$UNIX2003"
.globl _select
_select: jmp "_select$DARWIN_EXTSN"
.globl _setenv
_setenv: jmp "_setenv$UNIX2003"
.globl _setmode
_setmode: jmp "_setmode$UNIX2003"
.globl _setrlimit
_setrlimit: jmp "_setrlimit$UNIX2003"
.globl _regcomp
_regcomp: jmp "_regcomp$UNIX2003"
.globl _sleep
_sleep: jmp "_sleep$UNIX2003"
.globl "_sleep$NOCANCEL"
"_sleep$NOCANCEL": jmp "_sleep$NOCANCEL$UNIX2003"
.globl _sigsuspend
_sigsuspend:
    // XNU takes the mask by value; the public API takes a pointer.
    testq %rdi, %rdi
    je 1f
    movl (%rdi), %edi
1:
    jmp ___sigsuspend
.globl _select$1050
_select$1050: jmp _select
.globl _strtok_r
_strtok_r: jmp ___strtok_r
.globl _tcdrain
_tcdrain: jmp ___tcdrain
.globl _unsetenv
_unsetenv: jmp "_unsetenv$UNIX2003"
.globl _vfscanf
_vfscanf: jmp ___vfscanf
