#ifndef PANTHERA_UEFI_H
#define PANTHERA_UEFI_H

#include <stdint.h>

typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef __CHAR16_TYPE__ CHAR16;
typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef uint64_t UINTN;
typedef uint16_t UINT16;
typedef void VOID;
typedef UINT64 EFI_PHYSICAL_ADDRESS;

#if defined(__x86_64__)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

#define EFI_SUCCESS 0ULL
#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL
#define EFI_NOT_FOUND 0x800000000000000eULL
#define EFI_OUT_OF_RESOURCES 0x8000000000000009ULL
#define EFI_ERROR(status) (((status) & 0x8000000000000000ULL) != 0)

#define EFI_LOADER_DATA 2U
#define EFI_ALLOCATE_ANY_PAGES 0U
#define EFI_ALLOCATE_MAX_ADDRESS 1U
#define EFI_ALLOCATE_ADDRESS 2U
#define EFI_FILE_MODE_READ 0x0000000000000001ULL
#define EFI_MEMORY_RUNTIME 0x8000000000000000ULL

typedef struct {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8 Data4[8];
} EFI_GUID;

typedef struct {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 CRC32;
  UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;
typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct EFI_LOADED_IMAGE_PROTOCOL EFI_LOADED_IMAGE_PROTOCOL;
typedef struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
typedef struct EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL;
typedef struct EFI_CONFIGURATION_TABLE EFI_CONFIGURATION_TABLE;

typedef EFI_STATUS(EFIAPI *EFI_TEXT_RESET)(
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
  UINT8 ExtendedVerification
);

typedef EFI_STATUS(EFIAPI *EFI_TEXT_STRING)(
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
  CHAR16 *String
);

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  EFI_TEXT_RESET Reset;
  EFI_TEXT_STRING OutputString;
  void *TestString;
  void *QueryMode;
  void *SetMode;
  void *SetAttribute;
  void *ClearScreen;
  void *SetCursorPosition;
  void *EnableCursor;
  void *Mode;
};

typedef EFI_STATUS(EFIAPI *EFI_HANDLE_PROTOCOL)(
  EFI_HANDLE Handle,
  EFI_GUID *Protocol,
  VOID **Interface
);

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_POOL)(
  UINT32 PoolType,
  UINTN Size,
  VOID **Buffer
);

typedef EFI_STATUS(EFIAPI *EFI_GET_MEMORY_MAP)(
  UINTN *MemoryMapSize,
  VOID *MemoryMap,
  UINTN *MapKey,
  UINTN *DescriptorSize,
  UINT32 *DescriptorVersion
);

typedef EFI_STATUS(EFIAPI *EFI_ALLOCATE_PAGES)(
  UINT32 Type,
  UINT32 MemoryType,
  UINTN Pages,
  EFI_PHYSICAL_ADDRESS *Memory
);

typedef EFI_STATUS(EFIAPI *EFI_FREE_POOL)(
  VOID *Buffer
);

typedef EFI_STATUS(EFIAPI *EFI_EXIT_BOOT_SERVICES)(
  EFI_HANDLE ImageHandle,
  UINTN MapKey
);

struct EFI_BOOT_SERVICES {
  EFI_TABLE_HEADER Hdr;
  VOID *RaiseTPL;
  VOID *RestoreTPL;
  EFI_ALLOCATE_PAGES AllocatePages;
  VOID *FreePages;
  EFI_GET_MEMORY_MAP GetMemoryMap;
  EFI_ALLOCATE_POOL AllocatePool;
  EFI_FREE_POOL FreePool;
  VOID *CreateEvent;
  VOID *SetTimer;
  VOID *WaitForEvent;
  VOID *SignalEvent;
  VOID *CloseEvent;
  VOID *CheckEvent;
  VOID *InstallProtocolInterface;
  VOID *ReinstallProtocolInterface;
  VOID *UninstallProtocolInterface;
  EFI_HANDLE_PROTOCOL HandleProtocol;
  VOID *Reserved;
  VOID *RegisterProtocolNotify;
  VOID *LocateHandle;
  VOID *LocateDevicePath;
  VOID *InstallConfigurationTable;
  VOID *LoadImage;
  VOID *StartImage;
  VOID *Exit;
  VOID *UnloadImage;
  EFI_EXIT_BOOT_SERVICES ExitBootServices;
  VOID *GetNextMonotonicCount;
  VOID *Stall;
  VOID *SetWatchdogTimer;
  VOID *ConnectController;
  VOID *DisconnectController;
  VOID *OpenProtocol;
  VOID *CloseProtocol;
  VOID *OpenProtocolInformation;
  VOID *ProtocolsPerHandle;
  VOID *LocateHandleBuffer;
  VOID *LocateProtocol;
  VOID *InstallMultipleProtocolInterfaces;
  VOID *UninstallMultipleProtocolInterfaces;
  VOID *CalculateCrc32;
  VOID *CopyMem;
  VOID *SetMem;
  VOID *CreateEventEx;
};

struct EFI_LOADED_IMAGE_PROTOCOL {
  UINT64 Revision;
  EFI_HANDLE ParentHandle;
  struct EFI_SYSTEM_TABLE *SystemTable;
  EFI_HANDLE DeviceHandle;
  EFI_DEVICE_PATH_PROTOCOL *FilePath;
  VOID *Reserved;
  UINT32 LoadOptionsSize;
  VOID *LoadOptions;
  VOID *ImageBase;
  UINT64 ImageSize;
  UINT32 ImageCodeType;
  UINT32 ImageDataType;
  VOID *Unload;
};

typedef EFI_STATUS(EFIAPI *EFI_OPEN_VOLUME)(
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This,
  EFI_FILE_PROTOCOL **Root
);

struct EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
  UINT64 Revision;
  EFI_OPEN_VOLUME OpenVolume;
};

typedef EFI_STATUS(EFIAPI *EFI_FILE_OPEN)(
  EFI_FILE_PROTOCOL *This,
  EFI_FILE_PROTOCOL **NewHandle,
  CHAR16 *FileName,
  UINT64 OpenMode,
  UINT64 Attributes
);

typedef EFI_STATUS(EFIAPI *EFI_FILE_CLOSE)(
  EFI_FILE_PROTOCOL *This
);

typedef EFI_STATUS(EFIAPI *EFI_FILE_READ)(
  EFI_FILE_PROTOCOL *This,
  UINTN *BufferSize,
  VOID *Buffer
);

typedef EFI_STATUS(EFIAPI *EFI_FILE_GET_INFO)(
  EFI_FILE_PROTOCOL *This,
  EFI_GUID *InformationType,
  UINTN *BufferSize,
  VOID *Buffer
);

struct EFI_FILE_PROTOCOL {
  UINT64 Revision;
  EFI_FILE_OPEN Open;
  EFI_FILE_CLOSE Close;
  VOID *Delete;
  EFI_FILE_READ Read;
  VOID *Write;
  VOID *GetPosition;
  VOID *SetPosition;
  EFI_FILE_GET_INFO GetInfo;
  VOID *SetInfo;
  VOID *Flush;
  VOID *OpenEx;
  VOID *ReadEx;
  VOID *WriteEx;
  VOID *FlushEx;
};

typedef struct {
  UINT64 Size;
  UINT64 FileSize;
  UINT64 PhysicalSize;
  UINT64 CreateTime[2];
  UINT64 LastAccessTime[2];
  UINT64 ModificationTime[2];
  UINT64 Attribute;
  CHAR16 FileName[1];
} EFI_FILE_INFO;

typedef struct EFI_SYSTEM_TABLE {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  UINT32 FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  VOID *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
  EFI_HANDLE StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
  EFI_RUNTIME_SERVICES *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  UINTN NumberOfTableEntries;
  EFI_CONFIGURATION_TABLE *ConfigurationTable;
} EFI_SYSTEM_TABLE;

struct EFI_CONFIGURATION_TABLE {
  EFI_GUID VendorGuid;
  VOID *VendorTable;
};

static const EFI_GUID gEfiLoadedImageProtocolGuid = {
  0x5b1b31a1U, 0x9562U, 0x11d2U, {0x8eU, 0x3fU, 0x00U, 0xa0U, 0xc9U, 0x69U, 0x72U, 0x3bU}
};

static const EFI_GUID gEfiSimpleFileSystemProtocolGuid = {
  0x964e5b22U, 0x6459U, 0x11d2U, {0x8eU, 0x39U, 0x00U, 0xa0U, 0xc9U, 0x69U, 0x72U, 0x3bU}
};

static const EFI_GUID gEfiFileInfoGuid = {
  0x09576e92U, 0x6d3fU, 0x11d2U, {0x8eU, 0x39U, 0x00U, 0xa0U, 0xc9U, 0x69U, 0x72U, 0x3bU}
};

/* ═══════════════════════════════════════════════════════════
 * Graphics Output Protocol (GOP) — for framebuffer access
 * ═══════════════════════════════════════════════════════════ */

typedef enum {
  PixelRedGreenBlueReserved8BitPerColor,
  PixelBlueGreenRedReserved8BitPerColor,
  PixelBitMask,
  PixelBltOnly,
  PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
  UINT32 RedMask;
  UINT32 GreenMask;
  UINT32 BlueMask;
  UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
  EFI_PIXEL_BITMASK PixelInformation;
  UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  UINT32 ModeNumber,
  UINTN *SizeOfInfo,
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
);

typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  UINT32 ModeNumber
);

typedef EFI_STATUS(EFIAPI *EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT)(
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  VOID *BltBuffer,
  UINT32 BltOperation,
  UINTN SourceX, UINTN SourceY,
  UINTN DestinationX, UINTN DestinationY,
  UINTN Width, UINTN Height,
  UINTN Delta
);

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
  EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

static const EFI_GUID gEfiGraphicsOutputProtocolGuid = {
  0x9042a9deU, 0x23dcU, 0x4a38U, {0x96U, 0xfbU, 0x7aU, 0xdeU, 0xd0U, 0x80U, 0x51U, 0x6aU}
};

#endif
