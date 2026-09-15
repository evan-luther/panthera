#include "PantheraATAStorage.h"

#include <IOKit/ata/IOATADevConfig.h>
#include <IOKit/ata/IOATABusInfo.h>
#include <IOKit/ata/IOATABusCommand.h>
#include <pexpert/pexpert.h>

#define super IOBlockStorageDevice
#define PANTHERA_ATAST_TRACE(...) PANTHERA_TRACE(IOLog(__VA_ARGS__))
OSDefineMetaClassAndStructors(PantheraATAStorage, IOBlockStorageDevice);

namespace {
static const UInt32 kPantheraATABlockSize = 512;
static const UInt32 kPantheraATAMaxTransferBlocks = 256;
static const UInt32 kPantheraATAIdentifyBytes = 512;

static bool
pantheraATABootArgEnabled(const char *name)
{
    int value = 0;

    return PE_parse_boot_argn(name, &value, sizeof(value)) && value != 0;
}

static bool
pantheraATABootArgExplicitlyDisabled(const char *name)
{
    int value = 0;

    return PE_parse_boot_argn(name, &value, sizeof(value)) && value == 0;
}

static IOReturn
setLBA48(IOATACommand *command, UInt64 lba, ataUnitID unit)
{
    if (!command || (lba & 0xFFFF000000000000ULL) != 0 || unit > 1) {
        return kIOReturnBadArgument;
    }
    IOExtendedLBA *extLBA = command->getExtendedLBA();
    if (!extLBA) {
        return kIOReturnUnsupported;
    }

    UInt8 lba7 = (UInt8) (lba & 0xff);
    UInt8 lba15 = (UInt8) ((lba >> 8) & 0xff);
    UInt8 lba23 = (UInt8) ((lba >> 16) & 0xff);
    UInt8 lba31 = (UInt8) ((lba >> 24) & 0xff);
    UInt8 lba39 = (UInt8) ((lba >> 32) & 0xff);
    UInt8 lba47 = (UInt8) ((lba >> 40) & 0xff);

    extLBA->setLBALow16(lba7 | (((UInt16) lba31) << 8));
    extLBA->setLBAMid16(lba15 | (((UInt16) lba39) << 8));
    extLBA->setLBAHigh16(lba23 | (((UInt16) lba47) << 8));
    extLBA->setDevice(mATALBASelect | (((UInt8) unit) << 4));
    command->setDevice_Head(mATALBASelect | (((UInt8) unit) << 4));
    return kIOReturnSuccess;
}
}

bool
PantheraATAStorage::init(OSDictionary *properties)
{
    bool ok;

    _ioLock = 0;
    ok = super::init(properties);
    _ioLock = ok ? IOLockAlloc() : 0;
    if (ok && !_ioLock) {
        return false;
    }
    PANTHERA_ATAST_TRACE("PANTHERA:ATAST init ok=%d\n", ok ? 1 : 0);
    return ok;
}

IOService *
PantheraATAStorage::probe(IOService *provider, SInt32 *score)
{
    PANTHERA_ATAST_TRACE("PANTHERA:ATAST probe provider=%s score=%ld\n",
                         provider ? provider->getName() : "<null>",
                         score ? (long) *score : -1L);
    return super::probe(provider, score);
}

bool
PantheraATAStorage::start(IOService *provider)
{
    PANTHERA_ATAST_TRACE("PANTHERA:ATAST start enter provider=%s class=%s\n",
                         provider ? provider->getName() : "<null>",
                         provider ? provider->getMetaClass()->getClassName() : "<null>");

    if (!super::start(provider)) {
        PANTHERA_ATAST_TRACE("PANTHERA:ATAST super::start failed\n");
        return false;
    }

    _provider = OSDynamicCast(IOATADevice, provider);
    if (!_provider) {
        PANTHERA_ATAST_TRACE("PANTHERA:ATAST provider cast failed\n");
        return false;
    }

    if (!_provider->open(this)) {
        PANTHERA_ATAST_TRACE("PANTHERA:ATAST provider open failed\n");
        return false;
    }

    _provider->retain();
    _blockSize = kPantheraATABlockSize;
    _maxBlock = 0;
    _mediaPresent = true;
    _mediaChanged = true;
    _writeProtected = false;
    _supports48Bit = false;
    _useDMA = false;
    _useInterrupts = false;

    strlcpy(_vendor, "Panthera", sizeof(_vendor));
    strlcpy(_product, "ATA Disk", sizeof(_product));
    strlcpy(_revision, "0.1", sizeof(_revision));
    strlcpy(_additionalInfo, "", sizeof(_additionalInfo));

    if (identifyDevice() != kIOReturnSuccess) {
        PANTHERA_ATAST_TRACE("PANTHERA:ATAST identify failed\n");
        return false;
    }

    setProperty(kIOMaximumBlockCountReadKey, kPantheraATAMaxTransferBlocks, 64);
    setProperty(kIOMaximumBlockCountWriteKey, kPantheraATAMaxTransferBlocks, 64);
    setProperty(kIOMaximumByteCountReadKey, (UInt64) kPantheraATAMaxTransferBlocks * _blockSize, 64);
    setProperty(kIOMaximumByteCountWriteKey, (UInt64) kPantheraATAMaxTransferBlocks * _blockSize, 64);
    setProperty(kIOMaximumSegmentCountReadKey, 32, 64);
    setProperty(kIOMaximumSegmentCountWriteKey, 32, 64);
    setProperty(kIOMinimumSegmentAlignmentByteCountKey, 2, 64);

    if (provider->getLocation()) {
        setLocation(provider->getLocation());
    }

    IOLog("PANTHERA:ATAST identified unit=%u supports48=%d maxBlock=%llu product=%s\n",
          (unsigned) _provider->getUnitID(),
          _supports48Bit ? 1 : 0,
          _maxBlock,
          _product);
    PANTHERA_ATAST_TRACE("PANTHERA:ATAST start unit=%u maxBlock=%llu product=%s\n",
                         (unsigned) _provider->getUnitID(),
                         _maxBlock,
                         _product);

    registerService();
    return true;
}

void
PantheraATAStorage::stop(IOService *provider)
{
    if (_provider) {
        _provider->close(this);
    }
    super::stop(provider);
}

void
PantheraATAStorage::free(void)
{
    if (_ioLock) {
        IOLockFree(_ioLock);
        _ioLock = 0;
    }
    if (_provider) {
        _provider->release();
        _provider = 0;
    }
    super::free();
}

IOReturn
PantheraATAStorage::doEjectMedia(void)
{
    return kIOReturnUnsupported;
}

IOReturn
PantheraATAStorage::doFormatMedia(UInt64)
{
    return kIOReturnUnsupported;
}

UInt32
PantheraATAStorage::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const
{
    if (capacities && capacitiesMaxCount > 0) {
        capacities[0] = (_maxBlock + 1) * _blockSize;
    }
    return 1;
}

char *
PantheraATAStorage::getVendorString(void)
{
    return _vendor;
}

char *
PantheraATAStorage::getProductString(void)
{
    return _product;
}

char *
PantheraATAStorage::getRevisionString(void)
{
    return _revision;
}

char *
PantheraATAStorage::getAdditionalDeviceInfoString(void)
{
    return _additionalInfo;
}

IOReturn
PantheraATAStorage::reportBlockSize(UInt64 *blockSize)
{
    if (!blockSize) {
        return kIOReturnBadArgument;
    }
    *blockSize = _blockSize;
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::reportEjectability(bool *isEjectable)
{
    if (!isEjectable) {
        return kIOReturnBadArgument;
    }
    *isEjectable = false;
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::reportMaxValidBlock(UInt64 *maxBlock)
{
    if (!maxBlock) {
        return kIOReturnBadArgument;
    }
    *maxBlock = _maxBlock;
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::reportMediaState(bool *mediaPresent, bool *changedState)
{
    if (!mediaPresent) {
        return kIOReturnBadArgument;
    }
    *mediaPresent = _mediaPresent;
    if (changedState) {
        *changedState = _mediaChanged;
    }
    _mediaChanged = false;
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::reportRemovability(bool *isRemovable)
{
    if (!isRemovable) {
        return kIOReturnBadArgument;
    }
    *isRemovable = false;
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::reportWriteProtection(bool *isWriteProtected)
{
    if (!isWriteProtected) {
        return kIOReturnBadArgument;
    }
    *isWriteProtected = _writeProtected;
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::doAsyncReadWrite(IOMemoryDescriptor *buffer,
                                     UInt64 block,
                                     UInt64 nblks,
                                     IOStorageAttributes *,
                                     IOStorageCompletion *completion)
{
    IOReturn status;
    UInt64 actualBytes = 0;

    if (!buffer || nblks == 0) {
        status = kIOReturnBadArgument;
    } else if (block > _maxBlock || (block + nblks - 1) > _maxBlock) {
        status = kIOReturnBadArgument;
    } else {
        status = executeSectors((buffer->getDirection() & kIODirectionOut) != 0,
                                buffer,
                                block,
                                nblks,
                                &actualBytes);
    }

    if (completion && completion->action) {
        completion->action(completion->target, completion->parameter, status, actualBytes);
        return kIOReturnSuccess;
    }

    return status;
}

IOReturn
PantheraATAStorage::doSynchronize(UInt64 block,
                                  UInt64 nblks,
                                  IOStorageSynchronizeOptions options)
{
    (void) block;
    (void) nblks;

    if ((options & kIOStorageSynchronizeOptionReserved) != 0) {
        return kIOReturnBadArgument;
    }
    // ponytail: guest FLUSH CACHE is skipped by default; run_phase2_qemu.sh enforces cache=writethrough so persistence is host-side. Set panthera_ata_noflush=0 to issue real flushes (needs the IRQ fix from R2.1).
    if (pantheraATABootArgExplicitlyDisabled("panthera_ata_noflush")) {
        return flushCache();
    }
    return kIOReturnSuccess;
}

IOReturn
PantheraATAStorage::flushCache(void)
{
    IOATACommand *command;
    IOReturn status;

    if (!_provider) {
        return kIOReturnOffline;
    }

    command = _provider->allocCommand();
    if (!command) {
        return kIOReturnNoMemory;
    }

    command->zeroCommand();
    command->setOpcode(kATAFnExecIO);
    command->setFlags(mATAFlagUseNoIRQ | mATAFlagImmediate | mATAFlagUseConfigSpeed);
    command->setUnit(_provider->getUnitID());
    command->setTimeoutMS(30000);
    command->setDevice_Head(((UInt8) _provider->getUnitID()) << 4);
    command->setCommand(kATAcmdFlushCache);

    if (_ioLock) {
        IOLockLock(_ioLock);
    }
    status = _provider->executeCommand(command);
    if (_ioLock) {
        IOLockUnlock(_ioLock);
    }
    _provider->freeCommand(command);
    PANTHERA_ATAST_TRACE("PANTHERA:ATAST flushCache status=0x%x\n", status);
    return status;
}

IOReturn
PantheraATAStorage::identifyDevice(void)
{
    UInt8 *buffer;
    IOMemoryDescriptor *descriptor;
    IOATACommand *command;
    IOReturn status;
    UInt64 sectors;

    buffer = (UInt8 *) IOMallocZeroData(kPantheraATAIdentifyBytes);
    if (!buffer) {
        return kIOReturnNoMemory;
    }

    descriptor = IOMemoryDescriptor::withAddress(buffer,
                                                 kPantheraATAIdentifyBytes,
                                                 kIODirectionIn);
    if (!descriptor) {
        IOFreeData(buffer, kPantheraATAIdentifyBytes);
        return kIOReturnNoMemory;
    }

    command = _provider->allocCommand();
    if (!command) {
        descriptor->release();
        IOFreeData(buffer, kPantheraATAIdentifyBytes);
        return kIOReturnNoMemory;
    }

    command->zeroCommand();
    command->setOpcode(kATAFnExecIO);
    command->setFlags(mATAFlagIORead | mATAFlagUseNoIRQ);
    command->setUnit(_provider->getUnitID());
    command->setTimeoutMS(30000);
    command->setBuffer(descriptor);
    command->setPosition(0);
    command->setByteCount(kPantheraATAIdentifyBytes);
    command->setDevice_Head(((UInt8) _provider->getUnitID()) << 4);
    command->setCommand(kATAcmdDriveIdentify);

    status = executePolledCommand(command,
                                  descriptor,
                                  kIODirectionIn,
                                  kPantheraATAIdentifyBytes,
                                  0);

    if (status == kIOReturnSuccess) {
        const UInt16 *words = (const UInt16 *) buffer;
        _supports48Bit = (words[83] & (1 << 10)) != 0;
        if (_supports48Bit) {
            sectors = (UInt64) words[100] |
                      ((UInt64) words[101] << 16) |
                      ((UInt64) words[102] << 32) |
                      ((UInt64) words[103] << 48);
            if (sectors == 0) {
                sectors = ((UInt64) words[61] << 16) |
                          (UInt64) words[60];
            }
        } else {
            sectors = ((UInt64) words[61] << 16) |
                      (UInt64) words[60];
        }

        if (sectors == 0) {
            status = kIOReturnNoMedia;
        } else {
            _maxBlock = sectors - 1;
            configureDevice((const UInt16 *) buffer);

            swapBytes16(&buffer[46], 8);
            swapBytes16(&buffer[54], 40);
            swapBytes16(&buffer[20], 20);
            fillCString(_revision, sizeof(_revision), &buffer[46], 8);
            fillCString(_product, sizeof(_product), &buffer[54], 40);
            fillCString(_additionalInfo, sizeof(_additionalInfo), &buffer[20], 20);
            PANTHERA_ATAST_TRACE("PANTHERA:ATAST identify unit=%u supports48=%d maxBlock=%llu\n",
                                 (unsigned) _provider->getUnitID(),
                                 _supports48Bit ? 1 : 0,
                                 _maxBlock);
        }
    }

    _provider->freeCommand(command);
    descriptor->release();
    IOFreeData(buffer, kPantheraATAIdentifyBytes);
    return status;
}

IOReturn
PantheraATAStorage::configureDevice(const UInt16 *identifyData)
{
    IOATABusInfo *busInfo;
    IOATADevConfig *config;
    IOReturn status;

    if (!_provider || !identifyData) {
        return kIOReturnBadArgument;
    }

    busInfo = IOATABusInfo::atabusinfo();
    if (!busInfo) {
        return kIOReturnNoMemory;
    }

    config = IOATADevConfig::atadevconfig();
    if (!config) {
        busInfo->release();
        return kIOReturnNoMemory;
    }

    status = _provider->provideBusInfo(busInfo);
    if (status == kIOReturnSuccess) {
        status = config->initWithBestSelection(identifyData, busInfo);
    }
    if (status == kIOReturnSuccess) {
        status = _provider->selectConfig(config);
    }

    if (status == kIOReturnSuccess) {
        bool dmaAvailable = (config->getDMAMode() != 0) || (config->getUltraMode() != 0);
        bool disableDMA = pantheraATABootArgEnabled("panthera_ata_nodma") ||
                          pantheraATABootArgExplicitlyDisabled("panthera_ata_dma");
        bool forcePolled = pantheraATABootArgEnabled("panthera_ata_polled");

        _useDMA = dmaAvailable && !disableDMA;
        _useInterrupts = _useDMA || !forcePolled;
        setProperty("Panthera DMA Available", dmaAvailable);
        setProperty("Panthera DMA Enabled", _useDMA);
        setProperty("Panthera DMA Disabled By Boot Arg", disableDMA);
        setProperty("Panthera IRQ Enabled", _useInterrupts);
        setProperty("Panthera Polled I/O Enabled", !_useInterrupts);
        setProperty("Panthera PIO Mode", config->getPIOMode(), 8);
        setProperty("Panthera DMA Mode", config->getDMAMode(), 8);
        setProperty("Panthera Ultra DMA Mode", config->getUltraMode(), 8);
        IOLog("PANTHERA:ATAST configured pio=0x%x dma=0x%x udma=0x%x dmaAvailable=%d useDMA=%d dmaDisabled=%d useIRQ=%d polled=%d\n",
              config->getPIOMode(),
              config->getDMAMode(),
              config->getUltraMode(),
              dmaAvailable ? 1 : 0,
              _useDMA ? 1 : 0,
              disableDMA ? 1 : 0,
              _useInterrupts ? 1 : 0,
              _useInterrupts ? 0 : 1);
        PANTHERA_ATAST_TRACE("PANTHERA:ATAST configured pio=0x%x dma=0x%x udma=0x%x useDMA=%d dmaDisabled=%d useIRQ=%d polled=%d\n",
                             config->getPIOMode(),
                             config->getDMAMode(),
                             config->getUltraMode(),
                             _useDMA ? 1 : 0,
                             disableDMA ? 1 : 0,
                             _useInterrupts ? 1 : 0,
                             _useInterrupts ? 0 : 1);
    } else {
        _useDMA = false;
        _useInterrupts = false;
        IOLog("PANTHERA:ATAST configureDevice failed status=0x%x, using PIO\n",
              status);
        PANTHERA_ATAST_TRACE("PANTHERA:ATAST configureDevice failed status=0x%x, using PIO\n",
                             status);
    }

    config->release();
    busInfo->release();
    return status;
}

IOReturn
PantheraATAStorage::executePolledCommand(IOATACommand *command,
                                         IOMemoryDescriptor *descriptor,
                                         IODirection direction,
                                         IOByteCount byteCount,
                                         IOByteCount position)
{
    IOReturn status;

    command->setBuffer(descriptor);
    command->setPosition(position);
    command->setByteCount(byteCount);

    descriptor->prepare(direction);
    status = _provider->executeCommand(command);
    descriptor->complete(direction);
    return status;
}

IOReturn
PantheraATAStorage::executeSectors(bool write,
                                   IOMemoryDescriptor *buffer,
                                   UInt64 block,
                                   UInt64 nblks,
                                   UInt64 *actualBytes)
{
    IOReturn status = kIOReturnSuccess;
    UInt64 remaining = nblks;
    UInt64 currentBlock = block;
    UInt64 transferred = 0;

    if (_ioLock) {
        IOLockLock(_ioLock);
    }

    while (remaining > 0) {
        UInt16 chunkBlocks;
        IOATACommand *command;
        IOByteCount chunkBytes;
        UInt64 chunkPosition;

        chunkBlocks = (remaining > kPantheraATAMaxTransferBlocks) ?
                      kPantheraATAMaxTransferBlocks :
                      (UInt16) remaining;
        chunkBytes = (IOByteCount) (chunkBlocks * _blockSize);
        chunkPosition = transferred;

        command = _provider->allocCommand();
        if (!command) {
            status = kIOReturnNoMemory;
            break;
        }

        command->zeroCommand();
        command->setOpcode(kATAFnExecIO);
        UInt32 flags = (write ? mATAFlagIOWrite : mATAFlagIORead) |
                       mATAFlagUseConfigSpeed |
                       (_useDMA ? mATAFlagUseDMA : 0) |
                       (_useInterrupts ? 0 : mATAFlagUseNoIRQ);
        if (_supports48Bit) {
            flags |= mATAFlag48BitLBA;
        }
        command->setFlags(flags);
        command->setUnit(_provider->getUnitID());
        command->setTimeoutMS(30000);

        if (_supports48Bit) {
            status = setLBA48(command, currentBlock, _provider->getUnitID());
            if (status != kIOReturnSuccess) {
                _provider->freeCommand(command);
                break;
            }
            IOExtendedLBA *extLBA = command->getExtendedLBA();
            extLBA->setSectorCount16(chunkBlocks);
            UInt8 ataCmd = _useDMA ?
                           (write ? kATAcmdWriteDMAExtended : kATAcmdReadDMAExtended) :
                           (write ? kATAcmdWriteExtended : kATAcmdReadExtended);
            extLBA->setCommand(ataCmd);
            command->setCommand(ataCmd);
        } else {
            if (currentBlock > 0x0fffffffULL) {
                status = kIOReturnUnsupported;
                _provider->freeCommand(command);
                break;
            }
            status = command->setLBA28((UInt32) currentBlock, _provider->getUnitID());
            if (status != kIOReturnSuccess) {
                _provider->freeCommand(command);
                break;
            }
            command->setSectorCount((UInt8) (chunkBlocks == 256 ? 0 : chunkBlocks));
            command->setCommand(_useDMA ?
                                (write ? kATAcmdWriteDMA : kATAcmdReadDMA) :
                                (write ? kATAcmdWrite : kATAcmdRead));
        }
        status = executePolledCommand(command,
                                      buffer,
                                      write ? kIODirectionOut : kIODirectionIn,
                                      chunkBytes,
                                      (IOByteCount) chunkPosition);
        _provider->freeCommand(command);

        if (status != kIOReturnSuccess) {
            break;
        }

        transferred += chunkBytes;
        currentBlock += chunkBlocks;
        remaining -= chunkBlocks;
    }

    if (actualBytes) {
        *actualBytes = transferred;
    }
    if (_ioLock) {
        IOLockUnlock(_ioLock);
    }
    return status;
}

void
PantheraATAStorage::swapBytes16(UInt8 *dataBuffer, IOByteCount length)
{
    IOByteCount i;
    UInt8 c;
    UInt8 *firstBytePtr;

    for (i = 0; i < length; i += 2) {
        firstBytePtr = dataBuffer;
        c = *dataBuffer++;
        *firstBytePtr = *dataBuffer;
        *dataBuffer++ = c;
    }
}

void
PantheraATAStorage::fillCString(char *dst, size_t dstSize, UInt8 *src, size_t srcLen)
{
    size_t i;
    size_t out = 0;
    bool sawNonSpace = false;

    if (!dst || dstSize == 0) {
        return;
    }

    for (i = 0; i < srcLen && out + 1 < dstSize; ++i) {
        char c = (char) src[i];
        if (c == '\0') {
            break;
        }
        if (c == ' ' && !sawNonSpace) {
            continue;
        }
        dst[out++] = c;
        if (c != ' ') {
            sawNonSpace = true;
        }
    }

    while (out > 0 && dst[out - 1] == ' ') {
        --out;
    }
    dst[out] = '\0';
}
