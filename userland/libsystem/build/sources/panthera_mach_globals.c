/*
 * Explicit Mach globals for early userland.
 *
 * dyld seeds mach_task_self_ before libmalloc starts. Keep this symbol in a
 * dedicated object so it survives partial relinks of libpanthera_extra.
 */

unsigned int mach_task_self_ = 0;
unsigned int mach_host_self_ = 0;
