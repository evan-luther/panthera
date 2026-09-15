/* Panthera shim: os/linker_set.h
 * Linker set iteration macros.
 */
#ifndef _OS_LINKER_SET_H
#define _OS_LINKER_SET_H

/* LINKER_SET_FOREACH iterates over entries placed in a Mach-O section.
 * On Panthera we provide a real implementation using getsectiondata.
 */
#include <mach-o/getsect.h>
#include <stdint.h>

#define LINKER_SET_FOREACH(_var, _type, _sectname) \
    unsigned long _ls_sz_ = 0; \
    _type _ls_start_ = (_type)getsectiondata( \
        &_mh_dylib_header, "__DATA", (_sectname), &_ls_sz_); \
    _type _ls_end_ = (_type)((uintptr_t)_ls_start_ + _ls_sz_); \
    for (_type _var = _ls_start_; _ls_start_ && _var < _ls_end_; _var++)

#endif /* _OS_LINKER_SET_H */
