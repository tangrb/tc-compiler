#ifndef TC_VM_DEBUG_ELF_H
#define TC_VM_DEBUG_ELF_H

#include "ir/linemap.h"
#include "runtime/profile.h"

TcImplError tc_vm_debug_code_size(const TcPlatformProfile *profile,
                                  const TcLineMap *linemap,
                                  uint64_t *out_size);
TcImplError tc_vm_write_debug_elf(int fd, const TcLineMap *linemap,
                                  const TcPlatformProfile *profile);

#endif
