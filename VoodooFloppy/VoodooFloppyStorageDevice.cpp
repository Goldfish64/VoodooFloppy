//
//  VoodooFloppyStorageDevice.cpp
//  VoodooFloppy
//
//  Created by John Davis on 11/4/18.
//  Copyright © 2018 Goldfish64. All rights reserved.
//

#include <IOKit/IOLib.h>

#include "VoodooFloppyStorageDevice.hpp"

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppyStorageDevice, IOBlockStorageDevice)

bool VoodooFloppyStorageDevice::attach(IOService *provider) {
    IOLog("VoodooFloppyStorageDevice::attach()\n");
    if (!super::attach(provider))
        return false;
    
    // Save reference to controller.
    _controller = (VoodooFloppyController*)provider;
    IOLog("VoodooFloppyStorageDevice: Drive number %u, type 0x%X\n", ((OSNumber*)getProperty(FLOPPY_IOREG_DRIVE_NUM))->unsigned8BitValue(), ((OSNumber*)getProperty(FLOPPY_IOREG_DRIVE_TYPE))->unsigned8BitValue());
    _controller->initDrive(0, FLOPPY_TYPE_1440_35);
    return true;
}

void VoodooFloppyStorageDevice::detach(IOService *provider) {
    IOLog("VoodooFloppyStorageDevice::detach()\n");
    super::detach(provider);
}

IOReturn VoodooFloppyStorageDevice::doEjectMedia() {
    IOLog("VoodooFloppyStorageDevice::doEjectMedia()\n");
    return kIOReturnUnsupported;
}

IOReturn VoodooFloppyStorageDevice::doFormatMedia(UInt64 byteCapacity) {
    IOLog("VoodooFloppyStorageDevice::doFormatMedia()\n");
    return kIOReturnUnsupported;
}

UInt32 VoodooFloppyStorageDevice::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const {
    IOLog("VoodooFloppyStorageDevice::doGetFormatCapacities()\n");
    return 0;
}

char *VoodooFloppyStorageDevice::getVendorString() {
    IOLog("VoodooFloppyStorageDevice::getVendorString()\n");
    return "Generic";
}

char *VoodooFloppyStorageDevice::getProductString() {
    IOLog("VoodooFloppyStorageDevice::getProductString()\n");
    return "Floppy Drive";
}

char *VoodooFloppyStorageDevice::getRevisionString() {
    IOLog("VoodooFloppyStorageDevice::getRevisionString()\n");
    return "";
}

char *VoodooFloppyStorageDevice::getAdditionalDeviceInfoString() {
    IOLog("VoodooFloppyStorageDevice::getAdditionalDeviceInfoString()\n");
    return "";
}

IOReturn VoodooFloppyStorageDevice::reportBlockSize(UInt64 *blockSize) {
    IOLog("VoodooFloppyStorageDevice::reportBlockSize()\n");
    *blockSize = 512;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportEjectability(bool *isEjectable) {
    *isEjectable = false;
    IOLog("VoodooFloppyStorageDevice::reportEjectability()\n");
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportMaxValidBlock(UInt64 *maxBlock) {
    IOLog("VoodooFloppyStorageDevice::reportMaxValidBlock()\n");
    *maxBlock = 2880;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportMediaState(bool *mediaPresent, bool *changedState) {
    IOLog("VoodooFloppyStorageDevice::reportMediaState()\n");
    *mediaPresent = true;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportRemovability(bool *isRemovable) {
    IOLog("VoodooFloppyStorageDevice::reportRemovability()\n");
    *isRemovable = true;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportWriteProtection(bool *isWriteProtected) {
    IOLog("VoodooFloppyStorageDevice::reportWriteProtection()\n");
    // TODO
    *isWriteProtected = true;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion) {
    IOLog("VoodooFloppyStorageDevice::doAsyncReadWrite(start %llu, %llu blocks)\n", block, nblks);
   // _controller->readDrive(0, buffer, block, nblks, attributes);
    return kIOReturnUnsupported;
}
