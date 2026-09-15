#include "../include/panthera_loader.h"

#define MH_MAGIC_64 0xfeedfacfU
#define CPU_TYPE_X86_64 0x01000007U
#define MH_EXECUTE 0x2U
#define LC_SEGMENT_64 0x19U
#define LC_UNIXTHREAD 0x5U
#define LC_SYMTAB 0x2U
#define LC_DYSYMTAB 0xbU
#define X86_64_RELOC_UNSIGNED 0U
#define N_STAB 0xe0U
#define N_TYPE 0x0eU
#define N_SECT 0x0eU
#define N_GSYM 0x20U
#define N_FNAME 0x22U
#define N_STSYM 0x26U
#define N_LCSYM 0x28U
#define N_BNSYM 0x2eU
#define N_RSYM 0x40U
#define N_SLINE 0x44U
#define N_ENSYM 0x4eU
#define N_SSYM 0x60U
#define N_SO 0x64U
#define N_OSO 0x66U
#define N_LSYM 0x80U
#define N_BINCL 0x82U
#define N_SOL 0x84U
#define N_PARAMS 0x86U
#define N_VERSION 0x88U
#define N_OLEVEL 0x8aU
#define N_PSYM 0xa0U
#define N_EINCL 0xa2U
#define N_ENTRY 0xa4U
#define N_LBRAC 0xc0U
#define N_EXCL 0xc2U
#define N_RBRAC 0xe0U
#define N_BCOMM 0xe2U
#define N_ECOMM 0xe4U
#define N_ECOML 0xe8U
#define N_LENG 0xfeU

typedef struct {
  uint32_t magic;
  int32_t cputype;
  int32_t cpusubtype;
  uint32_t filetype;
  uint32_t ncmds;
  uint32_t sizeofcmds;
  uint32_t flags;
  uint32_t reserved;
} PantheraMachHeader64;

typedef struct {
  uint32_t cmd;
  uint32_t cmdsize;
} PantheraLoadCommand;

typedef struct {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t symoff;
  uint32_t nsyms;
  uint32_t stroff;
  uint32_t strsize;
} PantheraSymtabCommand;

typedef struct {
  uint32_t cmd;
  uint32_t cmdsize;
  char segname[16];
  uint64_t vmaddr;
  uint64_t vmsize;
  uint64_t fileoff;
  uint64_t filesize;
  uint32_t maxprot;
  uint32_t initprot;
  uint32_t nsects;
  uint32_t flags;
} PantheraSegmentCommand64;

typedef struct {
  char sectname[16];
  char segname[16];
  uint64_t addr;
  uint64_t size;
  uint32_t offset;
  uint32_t align;
  uint32_t reloff;
  uint32_t nreloc;
  uint32_t flags;
  uint32_t reserved1;
  uint32_t reserved2;
  uint32_t reserved3;
} PantheraSection64;

typedef struct {
  uint32_t cmd;
  uint32_t cmdsize;
  uint32_t ilocalsym;
  uint32_t nlocalsym;
  uint32_t iextdefsym;
  uint32_t nextdefsym;
  uint32_t iundefsym;
  uint32_t nundefsym;
  uint32_t tocoff;
  uint32_t ntoc;
  uint32_t modtaboff;
  uint32_t nmodtab;
  uint32_t extrefsymoff;
  uint32_t nextrefsyms;
  uint32_t indirectsymoff;
  uint32_t nindirectsyms;
  uint32_t extreloff;
  uint32_t nextrel;
  uint32_t locreloff;
  uint32_t nlocrel;
} PantheraDysymtabCommand;

typedef struct {
  uint32_t address;
  uint32_t info;
} PantheraRelocationInfo;

typedef struct {
  union {
    uint32_t n_strx;
  } n_un;
  uint8_t n_type;
  uint8_t n_sect;
  uint16_t n_desc;
  uint64_t n_value;
} PantheraNList64;

typedef struct {
  uint32_t flavor;
  uint32_t count;
} PantheraThreadStateHeader;

typedef struct {
  uint64_t rax;
  uint64_t rbx;
  uint64_t rcx;
  uint64_t rdx;
  uint64_t rdi;
  uint64_t rsi;
  uint64_t rbp;
  uint64_t rsp;
  uint64_t r8;
  uint64_t r9;
  uint64_t r10;
  uint64_t r11;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;
  uint64_t rip;
  uint64_t rflags;
  uint64_t cs;
  uint64_t fs;
  uint64_t gs;
} PantheraX86ThreadState64;

static void
panthera_zero(void *buffer, uint64_t size)
{
  uint8_t *bytes = (uint8_t *)buffer;
  uint64_t index;

  for (index = 0; index < size; index++) {
    bytes[index] = 0;
  }
}

static void
panthera_copy16(char dst[16], const char src[16])
{
  uint32_t index;

  for (index = 0; index < 16; index++) {
    dst[index] = src[index];
  }
}

static uint8_t *
panthera_loaded_vmaddr(
  const PantheraKernelImage *kernel_image,
  uint64_t loaded_base,
  uint64_t vmaddr,
  uint64_t size
)
{
  if (vmaddr < kernel_image->lowest_vmaddr) {
    return 0;
  }
  if (size > kernel_image->highest_vmaddr - vmaddr) {
    return 0;
  }

  return (uint8_t *)(uintptr_t)(loaded_base + (vmaddr - kernel_image->lowest_vmaddr));
}

static uint8_t *
panthera_loaded_file_offset(
  const PantheraKernelImage *kernel_image,
  uint64_t loaded_base,
  uint64_t file_offset,
  uint64_t size
)
{
  uint32_t segment_index;

  for (segment_index = 0; segment_index < kernel_image->segment_count; segment_index++) {
    const PantheraKernelSegment *segment = &kernel_image->segments[segment_index];
    uint64_t segment_file_offset;

    if (segment->filesize == 0) {
      continue;
    }
    if (file_offset < segment->fileoff) {
      continue;
    }
    segment_file_offset = file_offset - segment->fileoff;
    if (segment_file_offset > segment->filesize) {
      continue;
    }
    if (size > segment->filesize - segment_file_offset) {
      continue;
    }

    return panthera_loaded_vmaddr(
      kernel_image,
      loaded_base,
      segment->vmaddr + segment_file_offset,
      size
    );
  }

  return 0;
}

static int
panthera_name_equals(const char lhs[16], const char *rhs)
{
  uint32_t index;

  for (index = 0; index < 16; index++) {
    char rhs_char = rhs[index];

    if (lhs[index] != rhs_char) {
      return 0;
    }
    if (rhs_char == '\0') {
      return 1;
    }
  }

  return rhs[16] == '\0';
}

static int
panthera_symbol_is_section_based(uint8_t n_type)
{
  if ((n_type & N_STAB) != 0) {
    switch (n_type) {
      case N_STSYM:
      case N_LCSYM:
      case N_BNSYM:
      case N_SLINE:
      case N_ENSYM:
      case N_SO:
      case N_SOL:
      case N_ENTRY:
      case N_ECOMM:
      case N_ECOML:
      case N_RBRAC:
      case N_LBRAC:
        return 1;
      default:
        return 0;
    }
  }

  return (n_type & N_TYPE) == N_SECT;
}

PantheraLoaderStatus
panthera_parse_kernel_macho(
  const void *image,
  uint64_t image_size,
  PantheraKernelImage *out_image
)
{
  const uint8_t *cursor;
  const PantheraMachHeader64 *header;
  uint32_t command_index;

  if (image == 0 || out_image == 0) {
    return PANTHERA_LOADER_E_NULL;
  }

  if (image_size < sizeof(PantheraMachHeader64)) {
    return PANTHERA_LOADER_E_TRUNCATED;
  }

  header = (const PantheraMachHeader64 *)image;
  if (header->magic != MH_MAGIC_64) {
    return PANTHERA_LOADER_E_BAD_MAGIC;
  }
  if ((uint32_t)header->cputype != CPU_TYPE_X86_64) {
    return PANTHERA_LOADER_E_BAD_CPU;
  }
  if (header->filetype != MH_EXECUTE) {
    return PANTHERA_LOADER_E_BAD_FILETYPE;
  }
  if ((uint64_t)sizeof(PantheraMachHeader64) + (uint64_t)header->sizeofcmds > image_size) {
    return PANTHERA_LOADER_E_TRUNCATED;
  }

  panthera_zero(out_image, sizeof(*out_image));
  out_image->file_size = image_size;
  out_image->lowest_vmaddr = UINT64_MAX;

  cursor = (const uint8_t *)image + sizeof(PantheraMachHeader64);
  for (command_index = 0; command_index < header->ncmds; command_index++) {
    const PantheraLoadCommand *load_command;

    if ((uint64_t)(cursor - (const uint8_t *)image) + sizeof(PantheraLoadCommand) > image_size) {
      return PANTHERA_LOADER_E_TRUNCATED;
    }

    load_command = (const PantheraLoadCommand *)cursor;
    if (load_command->cmdsize < sizeof(PantheraLoadCommand)) {
      return PANTHERA_LOADER_E_TRUNCATED;
    }
    if ((uint64_t)(cursor - (const uint8_t *)image) + load_command->cmdsize > image_size) {
      return PANTHERA_LOADER_E_TRUNCATED;
    }

    if (load_command->cmd == LC_SEGMENT_64) {
      const PantheraSegmentCommand64 *segment;
      uint64_t segment_end;

      if (load_command->cmdsize < sizeof(PantheraSegmentCommand64)) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      segment = (const PantheraSegmentCommand64 *)cursor;
      if (load_command->cmdsize < sizeof(PantheraSegmentCommand64) +
          (uint64_t)segment->nsects * sizeof(PantheraSection64)) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }
      if (segment->vmsize == 0 && segment->filesize == 0) {
        cursor += load_command->cmdsize;
        continue;
      }

      if (segment->vmaddr < out_image->lowest_vmaddr) {
        out_image->lowest_vmaddr = segment->vmaddr;
      }
      segment_end = segment->vmaddr + segment->vmsize;
      if (segment_end > out_image->highest_vmaddr) {
        out_image->highest_vmaddr = segment_end;
      }

      if (segment->fileoff + segment->filesize > image_size) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      if (out_image->segment_count >= PANTHERA_MAX_KERNEL_SEGMENTS) {
        return PANTHERA_LOADER_E_TOO_MANY_SEGMENTS;
      }

      {
        PantheraKernelSegment *out_segment = &out_image->segments[out_image->segment_count++];
        panthera_copy16(out_segment->segname, segment->segname);
        out_segment->vmaddr = segment->vmaddr;
        out_segment->vmsize = segment->vmsize;
        out_segment->fileoff = segment->fileoff;
        out_segment->filesize = segment->filesize;
        out_segment->maxprot = segment->maxprot;
        out_segment->initprot = segment->initprot;
      }

      {
        const PantheraSection64 *section =
          (const PantheraSection64 *)(cursor + sizeof(PantheraSegmentCommand64));
        uint32_t section_index;

        for (section_index = 0; section_index < segment->nsects; section_index++) {
          PantheraKernelSection *out_section;

          if (out_image->section_count >= PANTHERA_MAX_KERNEL_SECTIONS) {
            return PANTHERA_LOADER_E_TOO_MANY_SECTIONS;
          }

          out_section = &out_image->sections[out_image->section_count++];
          out_section->addr = section[section_index].addr;
          out_section->size = section[section_index].size;
        }
      }
    } else if (load_command->cmd == LC_DYSYMTAB) {
      const PantheraDysymtabCommand *dysymtab;

      if (load_command->cmdsize < sizeof(PantheraDysymtabCommand)) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      dysymtab = (const PantheraDysymtabCommand *)cursor;
      if ((uint64_t)dysymtab->locreloff +
          (uint64_t)dysymtab->nlocrel * sizeof(PantheraRelocationInfo) > image_size) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      out_image->local_reloc_offset = dysymtab->locreloff;
      out_image->local_reloc_count = dysymtab->nlocrel;
    } else if (load_command->cmd == LC_UNIXTHREAD) {
      const uint8_t *thread_payload;

      if (load_command->cmdsize < sizeof(PantheraLoadCommand) + sizeof(PantheraThreadStateHeader) + sizeof(PantheraX86ThreadState64)) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      thread_payload = cursor + sizeof(PantheraLoadCommand) + sizeof(PantheraThreadStateHeader);
      out_image->entry_pc = ((const PantheraX86ThreadState64 *)thread_payload)->rip;
    }

    cursor += load_command->cmdsize;
  }

  if (out_image->entry_pc == 0) {
    return PANTHERA_LOADER_E_MISSING_ENTRY;
  }

  return PANTHERA_LOADER_OK;
}

PantheraLoaderStatus
panthera_rebase_kernel_locals(
  const void *file_image,
  uint64_t file_size,
  const PantheraKernelImage *kernel_image,
  void *loaded_image,
  uint64_t loaded_base,
  uint32_t *applied_count_out
)
{
  const PantheraRelocationInfo *relocs;
  uint64_t hib_master_gdt_slot_vmaddr = 0;
  uint64_t vstart_master_gdt_slot_vmaddr = 0;
  uint64_t reloc_base_vmaddr = 0;
  uint64_t reloc_bytes;
  uint64_t slide;
  uint32_t applied_count = 0;
  uint32_t index;
  uint32_t segment_index;

  (void)loaded_image;

  if (file_image == 0 || kernel_image == 0 || loaded_base == 0) {
    return PANTHERA_LOADER_E_NULL;
  }

  if (applied_count_out != 0) {
    *applied_count_out = 0;
  }

  slide = loaded_base - (uint64_t)(uint32_t)kernel_image->lowest_vmaddr;
  if (slide == 0 || kernel_image->local_reloc_count == 0) {
    return PANTHERA_LOADER_OK;
  }

  reloc_bytes = (uint64_t)kernel_image->local_reloc_count * sizeof(PantheraRelocationInfo);
  if ((uint64_t)kernel_image->local_reloc_offset + reloc_bytes > file_size) {
    return PANTHERA_LOADER_E_TRUNCATED;
  }

  relocs = (const PantheraRelocationInfo *)((const uint8_t *)file_image + kernel_image->local_reloc_offset);
  for (segment_index = 0; segment_index < kernel_image->segment_count; segment_index++) {
    const PantheraKernelSegment *segment = &kernel_image->segments[segment_index];

    if (segment->segname[0] == '_' &&
        segment->segname[1] == '_' &&
        segment->segname[2] == 'D' &&
        segment->segname[3] == 'A' &&
        segment->segname[4] == 'T' &&
        segment->segname[5] == 'A' &&
        segment->segname[6] == '\0') {
      reloc_base_vmaddr = segment->vmaddr;
      break;
    }
  }
  if (reloc_base_vmaddr == 0) {
    return PANTHERA_LOADER_E_BAD_RELOC;
  }
  hib_master_gdt_slot_vmaddr = kernel_image->lowest_vmaddr + 0x1042U;
  vstart_master_gdt_slot_vmaddr = kernel_image->lowest_vmaddr + 0x99002U;

  for (index = 0; index < kernel_image->local_reloc_count; index++) {
    const PantheraRelocationInfo *reloc = &relocs[index];
    uint32_t info = reloc->info;
    uint32_t r_symbolnum = info & 0x00ffffffU;
    uint32_t r_pcrel = (info >> 24) & 0x1U;
    uint32_t r_length = (info >> 25) & 0x3U;
    uint32_t r_extern = (info >> 27) & 0x1U;
    uint32_t r_type = (info >> 28) & 0xfU;
    int32_t signed_address = (int32_t)reloc->address;
    const PantheraKernelSection *target_section;
    uint64_t patch_vmaddr;
    uint64_t *patch_slot;
    uint64_t candidate_value;

    if (r_symbolnum == 0 || r_symbolnum > kernel_image->section_count) {
      continue;
    }
    if (r_pcrel != 0 || r_extern != 0) {
      continue;
    }
    if (r_type != X86_64_RELOC_UNSIGNED || r_length != 3U) {
      continue;
    }

    target_section = &kernel_image->sections[r_symbolnum - 1U];
    patch_vmaddr = reloc_base_vmaddr;
    if (signed_address < 0) {
      patch_vmaddr -= (uint64_t)(-(int64_t)signed_address);
    } else {
      patch_vmaddr += (uint64_t)signed_address;
    }
    if (patch_vmaddr == hib_master_gdt_slot_vmaddr ||
        patch_vmaddr == vstart_master_gdt_slot_vmaddr) {
      continue;
    }
    patch_slot = (uint64_t *)panthera_loaded_vmaddr(
      kernel_image,
      loaded_base,
      patch_vmaddr,
      sizeof(uint64_t)
    );
    if (patch_slot == 0) {
      return PANTHERA_LOADER_E_BAD_RELOC;
    }
    candidate_value = *patch_slot;
    if (candidate_value < target_section->addr ||
        candidate_value >= target_section->addr + target_section->size) {
      return PANTHERA_LOADER_E_BAD_RELOC;
    }

    *patch_slot += slide;
    applied_count++;
  }

  if (applied_count_out != 0) {
    *applied_count_out = applied_count;
  }

  return PANTHERA_LOADER_OK;
}

PantheraLoaderStatus
panthera_slide_kernel_macho_metadata(
  const PantheraKernelImage *kernel_image,
  uint64_t loaded_base
)
{
  PantheraMachHeader64 *header;
  PantheraLoadCommand *load_command;
  PantheraSymtabCommand *symtab_command = 0;
  PantheraKernelSegment const *text_segment = 0;
  uint64_t slide;
  uint32_t segment_index;
  uint32_t command_index;

  if (kernel_image == 0 || loaded_base == 0) {
    return PANTHERA_LOADER_E_NULL;
  }

  slide = loaded_base - (uint64_t)(uint32_t)kernel_image->lowest_vmaddr;
  if (slide == 0) {
    return PANTHERA_LOADER_OK;
  }

  for (segment_index = 0; segment_index < kernel_image->segment_count; segment_index++) {
    const PantheraKernelSegment *segment = &kernel_image->segments[segment_index];

    if (segment->segname[0] == '_' &&
        segment->segname[1] == '_' &&
        segment->segname[2] == 'T' &&
        segment->segname[3] == 'E' &&
        segment->segname[4] == 'X' &&
        segment->segname[5] == 'T' &&
        segment->segname[6] == '\0') {
      text_segment = segment;
      break;
    }
  }
  if (text_segment == 0) {
    return PANTHERA_LOADER_E_BAD_RELOC;
  }

  header = (PantheraMachHeader64 *)(void *)panthera_loaded_vmaddr(
    kernel_image,
    loaded_base,
    text_segment->vmaddr,
    sizeof(PantheraMachHeader64)
  );
  if (header == 0) {
    return PANTHERA_LOADER_E_BAD_RELOC;
  }
  if (header->magic != MH_MAGIC_64) {
    return PANTHERA_LOADER_E_BAD_MAGIC;
  }
  if (panthera_loaded_vmaddr(
        kernel_image,
        loaded_base,
        text_segment->vmaddr,
        sizeof(PantheraMachHeader64) + (uint64_t)header->sizeofcmds
      ) == 0) {
    return PANTHERA_LOADER_E_TRUNCATED;
  }

  load_command = (PantheraLoadCommand *)(void *)((uint8_t *)header + sizeof(*header));
  for (command_index = 0; command_index < header->ncmds; command_index++) {
    if (load_command->cmdsize < sizeof(PantheraLoadCommand)) {
      return PANTHERA_LOADER_E_TRUNCATED;
    }

    if (load_command->cmd == LC_SEGMENT_64) {
      PantheraSegmentCommand64 *segment = (PantheraSegmentCommand64 *)(void *)load_command;
      PantheraSection64 *section = (PantheraSection64 *)(void *)((uint8_t *)segment + sizeof(*segment));
      uint32_t section_index;

      if (load_command->cmdsize < sizeof(PantheraSegmentCommand64) +
          (uint64_t)segment->nsects * sizeof(PantheraSection64)) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      segment->vmaddr += slide;
      for (section_index = 0; section_index < segment->nsects; section_index++) {
        uint64_t original_section_addr = section[section_index].addr;

        if (original_section_addr != 0) {
          section[section_index].addr = original_section_addr + slide;
        }

        if (panthera_name_equals(segment->segname, "__DATA_CONST") &&
            (panthera_name_equals(section[section_index].sectname, "__got") ||
             panthera_name_equals(section[section_index].sectname, "__mod_init_func"))) {
          uint64_t *pointer_slot = (uint64_t *)(void *)panthera_loaded_vmaddr(
            kernel_image,
            loaded_base,
            original_section_addr,
            section[section_index].size
          );
          uint64_t slot_index;
          uint64_t pointer_count;

          if (pointer_slot == 0) {
            return PANTHERA_LOADER_E_BAD_RELOC;
          }

          pointer_count = section[section_index].size / sizeof(uint64_t);
          for (slot_index = 0; slot_index < pointer_count; slot_index++) {
            uint64_t candidate_value = pointer_slot[slot_index];

            if (candidate_value >= kernel_image->lowest_vmaddr &&
                candidate_value < kernel_image->highest_vmaddr) {
              pointer_slot[slot_index] = candidate_value + slide;
            }
          }
        }
      }
    } else if (load_command->cmd == LC_UNIXTHREAD) {
      PantheraThreadStateHeader *thread_header =
        (PantheraThreadStateHeader *)(void *)((uint8_t *)load_command + sizeof(*load_command));
      PantheraX86ThreadState64 *thread_state =
        (PantheraX86ThreadState64 *)(void *)((uint8_t *)thread_header + sizeof(*thread_header));

      if (load_command->cmdsize < sizeof(PantheraLoadCommand) +
          sizeof(PantheraThreadStateHeader) + sizeof(PantheraX86ThreadState64)) {
        return PANTHERA_LOADER_E_TRUNCATED;
      }

      thread_state->rip += slide;
    } else if (load_command->cmd == LC_SYMTAB) {
      symtab_command = (PantheraSymtabCommand *)(void *)load_command;
    }

    load_command = (PantheraLoadCommand *)(void *)((uint8_t *)load_command + load_command->cmdsize);
  }

  if (symtab_command != 0 && symtab_command->nsyms != 0) {
    PantheraNList64 *symbols;
    uint64_t symbol_bytes =
      (uint64_t)symtab_command->nsyms * sizeof(PantheraNList64);
    uint32_t symbol_index;

    symbols = (PantheraNList64 *)(void *)panthera_loaded_file_offset(
      kernel_image,
      loaded_base,
      symtab_command->symoff,
      symbol_bytes
    );
    if (symbols == 0) {
      return PANTHERA_LOADER_E_BAD_RELOC;
    }

    for (symbol_index = 0; symbol_index < symtab_command->nsyms; symbol_index++) {
      PantheraNList64 *symbol = &symbols[symbol_index];

      if (!panthera_symbol_is_section_based(symbol->n_type)) {
        continue;
      }
      if (symbol->n_sect == 0 || symbol->n_value == 0) {
        continue;
      }
      if (symbol->n_value < kernel_image->lowest_vmaddr ||
          symbol->n_value >= kernel_image->highest_vmaddr) {
        continue;
      }

      symbol->n_value += slide;
    }
  }

  return PANTHERA_LOADER_OK;
}
