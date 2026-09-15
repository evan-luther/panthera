#ifndef PANTHERA_LOADER_H
#define PANTHERA_LOADER_H

#include <stdint.h>

#define PANTHERA_MAX_KERNEL_SEGMENTS 16
#define PANTHERA_MAX_KERNEL_SECTIONS 64

typedef struct {
  char segname[16];
  uint64_t vmaddr;
  uint64_t vmsize;
  uint64_t fileoff;
  uint64_t filesize;
  uint32_t maxprot;
  uint32_t initprot;
} PantheraKernelSegment;

typedef struct {
  uint64_t addr;
  uint64_t size;
} PantheraKernelSection;

typedef struct {
  uint64_t entry_pc;
  uint64_t lowest_vmaddr;
  uint64_t highest_vmaddr;
  uint64_t file_size;
  uint32_t local_reloc_offset;
  uint32_t local_reloc_count;
  uint32_t section_count;
  uint32_t segment_count;
  PantheraKernelSegment segments[PANTHERA_MAX_KERNEL_SEGMENTS];
  PantheraKernelSection sections[PANTHERA_MAX_KERNEL_SECTIONS];
} PantheraKernelImage;

typedef enum {
  PANTHERA_LOADER_OK = 0,
  PANTHERA_LOADER_E_NULL = 1,
  PANTHERA_LOADER_E_TRUNCATED = 2,
  PANTHERA_LOADER_E_BAD_MAGIC = 3,
  PANTHERA_LOADER_E_BAD_CPU = 4,
  PANTHERA_LOADER_E_BAD_FILETYPE = 5,
  PANTHERA_LOADER_E_TOO_MANY_SEGMENTS = 6,
  PANTHERA_LOADER_E_MISSING_ENTRY = 7,
  PANTHERA_LOADER_E_BAD_RELOC = 8,
  PANTHERA_LOADER_E_TOO_MANY_SECTIONS = 9
} PantheraLoaderStatus;

PantheraLoaderStatus
panthera_parse_kernel_macho(
  const void *image,
  uint64_t image_size,
  PantheraKernelImage *out_image
);

PantheraLoaderStatus
panthera_rebase_kernel_locals(
  const void *file_image,
  uint64_t file_size,
  const PantheraKernelImage *kernel_image,
  void *loaded_image,
  uint64_t loaded_base,
  uint32_t *applied_count_out
);

PantheraLoaderStatus
panthera_slide_kernel_macho_metadata(
  const PantheraKernelImage *kernel_image,
  uint64_t loaded_base
);

#endif
