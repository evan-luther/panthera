/* panthera_unix2003_bridge.s — Map plain C names to $UNIX2003 variants */
.text

/* stdio */
.globl _fputs
_fputs: jmp "_fputs$UNIX2003"
.globl _fwrite
_fwrite: jmp "_fwrite$UNIX2003"
.globl _fopen
_fopen: jmp "_fopen$UNIX2003"
.globl _fdopen
_fdopen: jmp "_fdopen$UNIX2003"
.globl _freopen
_freopen: jmp "_freopen$UNIX2003"
.globl "_fopen$DARWIN_EXTSN"
"_fopen$DARWIN_EXTSN": jmp "_fopen$UNIX2003"
.globl "_fdopen$DARWIN_EXTSN"
"_fdopen$DARWIN_EXTSN": jmp "_fdopen$UNIX2003"

/* time */
.globl _nanosleep
_nanosleep: jmp "_nanosleep$UNIX2003"
.globl _mktime
_mktime: jmp "_mktime$UNIX2003"
.globl _strftime
_strftime: jmp "_strftime$UNIX2003"
.globl _strftime_l
_strftime_l: jmp "_strftime_l$UNIX2003"
.globl _strptime
_strptime: jmp "_strptime$UNIX2003"
.globl _strptime_l
_strptime_l: jmp "_strptime_l$UNIX2003"
.globl _clock
_clock: jmp "_clock$UNIX2003"
.globl _wcsftime
_wcsftime: jmp "_wcsftime$UNIX2003"
.globl _wcsftime_l
_wcsftime_l: jmp "_wcsftime_l$UNIX2003"

/* stdlib/env */
.globl _setenv
_setenv: jmp "_setenv$UNIX2003"
.globl _putenv
_putenv: jmp "_putenv$UNIX2003"
.globl _unsetenv
_unsetenv: jmp "_unsetenv$UNIX2003"

/* string */
.globl _strtod
_strtod: jmp "_strtod$UNIX2003"
.globl _strtod_l
_strtod_l: jmp "_strtod_l$UNIX2003"
.globl _strtof
_strtof: jmp "_strtof$UNIX2003"
.globl _strtof_l
_strtof_l: jmp "_strtof_l$UNIX2003"
.globl _strtok_r
_strtok_r: jmp ___strtok_r
.globl _confstr
_confstr: jmp "_confstr$UNIX2003"
.globl _tempnam
_tempnam: jmp "_tempnam$UNIX2003"

/* process/signal */
.globl _killpg
_killpg: jmp "_killpg$UNIX2003"
.globl _setpgrp
_setpgrp: jmp "_setpgrp$UNIX2003"
.globl _nice
_nice: jmp "_nice$UNIX2003"
.globl _sigaltstack
_sigaltstack: jmp "_sigaltstack$UNIX2003"
.globl _lockf
_lockf: jmp "_lockf$UNIX2003"
.globl _encrypt
_encrypt: jmp "_encrypt$UNIX2003"
.globl _setkey
_setkey: jmp "_setkey$UNIX2003"
.globl _setmode
_setmode: jmp "_setmode$UNIX2003"

/* directory $INODE64 */
.globl _closedir
_closedir: jmp "_closedir$INODE64"
.globl "_opendir$INODE64"
"_opendir$INODE64": jmp "___opendir2$INODE64$UNIX2003"
.globl "_seekdir$INODE64"
"_seekdir$INODE64": jmp "__seekdir$INODE64$UNIX2003"
.globl "_rewinddir$INODE64"
"_rewinddir$INODE64": jmp "_rewinddir$INODE64$UNIX2003"
.globl "_telldir$INODE64"
"_telldir$INODE64": jmp "_telldir$INODE64$UNIX2003"

/* compat */
.globl _setregid
_setregid: jmp "_setregid$UNIX2003"
.globl _setreuid
_setreuid: jmp "_setreuid$UNIX2003"
.globl _setattrlist
_setattrlist: jmp "_setattrlist$UNIX2003"
.globl _socketpair
_socketpair: jmp "_socketpair$UNIX2003"
.globl _getopt
_getopt: jmp "_getopt$UNIX2003"
.globl _usleep
_usleep: jmp "_usleep$UNIX2003"
.globl _ttyname_r
_ttyname_r: jmp "_ttyname_r$UNIX2003"
.globl _timezone
_timezone: jmp "_timezone$UNIX2003"
.globl _regcomp
_regcomp: jmp "_regcomp$UNIX2003"

/* network */
.globl _sigpause
_sigpause: jmp "_sigpause$NOCANCEL$UNIX2003"
.globl "_sigsuspend$NOCANCEL"
"_sigsuspend$NOCANCEL": jmp "_sigsuspend$NOCANCEL$UNIX2003"
.globl "_recv$NOCANCEL"
"_recv$NOCANCEL": jmp "_recv$NOCANCEL$UNIX2003"
.globl "_send$NOCANCEL"
"_send$NOCANCEL": jmp "_send$NOCANCEL$UNIX2003"
.globl "_usleep$NOCANCEL"
"_usleep$NOCANCEL": jmp "_usleep$NOCANCEL$UNIX2003"

/* writev */
.globl "_writev$UNIX2003"
"_writev$UNIX2003": jmp _writev
.globl _task_set_special_port
_task_set_special_port: jmp ___task_set_special_port
