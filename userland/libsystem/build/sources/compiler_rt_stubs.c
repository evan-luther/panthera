/* Minimal compiler_rt stubs for Panthera */
long __stack_chk_guard = 0x595e9fbd94fda766ULL;
void __stack_chk_fail(void) { __asm__ volatile("ud2"); }
