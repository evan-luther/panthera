#include "../include/panthera_uefi.h"
#include "../include/panthera_loader.h"
#include "../include/panthera_xnu_boot.h"

_Static_assert(sizeof(boot_args) == 4096, "XNU i386 boot_args ABI changed");

static PantheraKernelImage gKernelImage;
static EFI_PHYSICAL_ADDRESS gKernelImageBase;
static UINTN gKernelImageSize;
static UINTN gKernelReservedSize;
static EFI_PHYSICAL_ADDRESS gKernelTextBase;
static EFI_PHYSICAL_ADDRESS gKernelEntryBase;
static EFI_PHYSICAL_ADDRESS gKernelBootDataCursor;
static EFI_PHYSICAL_ADDRESS gBootArgsBase;
static EFI_PHYSICAL_ADDRESS gDeviceTreeBase;
static UINT32 gDeviceTreeSize;
static EFI_PHYSICAL_ADDRESS gMemoryMapBase;
static UINTN gMemoryMapCapacity;
static EFI_PHYSICAL_ADDRESS gSystemTableBase;
static EFI_PHYSICAL_ADDRESS gHandoffCodeBase;
static UINTN gHandoffCodeSize;
static EFI_PHYSICAL_ADDRESS gHandoffStackBase;
static UINT32 gBootKextCount;
static char gBootCommandLine[1024] = "serial=3 dataconstro=0 kernelmanagerd=0";
static UINT64 gFSBFrequencyHz = 100000000ULL;

/* GOP framebuffer info — queried early, before ExitBootServices */
static UINT64 gGopFrameBufferBase;
static UINT32 gGopWidth;
static UINT32 gGopHeight;
static UINT32 gGopPixelsPerScanLine;

enum {
  PANTHERA_HANDOFF_STACK_SIZE = 16384U,
  PANTHERA_MEMORY_MAP_BUFFER_SIZE = 1048576U,
  PANTHERA_DEVICE_TREE_MAX_SIZE = 65536U,
  PANTHERA_DEVICE_TREE_RANDOM_SEED_SIZE = 64U,
  PANTHERA_MAX_BOOT_KEXTS = 34U
};

#define PANTHERA_DEFAULT_BOOT_ARGS "serial=3 dataconstro=0 kernelmanagerd=0"

#define PANTHERA_FSB_FREQUENCY_HZ 100000000ULL
#define PANTHERA_TSC_CALIBRATION_USEC 100000ULL
#define PANTHERA_MIN_CALIBRATED_FREQUENCY_HZ 10000000ULL
#define PANTHERA_MAX_CALIBRATED_FREQUENCY_HZ 10000000000ULL
#define PANTHERA_BOOTSTRAP_MAP_LIMIT 0x3e800000ULL
#define PANTHERA_BOOTSTRAP_ALLOC_MAX_ADDRESS (PANTHERA_BOOTSTRAP_MAP_LIMIT - 1ULL)

extern const UINT8 panthera_handoff_template_begin[];
extern const UINT8 panthera_handoff_template_end[];
extern const UINT8 panthera_handoff_gdtr[];
extern const UINT8 panthera_handoff_gdt[];

typedef VOID (*PantheraHandoffEntry)(
  UINT32 boot_args_phys,
  UINT32 kernel_entry_phys,
  UINT32 stack_top_phys
);

typedef struct {
  UINT32 nProperties;
  UINT32 nChildren;
} PantheraDTNode;

typedef struct {
  char name[32];
  UINT32 length;
} PantheraDTProperty;

typedef struct {
  UINT32 paddr;
  UINT32 length;
} PantheraDeviceTreeBuffer;

typedef struct {
  UINT32 infoDictPhysAddr;
  UINT32 infoDictLength;
  UINT32 executablePhysAddr;
  UINT32 executableLength;
  UINT32 bundlePathPhysAddr;
  UINT32 bundlePathLength;
} PantheraBooterKextFileInfo;

typedef struct {
  const char *bundle_name;
  const char *bundle_path;
  const CHAR16 *info_path;
  const CHAR16 *executable_path;
  char driver_entry_name[32];
  const char *required_boot_arg;
} PantheraBootKextManifestEntry;

typedef struct {
  PantheraDeviceTreeBuffer device_tree_buffer;
  const char *driver_entry_name;
  EFI_PHYSICAL_ADDRESS file_info_base;
  EFI_PHYSICAL_ADDRESS info_dict_base;
  EFI_PHYSICAL_ADDRESS executable_base;
  EFI_PHYSICAL_ADDRESS bundle_path_base;
  UINTN info_dict_size;
  UINTN executable_size;
  UINTN bundle_path_size;
} PantheraBootKextRecord;

typedef struct {
  UINT8 maxBusNum;
  UINT8 majorVersion;
  UINT8 minorVersion;
  UINT8 BIOSPresent;
  UINT8 busFlags;
  UINT8 reserved[3];
} PantheraPCIBusInfo;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
static const PantheraBootKextManifestEntry gBootKextManifest[PANTHERA_MAX_BOOT_KEXTS] = {
  {
    "SystemKernel",
    "/System/Library/Extensions/System.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-SystemKernel"
  },
  {
    "BSDKernel",
    "/System/Library/Extensions/System.kext/PlugIns/BSDKernel.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\PlugIns\\BSDKernel.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-BSDKernel"
  },
  {
    "IOKitKPI",
    "/System/Library/Extensions/System.kext/PlugIns/IOKit.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\PlugIns\\IOKit.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-IOKitKPI"
  },
  {
    "LibkernKPI",
    "/System/Library/Extensions/System.kext/PlugIns/Libkern.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\PlugIns\\Libkern.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-LibkernKPI"
  },
  {
    "MachKPI",
    "/System/Library/Extensions/System.kext/PlugIns/Mach.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\PlugIns\\Mach.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-MachKPI"
  },
  {
    "PrivateKPI",
    "/System/Library/Extensions/System.kext/PlugIns/Private.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\PlugIns\\Private.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-PrivateKPI"
  },
  {
    "UnsupportedKPI",
    "/System/Library/Extensions/System.kext/PlugIns/Unsupported.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\System.kext\\PlugIns\\Unsupported.kext\\Info.plist",
    (const CHAR16 *)0,
    "Driver-UnsupportedKPI"
  },
  {
    "corecrypto",
    "/System/Library/Extensions/corecrypto.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\corecrypto.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\corecrypto.kext\\Contents\\MacOS\\corecrypto",
    "Driver-corecrypto",
    "panthera_zfs_spl=1"
  },
  {
    "spl",
    "/System/Library/Extensions/spl.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\spl.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\spl.kext\\Contents\\MacOS\\spl",
    "Driver-spl",
    "panthera_zfs_spl=1"
  },
  {
    "zfs",
    "/System/Library/Extensions/zfs.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\zfs.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\zfs.kext\\Contents\\MacOS\\zfs",
    "Driver-zfs",
    "panthera_zfs=1"
  },
  {
    "AppleAPIC",
    "/System/Library/Extensions/AppleAPIC.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleAPIC.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleAPIC.kext\\Contents\\MacOS\\AppleAPIC",
    "Driver-AppleAPIC"
  },
  {
    "AppleSMBIOS",
    "/System/Library/Extensions/AppleSMBIOS.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleSMBIOS.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleSMBIOS.kext\\Contents\\MacOS\\AppleSMBIOS",
    "Driver-AppleSMBIOS"
  },
  {
    "AppleI386PCI",
    "/System/Library/Extensions/AppleI386PCI.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleI386PCI.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleI386PCI.kext\\Contents\\MacOS\\AppleI386PCI",
    "Driver-AppleI386PCI"
  },
  {
    "AppleI386GenericPlatform",
    "/System/Library/Extensions/AppleI386GenericPlatform.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleI386GenericPlatform.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleI386GenericPlatform.kext\\Contents\\MacOS\\AppleI386GenericPlatform",
    "Driver-AppleI386GenericPlatform"
  },
  {
    "IOACPIFamily",
    "/System/Library/Extensions/IOACPIFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOACPIFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOACPIFamily.kext\\Contents\\MacOS\\IOACPIFamily",
    "Driver-IOACPIFamily"
  },
  {
    "IOPCIFamily",
    "/System/Library/Extensions/IOPCIFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOPCIFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOPCIFamily.kext\\Contents\\MacOS\\IOPCIFamily",
    "Driver-IOPCIFamily"
  },
  {
    "IOHIDFamily",
    "/System/Library/Extensions/IOHIDFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOHIDFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOHIDFamily.kext\\Contents\\MacOS\\IOHIDFamily",
    "Driver-IOHIDFamily"
  },
  {
    "IOUSBFamily",
    "/System/Library/Extensions/IOUSBFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOUSBFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOUSBFamily.kext\\Contents\\MacOS\\IOUSBFamily",
    "Driver-IOUSBFamily",
    "panthera_usbhid=1"
  },
  {
    "AppleUSBEHCI",
    "/System/Library/Extensions/AppleUSBEHCI.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleUSBEHCI.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleUSBEHCI.kext\\Contents\\MacOS\\AppleUSBEHCI",
    "Driver-AppleUSBEHCI",
    "panthera_usbhid=1"
  },
  {
    "AppleUSBUHCI",
    "/System/Library/Extensions/AppleUSBUHCI.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleUSBUHCI.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleUSBUHCI.kext\\Contents\\MacOS\\AppleUSBUHCI",
    "Driver-AppleUSBUHCI",
    "panthera_usbhid=1"
  },
  {
    "AppleUSBHub",
    "/System/Library/Extensions/AppleUSBHub.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleUSBHub.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleUSBHub.kext\\Contents\\MacOS\\AppleUSBHub",
    "Driver-AppleUSBHub",
    "panthera_usbhid=1"
  },
  {
    "IOUSBCompositeDriver",
    "/System/Library/Extensions/IOUSBCompositeDriver.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOUSBCompositeDriver.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOUSBCompositeDriver.kext\\Contents\\MacOS\\IOUSBCompositeDriver",
    "Driver-IOUSBCompositeDriver",
    "panthera_usbhid=1"
  },
  {
    "IOUSBHIDDriver",
    "/System/Library/Extensions/IOUSBHIDDriver.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOUSBHIDDriver.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOUSBHIDDriver.kext\\Contents\\MacOS\\IOUSBHIDDriver",
    "Driver-IOUSBHIDDriver",
    "panthera_usbhid=1"
  },
  {
    "IONetworkingFamily",
    "/System/Library/Extensions/IONetworkingFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IONetworkingFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IONetworkingFamily.kext\\Contents\\MacOS\\IONetworkingFamily",
    "Driver-IONetworkingFamily"
  },
  {
    "AppleRTL8139Ethernet",
    "/System/Library/Extensions/AppleRTL8139Ethernet.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleRTL8139Ethernet.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleRTL8139Ethernet.kext\\Contents\\MacOS\\AppleRTL8139Ethernet",
    "Driver-AppleRTL8139Ethernet"
  },
  {
    "IOHIDSystem",
    "/System/Library/Extensions/IOHIDSystem.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOHIDSystem.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOHIDSystem.kext\\Contents\\MacOS\\IOHIDSystem",
    "Driver-IOHIDSystem"
  },
  {
    "IOStorageFamily",
    "/System/Library/Extensions/IOStorageFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOStorageFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOStorageFamily.kext\\Contents\\MacOS\\IOStorageFamily",
    "Driver-IOStorageFamily"
  },
  {
    "HFSEncodings",
    "/System/Library/Extensions/HFSEncodings.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\HFSEncodings.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\HFSEncodings.kext\\Contents\\MacOS\\HFSEncodings",
    "Driver-HFSEncodings"
  },
  {
    "HFS",
    "/System/Library/Extensions/HFS.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\HFS.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\HFS.kext\\Contents\\MacOS\\HFS",
    "Driver-HFS"
  },
  {
    "IOATAFamily",
    "/System/Library/Extensions/IOATAFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOATAFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOATAFamily.kext\\Contents\\MacOS\\IOATAFamily",
    "Driver-IOATAFamily"
  },
  {
    "AppleIntelPIIXATA",
    "/System/Library/Extensions/AppleIntelPIIXATA.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleIntelPIIXATA.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\AppleIntelPIIXATA.kext\\Contents\\MacOS\\AppleIntelPIIXATA",
    "Driver-AppleIntelPIIXATA"
  },
  {
    "PantheraATAStorage",
    "/System/Library/Extensions/PantheraATAStorage.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\PantheraATAStorage.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\PantheraATAStorage.kext\\Contents\\MacOS\\PantheraATAStorage",
    "Driver-PantheraATAStorage"
  },
  {
    "IOGraphicsFamily",
    "/System/Library/Extensions/IOGraphicsFamily.kext",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOGraphicsFamily.kext\\Contents\\Info.plist",
    (const CHAR16 *)u"\\System\\Library\\Extensions\\IOGraphicsFamily.kext\\Contents\\MacOS\\IOGraphicsFamily",
    "Driver-IOGraphicsFamily",
    "panthera_iog=1"
  },
  { 0 }
};
#pragma clang diagnostic pop

static PantheraBootKextRecord gBootKextRecords[PANTHERA_MAX_BOOT_KEXTS];

static EFI_STATUS
panthera_ensure_low32(EFI_PHYSICAL_ADDRESS address);

static UINTN
panthera_align_page(UINTN value);

static UINTN
panthera_ascii_length(const char *text)
{
  UINTN length = 0;

  while (text[length] != '\0') {
    length++;
  }

  return length;
}

static int
panthera_ascii_is_space(char c)
{
  return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

static int
panthera_command_line_has_token(const char *command_line, const char *token)
{
  UINTN token_length = panthera_ascii_length(token);
  const char *cursor = command_line;

  if (token_length == 0) {
    return 0;
  }

  while (*cursor != '\0') {
    const char *token_start;
    UINTN index = 0;

    while (panthera_ascii_is_space(*cursor)) {
      cursor++;
    }
    if (*cursor == '\0') {
      break;
    }

    token_start = cursor;
    while (*cursor != '\0' && !panthera_ascii_is_space(*cursor)) {
      cursor++;
    }

    while (index < token_length && token_start[index] == token[index]) {
      index++;
    }
    if (index == token_length && token_start + index == cursor) {
      return 1;
    }
  }

  return 0;
}

static EFI_STATUS
console_write(EFI_SYSTEM_TABLE *system_table, const CHAR16 *text)
{
  if (system_table == 0 || system_table->ConOut == 0 || system_table->ConOut->OutputString == 0) {
    return EFI_SUCCESS;
  }

  return system_table->ConOut->OutputString(system_table->ConOut, (CHAR16 *)text);
}

static EFI_STATUS
console_write_ascii(EFI_SYSTEM_TABLE *system_table, const char *text)
{
  CHAR16 buffer[192];
  UINTN index = 0;

  while (text[index] != '\0' && index + 1 < sizeof(buffer) / sizeof(buffer[0])) {
    buffer[index] = (CHAR16)(UINT8)text[index];
    index++;
  }
  buffer[index] = 0;

  return console_write(system_table, buffer);
}

static EFI_STATUS
console_write_hex64(EFI_SYSTEM_TABLE *system_table, const char *label, UINT64 value)
{
  static const char hex_digits[] = "0123456789abcdef";
  CHAR16 buffer[96];
  UINTN index = 0;
  UINTN label_length = panthera_ascii_length(label);
  UINTN nibble;

  while (index < label_length && index + 1 < sizeof(buffer) / sizeof(buffer[0])) {
    buffer[index] = (CHAR16)(UINT8)label[index];
    index++;
  }

  if (index + 19 >= sizeof(buffer) / sizeof(buffer[0])) {
    return EFI_OUT_OF_RESOURCES;
  }

  buffer[index++] = '0';
  buffer[index++] = 'x';
  for (nibble = 0; nibble < 16; nibble++) {
    UINTN shift = (15 - nibble) * 4;
    buffer[index++] = (CHAR16)hex_digits[(value >> shift) & 0xfU];
  }
  buffer[index++] = '\r';
  buffer[index++] = '\n';
  buffer[index] = 0;

  return console_write(system_table, buffer);
}

static EFI_STATUS
console_write_uint64(EFI_SYSTEM_TABLE *system_table, const char *label, UINT64 value)
{
  CHAR16 buffer[96];
  CHAR16 digits[24];
  UINTN index = 0;
  UINTN label_length = panthera_ascii_length(label);
  UINTN digit_count = 0;

  while (index < label_length && index + 1 < sizeof(buffer) / sizeof(buffer[0])) {
    buffer[index] = (CHAR16)(UINT8)label[index];
    index++;
  }

  if (value == 0) {
    digits[digit_count++] = '0';
  } else {
    while (value != 0 && digit_count < sizeof(digits) / sizeof(digits[0])) {
      digits[digit_count++] = (CHAR16)('0' + (value % 10));
      value /= 10;
    }
  }

  while (digit_count > 0 && index + 1 < sizeof(buffer) / sizeof(buffer[0])) {
    buffer[index++] = digits[--digit_count];
  }
  buffer[index++] = '\r';
  buffer[index++] = '\n';
  buffer[index] = 0;

  return console_write(system_table, buffer);
}

static UINT64
panthera_read_tsc(void)
{
  UINT32 lo;
  UINT32 hi;

  __asm__ __volatile__("lfence; rdtsc" : "=a"(lo), "=d"(hi) :: "memory");
  return ((UINT64)hi << 32) | lo;
}

static UINT64
panthera_calibrate_tsc_frequency(EFI_SYSTEM_TABLE *system_table)
{
  typedef EFI_STATUS(EFIAPI *EFI_STALL)(UINTN Microseconds);
  EFI_STALL stall;
  UINT64 start;
  UINT64 end;
  UINT64 delta;
  UINT64 frequency;

  if (system_table == 0 || system_table->BootServices == 0 ||
      system_table->BootServices->Stall == 0) {
    return PANTHERA_FSB_FREQUENCY_HZ;
  }

  stall = (EFI_STALL)system_table->BootServices->Stall;
  start = panthera_read_tsc();
  if (EFI_ERROR(stall(PANTHERA_TSC_CALIBRATION_USEC))) {
    return PANTHERA_FSB_FREQUENCY_HZ;
  }
  end = panthera_read_tsc();

  if (end <= start) {
    return PANTHERA_FSB_FREQUENCY_HZ;
  }

  delta = end - start;
  frequency = (delta * 1000000ULL) / PANTHERA_TSC_CALIBRATION_USEC;
  if (frequency < PANTHERA_MIN_CALIBRATED_FREQUENCY_HZ ||
      frequency > PANTHERA_MAX_CALIBRATED_FREQUENCY_HZ) {
    return PANTHERA_FSB_FREQUENCY_HZ;
  }

  return frequency;
}

static EFI_STATUS
panthera_handle_protocol(
  EFI_SYSTEM_TABLE *system_table,
  EFI_HANDLE handle,
  EFI_GUID *protocol,
  VOID **interface_out
)
{
  if (system_table == 0 || system_table->BootServices == 0 || system_table->BootServices->HandleProtocol == 0) {
    return EFI_NOT_FOUND;
  }

  return system_table->BootServices->HandleProtocol(handle, protocol, interface_out);
}

static EFI_STATUS
panthera_allocate_pool(
  EFI_SYSTEM_TABLE *system_table,
  UINTN size,
  VOID **buffer_out
)
{
  if (system_table == 0 || system_table->BootServices == 0 || system_table->BootServices->AllocatePool == 0) {
    return EFI_OUT_OF_RESOURCES;
  }

  return system_table->BootServices->AllocatePool(EFI_LOADER_DATA, size, buffer_out);
}

static EFI_STATUS
panthera_allocate_pages(
  EFI_SYSTEM_TABLE *system_table,
  UINTN size,
  EFI_PHYSICAL_ADDRESS *address_out
)
{
  UINTN pages = (size + 4095U) / 4096U;
  EFI_STATUS status;

  if (system_table == 0 || system_table->BootServices == 0 || system_table->BootServices->AllocatePages == 0) {
    return EFI_OUT_OF_RESOURCES;
  }

  *address_out = PANTHERA_BOOTSTRAP_ALLOC_MAX_ADDRESS;
  status = system_table->BootServices->AllocatePages(
    EFI_ALLOCATE_MAX_ADDRESS,
    EFI_LOADER_DATA,
    pages,
    address_out
  );
  if (!EFI_ERROR(status)) {
    return EFI_SUCCESS;
  }

  *address_out = 0;
  return system_table->BootServices->AllocatePages(
    EFI_ALLOCATE_ANY_PAGES,
    EFI_LOADER_DATA,
    pages,
    address_out
  );
}

static EFI_STATUS
panthera_allocate_pages_at(
  EFI_SYSTEM_TABLE *system_table,
  UINTN size,
  EFI_PHYSICAL_ADDRESS *address_in_out
)
{
  UINTN pages = (size + 4095U) / 4096U;

  if (system_table == 0 || system_table->BootServices == 0 || system_table->BootServices->AllocatePages == 0) {
    return EFI_OUT_OF_RESOURCES;
  }

  return system_table->BootServices->AllocatePages(
    EFI_ALLOCATE_ADDRESS,
    EFI_LOADER_DATA,
    pages,
    address_in_out
  );
}

static VOID
panthera_free_pool(EFI_SYSTEM_TABLE *system_table, VOID *buffer)
{
  if (buffer != 0 && system_table != 0 && system_table->BootServices != 0 && system_table->BootServices->FreePool != 0) {
    (void)system_table->BootServices->FreePool(buffer);
  }
}

static VOID
panthera_copy_memory(VOID *dst, const VOID *src, UINTN size)
{
  UINT8 *dst_bytes = (UINT8 *)dst;
  const UINT8 *src_bytes = (const UINT8 *)src;
  UINTN index;

  for (index = 0; index < size; index++) {
    dst_bytes[index] = src_bytes[index];
  }
}

static VOID
panthera_zero_memory(VOID *dst, UINTN size)
{
  UINT8 *dst_bytes = (UINT8 *)dst;
  UINTN index;

  for (index = 0; index < size; index++) {
    dst_bytes[index] = 0;
  }
}

static EFI_STATUS
panthera_allocate_boot_args(
  EFI_SYSTEM_TABLE *system_table,
  boot_args **boot_args_out,
  EFI_PHYSICAL_ADDRESS *physical_base_out
)
{
  EFI_STATUS status;
  EFI_PHYSICAL_ADDRESS physical_base = 0;
  boot_args *args;

  status = panthera_allocate_pages(system_table, sizeof(boot_args), &physical_base);
  if (EFI_ERROR(status)) {
    return status;
  }

  args = (boot_args *)(UINTN)physical_base;
  panthera_zero_memory(args, sizeof(boot_args));
  *boot_args_out = args;
  *physical_base_out = physical_base;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_allocate_buffer_pages(
  EFI_SYSTEM_TABLE *system_table,
  UINTN size,
  VOID **buffer_out,
  EFI_PHYSICAL_ADDRESS *physical_base_out
)
{
  EFI_STATUS status;
  EFI_PHYSICAL_ADDRESS physical_base = 0;

  status = panthera_allocate_pages(system_table, size, &physical_base);
  if (EFI_ERROR(status)) {
    return status;
  }

  *buffer_out = (VOID *)(UINTN)physical_base;
  *physical_base_out = physical_base;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_allocate_kernel_boot_data_pages(
  EFI_SYSTEM_TABLE *system_table,
  UINTN size,
  VOID **buffer_out,
  EFI_PHYSICAL_ADDRESS *physical_base_out
)
{
  EFI_STATUS status;
  EFI_PHYSICAL_ADDRESS physical_base;
  UINTN aligned_size = panthera_align_page(size == 0 ? 1U : size);

  if (gKernelBootDataCursor == 0) {
    return EFI_NOT_FOUND;
  }

  physical_base = gKernelBootDataCursor;
  status = panthera_allocate_pages_at(system_table, aligned_size, &physical_base);
  if (EFI_ERROR(status)) {
    return status;
  }

  *buffer_out = (VOID *)(UINTN)physical_base;
  *physical_base_out = physical_base;
  gKernelBootDataCursor = physical_base + aligned_size;
  gKernelReservedSize = (UINTN)(gKernelBootDataCursor - gKernelTextBase);
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_copy_buffer_to_kernel_boot_data(
  EFI_SYSTEM_TABLE *system_table,
  const VOID *src,
  UINTN size,
  EFI_PHYSICAL_ADDRESS *physical_base_out
)
{
  EFI_STATUS status;
  VOID *buffer = 0;

  status = panthera_allocate_kernel_boot_data_pages(
    system_table,
    size == 0 ? 1U : size,
    &buffer,
    physical_base_out
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  panthera_zero_memory(buffer, size == 0 ? 1U : size);
  if (size != 0 && src != 0) {
    panthera_copy_memory(buffer, src, size);
  }
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_copy_ascii_to_kernel_boot_data(
  EFI_SYSTEM_TABLE *system_table,
  const char *text,
  EFI_PHYSICAL_ADDRESS *physical_base_out,
  UINTN *size_out
)
{
  UINTN size = panthera_ascii_length(text) + 1U;
  EFI_STATUS status;

  status = panthera_copy_buffer_to_kernel_boot_data(system_table, text, size, physical_base_out);
  if (EFI_ERROR(status)) {
    return status;
  }

  *size_out = size;
  return EFI_SUCCESS;
}

static VOID
panthera_copy_ascii(char *dst, UINTN dst_size, const char *src)
{
  UINTN index = 0;

  if (dst_size == 0) {
    return;
  }

  while (src[index] != '\0' && index + 1 < dst_size) {
    dst[index] = src[index];
    index++;
  }
  dst[index] = '\0';
}

static UINTN
panthera_align_4(UINTN value)
{
  return (value + 3U) & ~(UINTN)3U;
}

static UINTN
panthera_align_page(UINTN value)
{
  return (value + 4095U) & ~(UINTN)4095U;
}

static EFI_STATUS
panthera_dt_append_property(
  UINT8 *buffer,
  UINTN capacity,
  UINTN *offset_in_out,
  const char *name,
  const VOID *value,
  UINT32 value_size
)
{
  UINTN property_size = sizeof(PantheraDTProperty) + panthera_align_4(value_size);
  PantheraDTProperty *property;

  if (*offset_in_out + property_size > capacity) {
    return EFI_OUT_OF_RESOURCES;
  }

  property = (PantheraDTProperty *)(VOID *)(buffer + *offset_in_out);
  panthera_zero_memory(property, property_size);
  panthera_copy_ascii(property->name, sizeof(property->name), name);
  property->length = value_size;
  if (value != 0 && value_size != 0) {
    panthera_copy_memory(property + 1, value, value_size);
  }

  *offset_in_out += property_size;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_dt_append_name(
  UINT8 *buffer,
  UINTN capacity,
  UINTN *offset_in_out,
  const char *name
)
{
  return panthera_dt_append_property(
    buffer,
    capacity,
    offset_in_out,
    "name",
    name,
    (UINT32)(panthera_ascii_length(name) + 1U)
  );
}

static EFI_STATUS
panthera_build_synthetic_device_tree(EFI_SYSTEM_TABLE *system_table)
{
  static const UINT8 random_seed[PANTHERA_DEVICE_TREE_RANDOM_SEED_SIZE] = {
    0x50, 0x41, 0x4e, 0x54, 0x48, 0x45, 0x52, 0x41,
    0x10, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
    0x31, 0x42, 0x53, 0x64, 0x75, 0x86, 0x97, 0xa8,
    0xb9, 0xca, 0xdb, 0xec, 0xfd, 0x0e, 0x1f, 0x2a,
    0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f, 0xf0, 0xe1,
    0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87, 0x78, 0x69,
    0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc
  };
  static const PantheraPCIBusInfo pci_bus_info = {
    0xffU,
    2U,
    0U,
    0U,
    0x01U,
    { 0U, 0U, 0U }
  };
  EFI_STATUS status;
  VOID *buffer = 0;
  UINT8 *bytes;
  UINTN offset = 0;
  PantheraDTNode *node;

  status = panthera_allocate_buffer_pages(
    system_table,
    PANTHERA_DEVICE_TREE_MAX_SIZE,
    &buffer,
    &gDeviceTreeBase
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  panthera_zero_memory(buffer, PANTHERA_DEVICE_TREE_MAX_SIZE);
  bytes = (UINT8 *)buffer;

  node = (PantheraDTNode *)(VOID *)(bytes + offset);
  node->nProperties = 1;
  node->nChildren = 3;
  offset += sizeof(*node);
  status = panthera_dt_append_name(bytes, PANTHERA_DEVICE_TREE_MAX_SIZE, &offset, "/");
  if (EFI_ERROR(status)) {
    return status;
  }

  node = (PantheraDTNode *)(VOID *)(bytes + offset);
  node->nProperties = 2;
  node->nChildren = 1;
  offset += sizeof(*node);
  status = panthera_dt_append_name(bytes, PANTHERA_DEVICE_TREE_MAX_SIZE, &offset, "chosen");
  if (EFI_ERROR(status)) {
    return status;
  }
  status = panthera_dt_append_property(
    bytes,
    PANTHERA_DEVICE_TREE_MAX_SIZE,
    &offset,
    "random-seed",
    random_seed,
    sizeof(random_seed)
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  node = (PantheraDTNode *)(VOID *)(bytes + offset);
  node->nProperties = 1 + gBootKextCount;
  node->nChildren = 0;
  offset += sizeof(*node);
  status = panthera_dt_append_name(bytes, PANTHERA_DEVICE_TREE_MAX_SIZE, &offset, "memory-map");
  if (EFI_ERROR(status)) {
    return status;
  }
  {
    UINT32 kext_index;

    for (kext_index = 0; kext_index < gBootKextCount; kext_index++) {
      status = panthera_dt_append_property(
        bytes,
        PANTHERA_DEVICE_TREE_MAX_SIZE,
        &offset,
        gBootKextRecords[kext_index].driver_entry_name,
        &gBootKextRecords[kext_index].device_tree_buffer,
        sizeof(gBootKextRecords[kext_index].device_tree_buffer)
      );
      if (EFI_ERROR(status)) {
        return status;
      }
    }
  }

  node = (PantheraDTNode *)(VOID *)(bytes + offset);
  node->nProperties = 1;
  node->nChildren = 1;
  offset += sizeof(*node);
  status = panthera_dt_append_name(bytes, PANTHERA_DEVICE_TREE_MAX_SIZE, &offset, "efi");
  if (EFI_ERROR(status)) {
    return status;
  }

  node = (PantheraDTNode *)(VOID *)(bytes + offset);
  node->nProperties = 2;
  node->nChildren = 0;
  offset += sizeof(*node);
  status = panthera_dt_append_name(bytes, PANTHERA_DEVICE_TREE_MAX_SIZE, &offset, "platform");
  if (EFI_ERROR(status)) {
    return status;
  }
  status = panthera_dt_append_property(
    bytes,
    PANTHERA_DEVICE_TREE_MAX_SIZE,
    &offset,
    "FSBFrequency",
    &gFSBFrequencyHz,
    sizeof(gFSBFrequencyHz)
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  node = (PantheraDTNode *)(VOID *)(bytes + offset);
  node->nProperties = 2;
  node->nChildren = 0;
  offset += sizeof(*node);
  status = panthera_dt_append_name(bytes, PANTHERA_DEVICE_TREE_MAX_SIZE, &offset, "pci");
  if (EFI_ERROR(status)) {
    return status;
  }
  status = panthera_dt_append_property(
    bytes,
    PANTHERA_DEVICE_TREE_MAX_SIZE,
    &offset,
    "pci-bus-info",
    &pci_bus_info,
    sizeof(pci_bus_info)
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  gDeviceTreeSize = (UINT32)offset;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_capture_system_table(
  EFI_SYSTEM_TABLE *system_table,
  EFI_SYSTEM_TABLE **system_table_copy_out,
  EFI_PHYSICAL_ADDRESS *system_table_base_out
)
{
  EFI_STATUS status;
  UINTN config_table_bytes;
  UINTN total_size;
  VOID *buffer = 0;
  EFI_PHYSICAL_ADDRESS physical_base = 0;
  EFI_SYSTEM_TABLE *copied_system_table;
  EFI_CONFIGURATION_TABLE *copied_config_table;

  config_table_bytes = system_table->NumberOfTableEntries * sizeof(EFI_CONFIGURATION_TABLE);
  total_size = sizeof(EFI_SYSTEM_TABLE) + config_table_bytes;

  status = panthera_allocate_buffer_pages(system_table, total_size, &buffer, &physical_base);
  if (EFI_ERROR(status)) {
    return status;
  }

  copied_system_table = (EFI_SYSTEM_TABLE *)buffer;
  panthera_copy_memory(copied_system_table, system_table, sizeof(EFI_SYSTEM_TABLE));
  copied_config_table = (EFI_CONFIGURATION_TABLE *)((UINT8 *)buffer + sizeof(EFI_SYSTEM_TABLE));
  if (config_table_bytes != 0) {
    panthera_copy_memory(
      copied_config_table,
      system_table->ConfigurationTable,
      config_table_bytes
    );
  }
  copied_system_table->ConfigurationTable = copied_config_table;

  *system_table_copy_out = copied_system_table;
  *system_table_base_out = physical_base;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_load_kernel_file(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE *system_table,
  const CHAR16 *path,
  VOID **buffer_out,
  UINTN *size_out
)
{
  EFI_STATUS status;
  EFI_LOADED_IMAGE_PROTOCOL *loaded_image = 0;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *file_system = 0;
  EFI_FILE_PROTOCOL *root = 0;
  EFI_FILE_PROTOCOL *file = 0;
  EFI_FILE_INFO *file_info = 0;
  VOID *file_buffer = 0;
  UINTN file_info_size = 0;
  UINTN file_size;

  *buffer_out = 0;
  *size_out = 0;

  status = panthera_handle_protocol(
    system_table,
    image_handle,
    (EFI_GUID *)&gEfiLoadedImageProtocolGuid,
    (VOID **)&loaded_image
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_handle_protocol(
    system_table,
    loaded_image->DeviceHandle,
    (EFI_GUID *)&gEfiSimpleFileSystemProtocolGuid,
    (VOID **)&file_system
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = file_system->OpenVolume(file_system, &root);
  if (EFI_ERROR(status)) {
    return status;
  }

  status = root->Open(root, &file, (CHAR16 *)path, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status)) {
    root->Close(root);
    return status;
  }

  status = file->GetInfo(file, (EFI_GUID *)&gEfiFileInfoGuid, &file_info_size, 0);
  if (status != EFI_BUFFER_TOO_SMALL || file_info_size == 0) {
    file->Close(file);
    root->Close(root);
    return status;
  }

  status = panthera_allocate_pool(system_table, file_info_size, (VOID **)&file_info);
  if (EFI_ERROR(status)) {
    file->Close(file);
    root->Close(root);
    return status;
  }

  status = file->GetInfo(file, (EFI_GUID *)&gEfiFileInfoGuid, &file_info_size, file_info);
  if (EFI_ERROR(status)) {
    panthera_free_pool(system_table, file_info);
    file->Close(file);
    root->Close(root);
    return status;
  }

  file_size = (UINTN)file_info->FileSize;
  status = panthera_allocate_pool(system_table, file_size, &file_buffer);
  if (EFI_ERROR(status)) {
    panthera_free_pool(system_table, file_info);
    file->Close(file);
    root->Close(root);
    return status;
  }

  status = file->Read(file, &file_size, file_buffer);
  if (EFI_ERROR(status) || file_size != (UINTN)file_info->FileSize) {
    panthera_free_pool(system_table, file_buffer);
    panthera_free_pool(system_table, file_info);
    file->Close(file);
    root->Close(root);
    return EFI_NOT_FOUND;
  }

  panthera_free_pool(system_table, file_info);
  file->Close(file);
  root->Close(root);

  *buffer_out = file_buffer;
  *size_out = file_size;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_try_load_kernel(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE *system_table,
  VOID **buffer_out,
  UINTN *size_out
)
{
  EFI_STATUS status;

  status = panthera_load_kernel_file(image_handle, system_table, (const CHAR16 *)u"\\mach_kernel", buffer_out, size_out);
  if (!EFI_ERROR(status)) {
    console_write_ascii(system_table, "Loaded kernel path: \\mach_kernel\r\n");
    return EFI_SUCCESS;
  }

  status = panthera_load_kernel_file(image_handle, system_table, (const CHAR16 *)u"\\EFI\\PANTHERA\\kernel", buffer_out, size_out);
  if (!EFI_ERROR(status)) {
    console_write_ascii(system_table, "Loaded kernel path: \\EFI\\PANTHERA\\kernel\r\n");
    return EFI_SUCCESS;
  }

  return status;
}

static EFI_STATUS
panthera_load_boot_command_line(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE *system_table
)
{
  EFI_STATUS status;
  VOID *file_buffer = 0;
  UINTN file_size = 0;
  UINTN src_index = 0;
  UINTN dst_index = 0;
  int previous_was_space = 1;
  const char *source_label = "built-in";

  panthera_copy_ascii(gBootCommandLine, sizeof(gBootCommandLine), PANTHERA_DEFAULT_BOOT_ARGS);

  status = panthera_load_kernel_file(
    image_handle,
    system_table,
    (const CHAR16 *)u"\\EFI\\PANTHERA\\boot-args.txt",
    &file_buffer,
    &file_size
  );
  if (!EFI_ERROR(status) && file_buffer != 0 && file_size != 0) {
    const UINT8 *src = (const UINT8 *)file_buffer;

    panthera_zero_memory(gBootCommandLine, sizeof(gBootCommandLine));
    while (src_index < file_size && dst_index + 1 < sizeof(gBootCommandLine)) {
      char next = (char)src[src_index++];
      if ((UINT8)next < 0x20U) {
        next = ' ';
      }
      if (next == ' ') {
        if (previous_was_space) {
          continue;
        }
        previous_was_space = 1;
      } else {
        previous_was_space = 0;
      }
      gBootCommandLine[dst_index++] = next;
    }
    while (dst_index > 0 && gBootCommandLine[dst_index - 1] == ' ') {
      dst_index--;
    }
    gBootCommandLine[dst_index] = '\0';
    if (dst_index == 0) {
      panthera_copy_ascii(gBootCommandLine, sizeof(gBootCommandLine), PANTHERA_DEFAULT_BOOT_ARGS);
    } else {
      source_label = "\\EFI\\PANTHERA\\boot-args.txt";
    }
  }
  if (file_buffer != 0) {
    panthera_free_pool(system_table, file_buffer);
  }

  console_write_ascii(system_table, "Boot args source: ");
  console_write_ascii(system_table, source_label);
  console_write(system_table, (const CHAR16 *)u"\r\n");
  console_write_ascii(system_table, "Boot args: ");
  console_write_ascii(system_table, gBootCommandLine);
  console_write(system_table, (const CHAR16 *)u"\r\n");

  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_load_file_to_pages(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE *system_table,
  const CHAR16 *path,
  UINTN trailing_zero_bytes,
  EFI_PHYSICAL_ADDRESS *physical_base_out,
  UINTN *size_out
)
{
  EFI_STATUS status;
  VOID *pool_buffer = 0;
  UINTN pool_size = 0;
  UINTN final_size;
  VOID *page_buffer = 0;

  status = panthera_load_kernel_file(
    image_handle,
    system_table,
    path,
    &pool_buffer,
    &pool_size
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  final_size = pool_size + trailing_zero_bytes;
  status = panthera_allocate_kernel_boot_data_pages(
    system_table,
    final_size == 0 ? 1U : final_size,
    &page_buffer,
    physical_base_out
  );
  if (!EFI_ERROR(status)) {
    panthera_zero_memory(page_buffer, final_size == 0 ? 1U : final_size);
    if (pool_size != 0) {
      panthera_copy_memory(page_buffer, pool_buffer, pool_size);
    }
  }
  panthera_free_pool(system_table, pool_buffer);
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_ensure_low32(*physical_base_out);
  if (EFI_ERROR(status)) {
    return status;
  }

  *size_out = final_size;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_stage_boot_kexts(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE *system_table
)
{
  UINT32 index;

  gBootKextCount = 0;
  panthera_zero_memory(gBootKextRecords, sizeof(gBootKextRecords));

  for (index = 0; index < PANTHERA_MAX_BOOT_KEXTS; index++) {
    const PantheraBootKextManifestEntry *manifest = &gBootKextManifest[index];
    PantheraBootKextRecord *record;
    PantheraBooterKextFileInfo file_info;
    EFI_STATUS status;

    if (manifest->bundle_name == 0 || manifest->info_path == 0) {
      break;
    }

    if (manifest->required_boot_arg != 0 &&
        !panthera_command_line_has_token(gBootCommandLine, manifest->required_boot_arg)) {
      continue;
    }
    record = &gBootKextRecords[gBootKextCount];
    record->driver_entry_name = manifest->driver_entry_name;

    status = panthera_load_file_to_pages(
      image_handle,
      system_table,
      manifest->info_path,
      1U,
      &record->info_dict_base,
      &record->info_dict_size
    );
    if (EFI_ERROR(status)) {
      console_write_ascii(system_table, "Boot kext Info.plist load failed: ");
      console_write_ascii(system_table, manifest->bundle_name);
      console_write(system_table, (const CHAR16 *)u"\r\n");
      return status;
    }

    if (manifest->executable_path == 0) {
      record->executable_base = 0;
      record->executable_size = 0;
    } else {
      status = panthera_load_file_to_pages(
        image_handle,
        system_table,
        manifest->executable_path,
        0U,
        &record->executable_base,
        &record->executable_size
      );
      if (EFI_ERROR(status)) {
        console_write_ascii(system_table, "Boot kext executable load failed: ");
        console_write_ascii(system_table, manifest->bundle_name);
        console_write(system_table, (const CHAR16 *)u"\r\n");
        return status;
      }
    }

    status = panthera_copy_ascii_to_kernel_boot_data(
      system_table,
      manifest->bundle_path,
      &record->bundle_path_base,
      &record->bundle_path_size
    );
    if (EFI_ERROR(status)) {
      return status;
    }
    status = panthera_ensure_low32(record->bundle_path_base);
    if (EFI_ERROR(status)) {
      return status;
    }

    file_info.infoDictPhysAddr = (UINT32)record->info_dict_base;
    file_info.infoDictLength = (UINT32)record->info_dict_size;
    file_info.executablePhysAddr = (UINT32)record->executable_base;
    file_info.executableLength = (UINT32)record->executable_size;
    file_info.bundlePathPhysAddr = (UINT32)record->bundle_path_base;
    file_info.bundlePathLength = (UINT32)record->bundle_path_size;

    status = panthera_copy_buffer_to_kernel_boot_data(
      system_table,
      &file_info,
      sizeof(file_info),
      &record->file_info_base
    );
    if (EFI_ERROR(status)) {
      return status;
    }
    status = panthera_ensure_low32(record->file_info_base);
    if (EFI_ERROR(status)) {
      return status;
    }

    record->device_tree_buffer.paddr = (UINT32)record->file_info_base;
    record->device_tree_buffer.length = sizeof(file_info);
    gBootKextCount++;

    console_write_ascii(system_table, "Boot kext staged: ");
    console_write_ascii(system_table, manifest->bundle_name);
    console_write(system_table, (const CHAR16 *)u"\r\n");
  }

  return EFI_SUCCESS;
}

static const PantheraKernelSegment *
panthera_find_segment(const char *name)
{
  UINT32 index;

  for (index = 0; index < gKernelImage.segment_count; index++) {
    const PantheraKernelSegment *segment = &gKernelImage.segments[index];
    UINT32 name_index = 0;
    int matches = 1;

    while (name_index < 16) {
      char a = segment->segname[name_index];
      char b = name[name_index];
      if (a != b) {
        matches = 0;
        break;
      }
      if (a == '\0') {
        break;
      }
      name_index++;
    }

    if (matches) {
      return segment;
    }
  }

  return 0;
}

static EFI_PHYSICAL_ADDRESS
panthera_segment_physical_base(const PantheraKernelSegment *segment)
{
  return gKernelImageBase + (segment->vmaddr - gKernelImage.lowest_vmaddr);
}

static EFI_STATUS
panthera_patch_u32_imm(
  UINT8 *image_base,
  UINTN opcode_offset,
  UINT8 expected_opcode,
  UINT32 expected_value,
  UINT32 patched_value
)
{
  UINT8 *opcode = image_base + opcode_offset;
  UINT32 *imm = (UINT32 *)(opcode + 1);

  if (*opcode != expected_opcode || *imm != expected_value) {
    return EFI_NOT_FOUND;
  }

  *imm = patched_value;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_patch_u32_value(
  UINT8 *image_base,
  UINTN value_offset,
  UINT32 expected_value,
  UINT32 patched_value
)
{
  UINT32 *value = (UINT32 *)(image_base + value_offset);

  if (*value != expected_value) {
    return EFI_NOT_FOUND;
  }

  *value = patched_value;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_patch_hib_startup_immediates(void)
{
  EFI_STATUS status;
  UINT8 *image_base = (UINT8 *)(UINTN)gKernelImageBase;
  UINT32 linked_base = (UINT32)gKernelImage.lowest_vmaddr;
  UINT32 linked_low_eintstack = 0x00199000U;
  UINT32 linked_protected_mode_gdtr = 0x00101040U;
  UINT32 linked_boot_pml4 = 0x00105000U;
  UINT32 linked_boot_pdpt = 0x00106000U;
  UINT32 linked_master_gdt = 0x00139000U;
  UINT32 delta = (UINT32)gKernelImageBase - linked_base;

  if (delta == 0) {
    return EFI_SUCCESS;
  }

  status = panthera_patch_u32_imm(
    image_base,
    2,
    0xbc,
    linked_low_eintstack,
    linked_low_eintstack + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_patch_u32_imm(
    image_base,
    7,
    0xb8,
    linked_protected_mode_gdtr,
    linked_protected_mode_gdtr + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_patch_u32_imm(
    image_base,
    15,
    0xb8,
    linked_boot_pml4,
    linked_boot_pml4 + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_patch_u32_imm(
    image_base,
    28,
    0xba,
    linked_boot_pdpt,
    linked_boot_pdpt + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_patch_u32_imm(
    image_base,
    66,
    0xb8,
    linked_boot_pml4,
    linked_boot_pml4 + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_patch_u32_imm(
    image_base,
    85,
    0xea,
    linked_base + 0x5cU,
    linked_base + 0x5cU + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  status = panthera_patch_u32_value(
    image_base,
    0x1042,
    linked_master_gdt,
    linked_master_gdt + delta
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  return panthera_patch_u32_value(
    image_base,
    0x99002,
    linked_master_gdt,
    linked_master_gdt + delta
  );
}

static EFI_STATUS
panthera_stage_kernel_image(
  EFI_SYSTEM_TABLE *system_table,
  const VOID *kernel_file,
  UINTN kernel_file_size
)
{
  EFI_STATUS status;
  PantheraLoaderStatus loader_status;
  UINT64 image_span = gKernelImage.highest_vmaddr - gKernelImage.lowest_vmaddr;
  EFI_PHYSICAL_ADDRESS image_base =
    (EFI_PHYSICAL_ADDRESS)(UINT32)gKernelImage.lowest_vmaddr;
  UINT32 applied_reloc_count = 0;
  UINT32 index;

  status = panthera_allocate_pages_at(system_table, (UINTN)image_span, &image_base);
  if (EFI_ERROR(status)) {
    status = panthera_allocate_pages(system_table, (UINTN)image_span, &image_base);
    if (EFI_ERROR(status)) {
      return status;
    }
  }

  gKernelImageBase = image_base;
  gKernelImageSize = (UINTN)image_span;
  gKernelReservedSize = gKernelImageSize;
  panthera_zero_memory((VOID *)(UINTN)gKernelImageBase, gKernelImageSize);

  for (index = 0; index < gKernelImage.segment_count; index++) {
    const PantheraKernelSegment *segment = &gKernelImage.segments[index];
    VOID *segment_dst = (VOID *)(UINTN)panthera_segment_physical_base(segment);
    const UINT8 *segment_src;

    if (segment->filesize == 0) {
      continue;
    }
    if (segment->fileoff + segment->filesize > kernel_file_size) {
      return EFI_NOT_FOUND;
    }

    segment_src = (const UINT8 *)kernel_file + segment->fileoff;
    panthera_copy_memory(segment_dst, segment_src, (UINTN)segment->filesize);
  }

  loader_status = panthera_rebase_kernel_locals(
    kernel_file,
    kernel_file_size,
    &gKernelImage,
    (VOID *)(UINTN)gKernelImageBase,
    gKernelImageBase,
    &applied_reloc_count
  );
  if (loader_status != PANTHERA_LOADER_OK) {
    console_write_uint64(system_table, "Kernel local rebase loader status: ", (UINT64)loader_status);
    return EFI_NOT_FOUND;
  }

  loader_status = panthera_slide_kernel_macho_metadata(&gKernelImage, gKernelImageBase);
  if (loader_status != PANTHERA_LOADER_OK) {
    console_write_uint64(system_table, "Kernel Mach-O slide loader status: ", (UINT64)loader_status);
    return EFI_NOT_FOUND;
  }

  console_write_uint64(system_table, "Kernel local relocations applied: ", applied_reloc_count);
  gKernelBootDataCursor = (EFI_PHYSICAL_ADDRESS)panthera_align_page((UINTN)(gKernelImageBase + gKernelImageSize));
  return panthera_patch_hib_startup_immediates();
}

static EFI_STATUS
panthera_capture_memory_map(
  EFI_SYSTEM_TABLE *system_table,
  VOID **memory_map_out,
  EFI_PHYSICAL_ADDRESS *memory_map_base_out,
  UINTN *memory_map_size_out,
  UINTN *memory_map_capacity_out,
  UINTN *map_key_out,
  UINTN *descriptor_size_out,
  UINT32 *descriptor_version_out
)
{
  EFI_STATUS status;
  UINTN memory_map_size = 0;
  UINTN memory_map_capacity = 0;
  VOID *page_backed_map = 0;
  EFI_PHYSICAL_ADDRESS page_backed_base = 0;

  status = system_table->BootServices->GetMemoryMap(
    &memory_map_size,
    0,
    map_key_out,
    descriptor_size_out,
    descriptor_version_out
  );
  if (status != EFI_BUFFER_TOO_SMALL) {
    return status;
  }

  memory_map_capacity = PANTHERA_MEMORY_MAP_BUFFER_SIZE;
  if (memory_map_capacity < memory_map_size) {
    memory_map_capacity = memory_map_size;
  }
  status = panthera_allocate_buffer_pages(
    system_table,
    memory_map_capacity,
    &page_backed_map,
    &page_backed_base
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  panthera_zero_memory(page_backed_map, memory_map_capacity);
  memory_map_size = memory_map_capacity;

  status = system_table->BootServices->GetMemoryMap(
    &memory_map_size,
    page_backed_map,
    map_key_out,
    descriptor_size_out,
    descriptor_version_out
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  *memory_map_out = page_backed_map;
  *memory_map_base_out = page_backed_base;
  *memory_map_size_out = memory_map_size;
  *memory_map_capacity_out = memory_map_capacity;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_refresh_memory_map(
  EFI_SYSTEM_TABLE *system_table,
  VOID *memory_map,
  UINTN memory_map_capacity,
  UINTN *memory_map_size_out,
  UINTN *map_key_out,
  UINTN *descriptor_size_out,
  UINT32 *descriptor_version_out
)
{
  UINTN memory_map_size = memory_map_capacity;
  EFI_STATUS status;

  status = system_table->BootServices->GetMemoryMap(
    &memory_map_size,
    memory_map,
    map_key_out,
    descriptor_size_out,
    descriptor_version_out
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  *memory_map_size_out = memory_map_size;
  return EFI_SUCCESS;
}

static UINT64
panthera_compute_physical_memory_size(
  VOID *memory_map,
  UINTN memory_map_size,
  UINTN descriptor_size
)
{
  UINT8 *cursor = (UINT8 *)memory_map;
  UINT8 *end = cursor + memory_map_size;
  UINT64 highest_address = 0;

  while (cursor < end) {
    EfiMemoryRange *range = (EfiMemoryRange *)cursor;
    UINT64 range_end = range->PhysicalStart + (range->NumberOfPages << 12);
    if (range_end > highest_address) {
      highest_address = range_end;
    }
    cursor += descriptor_size;
  }

  return highest_address;
}

static VOID
panthera_fill_boot_args(
  boot_args *args,
  EFI_SYSTEM_TABLE *system_table,
  VOID *memory_map,
  UINTN memory_map_size,
  UINTN descriptor_size,
  UINT32 descriptor_version
)
{
  UINT8 *cursor = (UINT8 *)memory_map;
  UINT8 *end = cursor + memory_map_size;
  UINT64 runtime_start = 0;
  UINT32 runtime_count = 0;

  panthera_zero_memory(args, sizeof(*args));
  args->Revision = kBootArgsRevision;
  args->Version = kBootArgsVersion;
  args->efiMode = kBootArgsEfiMode64;
  args->debugMode = 0;
  args->flags = 0;
  panthera_copy_ascii(args->CommandLine, sizeof(args->CommandLine), gBootCommandLine);
  args->MemoryMap = (UINT32)(UINTN)memory_map;
  args->MemoryMapSize = (UINT32)memory_map_size;
  args->MemoryMapDescriptorSize = (UINT32)descriptor_size;
  args->MemoryMapDescriptorVersion = descriptor_version;
  args->deviceTreeP = (UINT32)gDeviceTreeBase;
  args->deviceTreeLength = gDeviceTreeSize;
  args->kaddr = (UINT32)gKernelTextBase;
  args->ksize = (UINT32)gKernelReservedSize;
  args->efiSystemTable = (UINT32)(UINTN)system_table;
  args->PhysicalMemorySize = panthera_compute_physical_memory_size(memory_map, memory_map_size, descriptor_size);
  args->FSBFrequency = gFSBFrequencyHz;
  args->pciConfigSpaceBaseAddress = 0;
  args->pciConfigSpaceStartBusNumber = 0;
  args->pciConfigSpaceEndBusNumber = 0xffU;

  /* Fill framebuffer info from pre-queried GOP */
  if (gGopFrameBufferBase != 0) {
    args->VideoV1.v_baseAddr = (UINT32)(gGopFrameBufferBase & 0xFFFFFFFF);
    args->VideoV1.v_rowBytes = gGopPixelsPerScanLine * 4;
    args->VideoV1.v_width = gGopWidth;
    args->VideoV1.v_height = gGopHeight;
    args->VideoV1.v_depth = 32;
    args->VideoV1.v_display = 0; /* GRAPHICS_MODE */
    args->Video.v_baseAddr = gGopFrameBufferBase;
    args->Video.v_rowBytes = gGopPixelsPerScanLine * 4;
    args->Video.v_width = gGopWidth;
    args->Video.v_height = gGopHeight;
    args->Video.v_depth = 32;
    args->Video.v_display = 0;
  }

  while (cursor < end) {
    EfiMemoryRange *range = (EfiMemoryRange *)cursor;
    if ((range->Attribute & EFI_MEMORY_RUNTIME) != 0) {
      if (runtime_count == 0) {
        runtime_start = range->PhysicalStart >> 12;
      }
      runtime_count += (UINT32)range->NumberOfPages;
    }
    cursor += descriptor_size;
  }

  args->efiRuntimeServicesPageStart = (UINT32)runtime_start;
  args->efiRuntimeServicesPageCount = runtime_count;
  args->efiRuntimeServicesVirtualPageStart = 0;
}

static EFI_STATUS
panthera_ensure_low32(EFI_PHYSICAL_ADDRESS address)
{
  if (address > 0xffffffffULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_prepare_handoff_code(EFI_SYSTEM_TABLE *system_table)
{
  EFI_STATUS status;
  const UINTN template_size =
    (UINTN)(panthera_handoff_template_end - panthera_handoff_template_begin);
  const UINTN gdtr_offset =
    (UINTN)(panthera_handoff_gdtr - panthera_handoff_template_begin);
  const UINTN gdt_offset =
    (UINTN)(panthera_handoff_gdt - panthera_handoff_template_begin);
  VOID *handoff_code = 0;
  UINT8 *handoff_bytes;

  status = panthera_allocate_buffer_pages(system_table, template_size, &handoff_code, &gHandoffCodeBase);
  if (EFI_ERROR(status)) {
    return status;
  }

  panthera_zero_memory(handoff_code, template_size);
  panthera_copy_memory(handoff_code, panthera_handoff_template_begin, template_size);
  handoff_bytes = (UINT8 *)handoff_code;
  *(UINT64 *)(handoff_bytes + gdtr_offset + sizeof(UINT16)) =
    (UINT64)(gHandoffCodeBase + gdt_offset);
  gHandoffCodeSize = template_size;
  return EFI_SUCCESS;
}

static EFI_STATUS
panthera_prepare_handoff_stack(EFI_SYSTEM_TABLE *system_table)
{
  EFI_STATUS status;
  VOID *stack_buffer = 0;

  status = panthera_allocate_buffer_pages(
    system_table,
    PANTHERA_HANDOFF_STACK_SIZE,
    &stack_buffer,
    &gHandoffStackBase
  );
  if (EFI_ERROR(status)) {
    return status;
  }

  panthera_zero_memory(stack_buffer, PANTHERA_HANDOFF_STACK_SIZE);
  return EFI_SUCCESS;
}

static EFI_PHYSICAL_ADDRESS
panthera_compute_entry_physical_base(void)
{
  if (gKernelImage.entry_pc < gKernelImage.lowest_vmaddr ||
      gKernelImage.entry_pc >= gKernelImage.highest_vmaddr) {
    return 0;
  }

  return gKernelImageBase + (gKernelImage.entry_pc - gKernelImage.lowest_vmaddr);
}

static VOID
panthera_debugcon_putc(UINT8 value)
{
  __asm__ volatile("outb %0, $0xe9" : : "a"(value));
}

static VOID
panthera_debugcon_write_ascii(const char *text)
{
  UINTN index = 0;

  while (text[index] != '\0') {
    panthera_debugcon_putc((UINT8)text[index]);
    index++;
  }
}

__attribute__((noreturn))
static VOID
panthera_halt_forever(void)
{
  for (;;) {
    __asm__ volatile("cli; hlt");
  }
}

static EFI_STATUS
panthera_exit_boot_services_and_enter_kernel(
  EFI_HANDLE image_handle,
  EFI_SYSTEM_TABLE *system_table,
  EFI_SYSTEM_TABLE *system_table_copy,
  boot_args *args,
  VOID *memory_map
)
{
  EFI_STATUS status = EFI_NOT_FOUND;
  UINTN attempt;
  UINTN memory_map_size = 0;
  UINTN map_key = 0;
  UINTN descriptor_size = 0;
  UINT32 descriptor_version = 0;
  PantheraHandoffEntry handoff_entry = (PantheraHandoffEntry)(UINTN)gHandoffCodeBase;

  for (attempt = 0; attempt < 4; attempt++) {
    status = panthera_refresh_memory_map(
      system_table,
      memory_map,
      gMemoryMapCapacity,
      &memory_map_size,
      &map_key,
      &descriptor_size,
      &descriptor_version
    );
    if (EFI_ERROR(status)) {
      console_write_hex64(system_table, "Final GetMemoryMap failed: ", status);
      return status;
    }

    panthera_fill_boot_args(
      args,
      system_table_copy,
      memory_map,
      memory_map_size,
      descriptor_size,
      descriptor_version
    );

    status = system_table->BootServices->ExitBootServices(image_handle, map_key);
    if (!EFI_ERROR(status)) {
      panthera_debugcon_write_ascii("PANTHERA:EBS\n");

      /* Poke Bochs VBE registers to keep QEMU's VGA in linear framebuffer mode.
       * Without this, QEMU's display engine stops scanning the LFB after
       * ExitBootServices and the screen freezes on the EFI text. */
      if (gGopFrameBufferBase != 0) {
        /* VBE_DISPI_IOPORT_INDEX = 0x01CE, VBE_DISPI_IOPORT_DATA = 0x01CF */
        /* VBE_DISPI_INDEX_ENABLE = 4 */
        /* VBE_DISPI_ENABLED = 0x01, VBE_DISPI_LFB_ENABLED = 0x40 */
        __asm__ volatile("outw %0, %1" : : "a"((unsigned short)4), "Nd"((unsigned short)0x01CE));
        __asm__ volatile("outw %0, %1" : : "a"((unsigned short)0x41), "Nd"((unsigned short)0x01CF));
      }

      handoff_entry(
        (UINT32)gBootArgsBase,
        (UINT32)gKernelEntryBase,
        (UINT32)(gHandoffStackBase + PANTHERA_HANDOFF_STACK_SIZE)
      );
      panthera_debugcon_write_ascii("PANTHERA:RET\n");
      panthera_halt_forever();
    }
  }

  console_write_hex64(system_table, "ExitBootServices failed: ", status);
  return status;
}

EFI_STATUS EFIAPI
EfiMain(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
  EFI_STATUS status;
  VOID *kernel_file = 0;
  UINTN kernel_file_size = 0;
  VOID *memory_map = 0;
  UINTN memory_map_size = 0;
  UINTN map_key = 0;
  UINTN descriptor_size = 0;
  UINT32 descriptor_version = 0;
  boot_args *args = 0;
  EFI_SYSTEM_TABLE *system_table_copy = 0;

  console_write(system_table, (const CHAR16 *)u"Panthera BOOTX64 scaffold loaded.\r\n");
  console_write(system_table, (const CHAR16 *)u"Kernel ABI source: xnu-10002.41.9/pexpert/pexpert/i386/boot.h\r\n");
  console_write(system_table, (const CHAR16 *)u"Kernel entry comes from LC_UNIXTHREAD in the Mach-O image, not a hard-coded symbol.\r\n");
  console_write(system_table, (const CHAR16 *)u"Loading mach_kernel from the staged EFI volume.\r\n");
  gFSBFrequencyHz = panthera_calibrate_tsc_frequency(system_table);
  console_write_uint64(system_table, "Calibrated FSBFrequency: ", gFSBFrequencyHz);

  /* Query GOP framebuffer EARLY — before anything that changes memory map */
  {
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    typedef EFI_STATUS(EFIAPI *EFI_LOCATE_PROTOCOL)(EFI_GUID *, VOID *, VOID **);
    EFI_LOCATE_PROTOCOL lp = (EFI_LOCATE_PROTOCOL)system_table->BootServices->LocateProtocol;
    EFI_STATUS gs = lp((EFI_GUID *)&gEfiGraphicsOutputProtocolGuid, 0, (VOID **)&gop);
    if (!EFI_ERROR(gs) && gop != 0 && gop->Mode != 0) {
      gGopFrameBufferBase = gop->Mode->FrameBufferBase;
      gGopWidth = gop->Mode->Info->HorizontalResolution;
      gGopHeight = gop->Mode->Info->VerticalResolution;
      gGopPixelsPerScanLine = gop->Mode->Info->PixelsPerScanLine;
      console_write(system_table, (const CHAR16 *)u"Framebuffer configured.\r\n");
    } else {
      console_write(system_table, (const CHAR16 *)u"WARNING: No GOP framebuffer.\r\n");
    }
  }

  status = panthera_try_load_kernel(image_handle, system_table, &kernel_file, &kernel_file_size);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Kernel load failed: ", status);
    return status;
  }

  console_write_uint64(system_table, "Kernel file size: ", kernel_file_size);

  status = panthera_parse_kernel_macho(kernel_file, kernel_file_size, &gKernelImage);
  if (status != PANTHERA_LOADER_OK) {
    console_write_hex64(system_table, "Kernel parse failed: ", status);
    panthera_free_pool(system_table, kernel_file);
    return EFI_NOT_FOUND;
  }

  console_write_hex64(system_table, "Kernel entry PC: ", gKernelImage.entry_pc);
  console_write_uint64(system_table, "Kernel segment count: ", gKernelImage.segment_count);
  status = panthera_stage_kernel_image(system_table, kernel_file, kernel_file_size);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Kernel staging failed: ", status);
    panthera_free_pool(system_table, kernel_file);
    return status;
  }
  console_write_hex64(system_table, "Kernel image physical base: ", gKernelImageBase);
  console_write_uint64(system_table, "Kernel image physical size: ", gKernelImageSize);
  {
    const PantheraKernelSegment *text_segment = panthera_find_segment("__TEXT");
    if (text_segment != 0) {
      gKernelTextBase = panthera_segment_physical_base(text_segment);
      console_write_hex64(system_table, "Kernel text physical base: ", gKernelTextBase);
    } else {
      console_write(system_table, (const CHAR16 *)u"Missing __TEXT segment in parsed kernel image.\r\n");
      panthera_free_pool(system_table, kernel_file);
      return EFI_NOT_FOUND;
    }
  }
  gKernelEntryBase = panthera_compute_entry_physical_base();
  if (gKernelEntryBase == 0) {
    console_write(system_table, (const CHAR16 *)u"Kernel entry PC is outside the staged image span.\r\n");
    panthera_free_pool(system_table, kernel_file);
    return EFI_NOT_FOUND;
  }
  console_write_hex64(system_table, "Kernel entry physical base: ", gKernelEntryBase);

  status = panthera_ensure_low32(gKernelTextBase);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"Kernel text base is above 4GiB.\r\n");
    panthera_free_pool(system_table, kernel_file);
    return status;
  }
  status = panthera_ensure_low32(gKernelEntryBase);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"Kernel entry base is above 4GiB.\r\n");
    panthera_free_pool(system_table, kernel_file);
    return status;
  }

  panthera_free_pool(system_table, kernel_file);

  status = panthera_capture_memory_map(
    system_table,
    &memory_map,
    &gMemoryMapBase,
    &memory_map_size,
    &gMemoryMapCapacity,
    &map_key,
    &descriptor_size,
    &descriptor_version
  );
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "GetMemoryMap failed: ", status);
    return status;
  }

  console_write_uint64(system_table, "EFI memory map size: ", memory_map_size);
  console_write_uint64(system_table, "EFI descriptor size: ", descriptor_size);
  console_write_hex64(system_table, "EFI map key: ", map_key);
  console_write_hex64(system_table, "EFI memory map base: ", gMemoryMapBase);

  status = panthera_capture_system_table(system_table, &system_table_copy, &gSystemTableBase);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "System table copy failed: ", status);
    return status;
  }

  console_write_hex64(system_table, "EFI system table copy: ", gSystemTableBase);
  status = panthera_ensure_low32(gSystemTableBase);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"EFI system table copy is above 4GiB.\r\n");
    return status;
  }

  status = panthera_allocate_boot_args(system_table, &args, &gBootArgsBase);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "boot_args allocation failed: ", status);
    return status;
  }
  status = panthera_ensure_low32(gBootArgsBase);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"boot_args allocation is above 4GiB.\r\n");
    return status;
  }

  status = panthera_load_boot_command_line(image_handle, system_table);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Boot args load failed: ", status);
    return status;
  }

  status = panthera_stage_boot_kexts(image_handle, system_table);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Boot kext staging failed: ", status);
    return status;
  }
  console_write_uint64(system_table, "Boot kext count: ", gBootKextCount);

  status = panthera_build_synthetic_device_tree(system_table);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Device tree allocation failed: ", status);
    return status;
  }
  status = panthera_ensure_low32(gDeviceTreeBase);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"Device tree allocation is above 4GiB.\r\n");
    return status;
  }
  console_write_hex64(system_table, "Device tree physical base: ", gDeviceTreeBase);
  console_write_uint64(system_table, "Device tree size: ", gDeviceTreeSize);

  status = panthera_prepare_handoff_code(system_table);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Handoff code allocation failed: ", status);
    return status;
  }
  console_write_hex64(system_table, "Handoff code base: ", gHandoffCodeBase);
  console_write_uint64(system_table, "Handoff code size: ", gHandoffCodeSize);
  status = panthera_ensure_low32(gHandoffCodeBase);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"Handoff code base is above 4GiB.\r\n");
    return status;
  }

  status = panthera_prepare_handoff_stack(system_table);
  if (EFI_ERROR(status)) {
    console_write_hex64(system_table, "Handoff stack allocation failed: ", status);
    return status;
  }
  console_write_hex64(system_table, "Handoff stack base: ", gHandoffStackBase);
  status = panthera_ensure_low32(gHandoffStackBase + PANTHERA_HANDOFF_STACK_SIZE);
  if (EFI_ERROR(status)) {
    console_write(system_table, (const CHAR16 *)u"Handoff stack top is above 4GiB.\r\n");
    return status;
  }

  panthera_fill_boot_args(args, system_table_copy, memory_map, memory_map_size, descriptor_size, descriptor_version);
  args->deviceTreeP = (UINT32)gDeviceTreeBase;
  args->deviceTreeLength = gDeviceTreeSize;
  console_write_hex64(system_table, "boot_args physical base: ", gBootArgsBase);
  console_write_hex64(system_table, "boot_args MemoryMap: ", args->MemoryMap);
  console_write_hex64(system_table, "boot_args efiSystemTable: ", args->efiSystemTable);
  console_write_uint64(system_table, "boot_args PhysicalMemorySize: ", args->PhysicalMemorySize);
  console_write_uint64(system_table, "boot_args RuntimePageCount: ", args->efiRuntimeServicesPageCount);
  console_write(system_table, (const CHAR16 *)u"Attempting ExitBootServices and 32-bit pstart handoff.\r\n");
  status = panthera_exit_boot_services_and_enter_kernel(
    image_handle,
    system_table,
    system_table_copy,
    args,
    memory_map
  );
  return status;
}
