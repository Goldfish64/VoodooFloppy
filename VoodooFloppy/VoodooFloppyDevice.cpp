//
//  VoodooFloppyDevice.cpp
//  VoodooFloppy
//
//  Created by John Davis on 11/4/18.
//  Copyright © 2018 Goldfish64. All rights reserved.
//

#include <IOKit/IOLib.h>

#include "VoodooFloppyDevice.hpp"

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppyDevice, IOBlockStorageDevice)

bool VoodooFloppyDevice::start(IOService *provider) {
    IOLog("VoodooFloppyDevice: start()\n");
    
    // Start superclass first.
    if (!super::start(provider))
        return false;
    
    return true;
}

IOReturn VoodooFloppyDevice::doEjectMedia() {
    IOLog("VoodooFloppyDevice::doEjectMedia()\n");
    return kIOReturnUnsupported;
}

IOReturn VoodooFloppyDevice::doFormatMedia(UInt64 byteCapacity) {
    IOLog("VoodooFloppyDevice::doFormatMedia()\n");
    return kIOReturnUnsupported;
}

UInt32 VoodooFloppyDevice::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const {
    IOLog("VoodooFloppyDevice::doGetFormatCapacities()\n");
    return 0;
}

char *VoodooFloppyDevice::getVendorString() {
    IOLog("VoodooFloppyDevice::getVendorString()\n");
    return "Generic";
}

char *VoodooFloppyDevice::getProductString() {
    IOLog("VoodooFloppyDevice::getProductString()\n");
    return "Floppy Drive";
}

char *VoodooFloppyDevice::getRevisionString() {
    IOLog("VoodooFloppyDevice::getRevisionString()\n");
    return "";
}

char *VoodooFloppyDevice::getAdditionalDeviceInfoString() {
    IOLog("VoodooFloppyDevice::getAdditionalDeviceInfoString()\n");
    return "";
}

IOReturn VoodooFloppyDevice::reportBlockSize(UInt64 *blockSize) {
    IOLog("VoodooFloppyDevice::reportBlockSize()\n");
    *blockSize = 512;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyDevice::reportEjectability(bool *isEjectable) {
    *isEjectable = true;
    IOLog("VoodooFloppyDevice::reportEjectability()\n");
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyDevice::reportMaxValidBlock(UInt64 *maxBlock) {
    IOLog("VoodooFloppyDevice::reportMaxValidBlock()\n");
    *maxBlock = 2880;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyDevice::reportMediaState(bool *mediaPresent, bool *changedState) {
    IOLog("VoodooFloppyDevice::reportMediaState()\n");
    *mediaPresent = true;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyDevice::reportRemovability(bool *isRemovable) {
    IOLog("VoodooFloppyDevice::reportRemovability()\n");
    *isRemovable = true;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyDevice::reportWriteProtection(bool *isWriteProtected) {
    IOLog("VoodooFloppyDevice::reportWriteProtection()\n");
    // TODO
    *isWriteProtected = true;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyDevice::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion) {
    IOLog("VoodooFloppyDevice::doAsyncReadWrite()\n");
    return kIOReturnUnsupported;
}
