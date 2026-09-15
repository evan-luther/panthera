#!/bin/bash
# build_all.sh — Build all OpenIOKit kexts for Panthera
#
# Builds Apple open-source IOKit kexts in dependency order.
# Products go to kexts/OpenIOKit/build/<KextName>/<KextName>.kext/

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PANTHERA_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
SRCDIR="${PANTHERA_ROOT}/src"
BUILD="${SCRIPT_DIR}/build_kext.sh"
LOGS_DIR="${SCRIPT_DIR}/build/logs"

mkdir -p "${LOGS_DIR}"

BUILT=0
FAILED=0
SKIPPED=0

build_kext() {
    local name="$1"; shift
    local srcdir="$1"; shift
    local srcs=("$@")

    if [ ! -d "$srcdir" ]; then
        echo "[SKIP] ${name} — source not found at ${srcdir}"
        SKIPPED=$((SKIPPED + 1))
        return 0
    fi

    local logfile="${LOGS_DIR}/${name}.log"
    if bash "${BUILD}" "${name}" "${srcdir}" "${srcs[@]}" > "${logfile}" 2>&1; then
        echo "[OK]   ${name}"
        BUILT=$((BUILT + 1))
    else
        echo "[FAIL] ${name} (log: ${logfile})"
        tail -n 25 "${logfile}" 2>/dev/null || true
        FAILED=$((FAILED + 1))
    fi
}

echo "========================================"
echo "OpenIOKit Build — Panthera Kext Suite"
echo "========================================"

# ── Tier 0: Platform fundamentals ──────────────────────

build_kext "AppleAPIC" \
    "${SRCDIR}/AppleAPIC-13" \
    AppleAPIC.cpp

build_kext "AppleSMBIOS" \
    "${SRCDIR}/AppleSMBIOS-42" \
    AppleSMBIOS.cpp

build_kext "AppleI386PCI" \
    "${SRCDIR}/AppleI386PCI-6" \
    --extra-include "${SRCDIR}/IOPCIFamily-617.40.5.0.1" \
    AppleI386PCI.cpp AppleI386AGP.cpp

build_kext "AppleI386GenericPlatform" \
    "${SRCDIR}/AppleI386GenericPlatform-5" \
    AppleI386PlatformExpert.cpp AppleI386CPU.cpp

# ── Tier 1: Bus families ───────────────────────────────

build_kext "IOPCIFamily" \
    "${SRCDIR}/IOPCIFamily-617.40.5.0.1" \
    --extra-include "${SRCDIR}/IOPCIFamily-617.40.5.0.1" \
    PantheraAppleVTDStubs.cpp IOPCIBridge.cpp IOPCIBridgeLegacy.cpp IOPCIDevice.cpp IOPCIConfigurator.cpp \
    IOPCIRange.cpp IOPCIDeviceI386.cpp \
    IOPCIMessagedInterruptController.cpp

build_kext "IOGraphicsFamily" \
    "${SRCDIR}/IOGraphics-598" \
    --module-start IOGraphicsFamilyModuleStart \
    --define GTRACE_IMPL=1 \
    --extra-include "${SRCDIR}/IOGraphics-598/IOGraphicsFamily" \
    --extra-include "${SRCDIR}/IOGraphics-598/IONDRVSupport" \
    --extra-include "${SRCDIR}/IOGraphics-598/IOGraphicsFamily/IOKit/graphics/tl" \
    --extra-include "${SRCDIR}/IOGraphics-598/GTrace/Kernel" \
    --extra-include "${SRCDIR}/IOGraphics-598/GMetric" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem" \
    IOGraphicsFamily/IOBootFramebuffer.cpp \
    IOGraphicsFamily/IODisplay.cpp \
    IOGraphicsFamily/IODisplayWrangler.cpp \
    IOGraphicsFamily/IOFramebuffer.cpp \
    IOGraphicsFamily/IOFramebufferUserClient.cpp \
    IOGraphicsFamily/IOGraphicsDevice.cpp \
    IOGraphicsFamily/IOI2CInterface.cpp \
    IOGraphicsFamily/IOKit/graphics/tl/osmemory.cpp \
    GTrace/Kernel/GTrace.cpp \
    GMetric/GMetric.cpp

build_kext "IOACPIFamily" \
    "${SRCDIR}/IOACPIFamily-8" \
    IOACPIPlatformDevice.cpp

# ── Tier 2: Storage stack ──────────────────────────────

build_kext "IOStorageFamily" \
    "${SRCDIR}/IOStorageFamily-312" \
    --extra-include "${SRCDIR}/IOStorageFamily-312" \
    IOStorage.cpp IOBlockStorageDevice.cpp IOBlockStorageDriver.cpp \
    IOMedia.cpp IOMediaBSDClient.cpp \
    IOGUIDPartitionScheme.cpp IOFDiskPartitionScheme.cpp \
    IOApplePartitionScheme.cpp IOPartitionScheme.cpp \
    IOFilterScheme.cpp

build_kext "HFSEncodings" \
    "${SRCDIR}/hfs-650.0.2" \
    --extra-include "${SRCDIR}/hfs-650.0.2/hfs_encodings" \
    hfs_encodings/hfs_encodings.c \
    hfs_encodings/hfs_encodings_kext.cpp \
    hfs_encodings/hfs_encodinghint.c

build_kext "HFS" \
    "${SRCDIR}/hfs-650.0.2/core" \
    --extra-include "${SRCDIR}/hfs-650.0.2/hfs_encodings" \
    --force-include "${SRCDIR}/hfs-650.0.2/core/kext-config.h" \
    --define BSD_KERNEL_PRIVATE \
    BTree.c BTreeAllocate.c BTreeMiscOps.c BTreeNodeOps.c \
    BTreeNodeReserve.c BTreeScanner.c BTreeTreeOps.c BTreeWrapper.c \
    CatalogUtilities.c FileExtentMapping.c FileIDsServices.c MacOSStubs.c \
    UnicodeWrappers.c VolumeAllocation.c hfs_attrlist.c hfs_btreeio.c \
    hfs_catalog.c hfs_chash.c hfs_cnode.c hfs_cprotect.c hfs_endian.c \
    hfs_extents.c hfs_fsinfo.c hfs_hotfiles.c hfs_iokit.cpp hfs_journal.c \
    hfs_link.c hfs_lookup.c hfs_notification.c hfs_quota.c hfs_readwrite.c \
    hfs_resize.c hfs_search.c hfs_vfsops.c hfs_vfsutils.c hfs_vnops.c \
    hfs_xattr.c rangelist.c

build_kext "IOATAFamily" \
    "${SRCDIR}/IOATAFamily-261" \
    ATADeviceNub.cpp \
    ATATimerEventSource.cpp \
    IOATARegI386.cpp \
    IOPCIATA.cpp \
    IOATAController.cpp IOATADevice.cpp IOATABusInfo.cpp \
    IOATABusCommand.cpp IOATADevConfig.cpp IOATACommand.cpp

build_kext "AppleIntelPIIXATA" \
    "${SRCDIR}/AppleIntelPIIXATA-251.0.1" \
    --extra-include "${SRCDIR}/AppleIntelPIIXATA-251.0.1" \
    AppleIntelPIIXATARoot.cpp AppleIntelPIIXATAChannel.cpp \
    AppleIntelPIIXPATA.cpp AppleIntelICHxSATA.cpp

build_kext "PantheraATAStorage" \
    "${SCRIPT_DIR}/local/PantheraATAStorage" \
    --extra-include "${SRCDIR}/IOATAFamily-261" \
    --extra-include "${SRCDIR}/IOStorageFamily-312" \
    PantheraATAStorage.cpp

# ── Tier 3: Networking stack ───────────────────────────

build_kext "IONetworkingFamily" \
    "${SRCDIR}/IONetworkingFamily-177" \
    IONetworkController.cpp IOEthernetController.cpp \
    IONetworkInterface.cpp IOEthernetInterface.cpp \
    IONetworkStack.cpp IONetworkUserClient.cpp \
    IONetworkMedium.cpp IOMbufMemoryCursor.cpp \
    IONetworkData.cpp IOKernelDebugger.cpp \
    IOOutputQueue.cpp IOPacketQueue.cpp

build_kext "AppleRTL8139Ethernet" \
    "${SRCDIR}/AppleRTL8139Ethernet-153" \
    RTL8139.cpp RTL8139PHY.cpp RTL8139Private.cpp

# ── Tier 4: Input / Serial ─────────────────────────────

build_kext "Apple16X50Serial" \
    "${SRCDIR}/Apple16X50Serial-24" \
    --extra-include "${SRCDIR}/Apple16X50Serial-24" \
    Apple16X50UARTSync.cpp Apple16X50UARTSyncPower.cpp \
    Apple16X50BusInterface.cpp Apple16X50PCI.cpp \
    Apple16X50Queue.cpp Apple16X50UARTTypes.cpp

build_kext "IOHIDSystem" \
    "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDFamily" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem/IOKit/hidsystem" \
    --extra-include "${SRCDIR}/IOGraphics-598/IOGraphicsFamily" \
    IOFixedPoint64.cpp \
    IOHIDDeviceShim.cpp \
    IOBSDConsole.cpp \
    IOHIDWorkLoop.cpp \
    IOHIDKeyboardDevice.cpp \
    IOHIDPointingDevice.cpp \
    IOHIDKeyboardEventDevice.cpp \
    IOHIDPointingEventDevice.cpp \
    IOHIDUserClient.cpp \
    IOHIDSystemCursorHelper.cpp \
    IOHIDPrivate.cpp \
    IOHIDSystem.cpp

build_kext "IOHIDFamily" \
    "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDFamily" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDFamily" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem/IOKit/hidsystem" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem/IOHIDDescriptorParser" \
    --extra-include "${SRCDIR}/IOGraphics-598/IOGraphicsFamily" \
    Cosmo_USB2ADB.c \
    ../IOHIDSystem/IOFixed64.cpp \
    IOHIDDebug.c \
    ../IOHIDSystem/IOHIDevice.cpp \
    IOHIDConsumer.cpp \
    IOHIDElementContainer.cpp \
    IOHIDElementPrivate.cpp \
    IOHIDDeviceElementContainer.cpp \
    IOHIDDevice.cpp \
    IOHIDEventCompat.cpp \
    IOHIDEventDriverCompat.cpp \
    IOHIDEventService.cpp \
    IOHIDEventServiceQueue.cpp \
    IOHIDInterface.cpp \
    ../IOHIDSystem/IOHIKeyboard.cpp \
    ../IOHIDSystem/IOHIKeyboardMapper.cpp \
    IOHIDKeyboard.cpp \
    IOHIDFamilyPrivate.cpp \
    IOHIDEventSource.cpp \
    IOHIDEventQueue.cpp \
    ../IOHIDSystem/IOHIPointing.cpp \
    ../IOHIDSystem/IOHITablet.cpp \
    IOHIDPointing.cpp \
    IOHIDReportElementQueue.cpp \
    IOHIDLibUserClient.cpp \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDCountDescriptorItems.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDHasUsage.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDIsButtonOrValue.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDOpenCloseDescriptor.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDGetButtonCaps.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDGetCaps.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDGetCollectionNodes.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDNextItem.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDProcessCollection.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDProcessGlobalItem.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDProcessLocalItem.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDProcessMainItem.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDProcessReportItem.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDGetValueCaps.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDParseDescriptor.c \
    ../IOHIDSystem/IOHIDDescriptorParser/HIDUsageInRange.c

build_kext "IOUSBFamily" \
    "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily" \
    --define SUPPORTS_SS_USB=1 \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBUserClient/Headers" \
    Classes/AppleUSBDiagnostics.cpp \
    Classes/IOUSBBus.cpp \
    Classes/IOUSBCommand.cpp \
    Classes/IOUSBController.cpp \
    Classes/IOUSBControllerListElement.cpp \
    Classes/IOUSBControllerUserClient.cpp \
    Classes/IOUSBControllerV2.cpp \
    Classes/IOUSBControllerV3.cpp \
    Classes/IOUSBController_Errata.cpp \
    Classes/IOUSBController_Pipes.cpp \
    Classes/IOUSBDevice.cpp \
    Classes/IOUSBHubDevice.cpp \
    Classes/IOUSBHubPolicyMaker.cpp \
    Classes/IOUSBInterface.cpp \
    Classes/IOUSBLog.cpp \
    Classes/IOUSBNub.cpp \
    Classes/IOUSBPipe.cpp \
    Classes/IOUSBPipeV2.cpp \
    Classes/IOUSBRootHubDevice.cpp \
    Classes/IOUSBWorkLoop.cpp

build_kext "AppleUSBEHCI" \
    "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBEHCI" \
    --define SUPPORTS_SS_USB=1 \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBEHCI/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBOHCI/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily/Headers" \
    Classes/AppleEHCIListElement.cpp \
    Classes/AppleEHCITestMode.cpp \
    Classes/AppleEHCIedMemoryBlock.cpp \
    Classes/AppleEHCIitdMemoryBlock.cpp \
    Classes/AppleEHCIsitdMemoryBlock.cpp \
    Classes/AppleEHCItdMemoryBlock.cpp \
    Classes/AppleUSBEHCI.cpp \
    Classes/AppleUSBEHCIHubInfo.cpp \
    Classes/AppleUSBEHCI_Interrupts.cpp \
    Classes/AppleUSBEHCI_PwrMgmt.cpp \
    Classes/AppleUSBEHCI_RootHub.cpp \
    Classes/AppleUSBEHCI_UIM.cpp

build_kext "AppleUSBUHCI" \
    "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBUHCI" \
    --define SUPPORTS_SS_USB=1 \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBUHCI/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBEHCI/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBOHCI/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily/Headers" \
    Classes/AppleUHCIListElement.cpp \
    Classes/AppleUHCIqhMemoryBlock.cpp \
    Classes/AppleUHCItdMemoryBlock.cpp \
    Classes/AppleUSBUHCI.cpp \
    Classes/AppleUSBUHCI_Interrupts.cpp \
    Classes/AppleUSBUHCI_Obsolete.cpp \
    Classes/AppleUSBUHCI_PwrMgmt.cpp \
    Classes/AppleUSBUHCI_RootHub.cpp \
    Classes/AppleUSBUHCI_UIM.cpp

build_kext "AppleUSBHub" \
    "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBHub" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/AppleUSBHub/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily/Headers" \
    Classes/AppleUSBHub.cpp \
    Classes/AppleUSBHubPort.cpp \
    Classes/AppleUSBHSHubUserClient.cpp

build_kext "IOUSBCompositeDriver" \
    "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBCompositeDriver" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBCompositeDriver/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily/Headers" \
    Classes/IOUSBCompositeDriver.cpp

build_kext "IOUSBHIDDriver" \
    "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBHIDDriver" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBHIDDriver/Headers" \
    --extra-include "${SRCDIR}/IOUSBFamily-630.4.5/IOUSBFamily/Headers" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDFamily" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem/IOKit/hidsystem" \
    Classes/IOUSBHIDDriver.cpp

build_kext "ApplePS2Controller" \
    "${SRCDIR}/ApplePS2Controller-8" \
    ApplePS2Controller.cpp \
    ApplePS2KeyboardDevice.cpp \
    ApplePS2MouseDevice.cpp

build_kext "ApplePS2Keyboard" \
    "${SRCDIR}/ApplePS2Keyboard-9" \
    --extra-include "${SRCDIR}/ApplePS2Controller-8" \
    --extra-include "${SRCDIR}/IOHIDFamily-2008.40.6/IOHIDSystem" \
    --extra-include "${SRCDIR}/IOGraphics-598/IOGraphicsFamily" \
    ApplePS2Keyboard.cpp

# ── Summary ────────────────────────────────────────────

echo ""
echo "========================================"
echo "OpenIOKit Build Complete"
echo "  Built:   ${BUILT}"
echo "  Failed:  ${FAILED}"
echo "  Skipped: ${SKIPPED}"
echo "========================================"

# List what was produced
echo ""
echo "Built kexts:"
find "${SCRIPT_DIR}/build" -name "*.kext" -type d 2>/dev/null | sort | while read -r k; do
    kname="$(basename "$k")"
    kbase="${kname%.kext}"
    kbin="${k}/Contents/MacOS/${kbase}"
    if [ -s "$kbin" ]; then
        size=$(du -sh "$k" 2>/dev/null | cut -f1)
        echo "  ${k##*/build/}  (${size})"
    fi
done

if [ "$FAILED" -gt 0 ] || [ "$SKIPPED" -gt 0 ]; then
    exit 1
fi
