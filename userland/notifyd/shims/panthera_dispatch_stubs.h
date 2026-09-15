/* Panthera dispatch stubs: included AFTER all system headers.
 * Redefines dispatch functions that notifyd uses for signal/timer sources
 * which don't work on Panthera's kqueue. The real dispatch_mach functions
 * are still used (not redefined here). */
#ifndef _PANTHERA_DISPATCH_STUBS_H
#define _PANTHERA_DISPATCH_STUBS_H

/* Only redefine these in .c files, not during header processing.
 * The -include flag processes this before other headers, so we need
 * to defer the actual redefinitions. We use a late-binding approach:
 * define a macro that the source file activates after all #includes. */

/* This header is force-included but the macros below will conflict with
 * dispatch headers. So instead, we provide no-op INLINE functions that
 * we alias via -D flags in the build command. */

#endif /* _PANTHERA_DISPATCH_STUBS_H */
