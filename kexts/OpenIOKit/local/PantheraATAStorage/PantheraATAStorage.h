#ifndef PANTHERA_ATA_STORAGE_H
#define PANTHERA_ATA_STORAGE_H

#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/ata/IOATADevice.h>
#include <IOKit/storage/IOBlockStorageDevice.h>

class PantheraATAStorage : public IOBlockStorageDevice
{
    OSDeclareDefaultStructors(PantheraATAStorage);

public:
    bool init(OSDictionary *properties = 0) APPLE_KEXT_OVERRIDE;
    IOService *probe(IOService *provider, SInt32 *score) APPLE_KEXT_OVERRIDE;
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free(void) APPLE_KEXT_OVERRIDE;

    IOReturn doEjectMedia(void) APPLE_KEXT_OVERRIDE;
    IOReturn doFormatMedia(UInt64 byteCapacity) APPLE_KEXT_OVERRIDE;
    UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
    char *getVendorString(void) APPLE_KEXT_OVERRIDE;
    char *getProductString(void) APPLE_KEXT_OVERRIDE;
    char *getRevisionString(void) APPLE_KEXT_OVERRIDE;
    char *getAdditionalDeviceInfoString(void) APPLE_KEXT_OVERRIDE;
    IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE;
    IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE;
    IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
    IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) APPLE_KEXT_OVERRIDE;
    IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE;
    IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE;
    IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer,
                              UInt64 block,
                              UInt64 nblks,
                              IOStorageAttributes *attributes,
                              IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;
    IOReturn doSynchronize(UInt64 block,
                           UInt64 nblks,
                           IOStorageSynchronizeOptions options = 0) APPLE_KEXT_OVERRIDE;

private:
    IOReturn identifyDevice(void);
    IOReturn configureDevice(const UInt16 *identifyData);
    IOReturn flushCache(void);
    IOReturn executePolledCommand(IOATACommand *command,
                                  IOMemoryDescriptor *descriptor,
                                  IODirection direction,
                                  IOByteCount byteCount,
                                  IOByteCount position);
    IOReturn executeSectors(bool write,
                            IOMemoryDescriptor *buffer,
                            UInt64 block,
                            UInt64 nblks,
                            UInt64 *actualBytes);
    static void swapBytes16(UInt8 *dataBuffer, IOByteCount length);
    void fillCString(char *dst, size_t dstSize, UInt8 *src, size_t srcLen);

    IOATADevice *_provider;
    UInt64 _maxBlock;
    UInt64 _blockSize;
    bool _mediaPresent;
    bool _mediaChanged;
    bool _writeProtected;
    bool _supports48Bit;
    bool _useDMA;
    bool _useInterrupts;
    IOLock *_ioLock;

    char _vendor[16];
    char _product[48];
    char _revision[16];
    char _additionalInfo[32];
};

#endif
