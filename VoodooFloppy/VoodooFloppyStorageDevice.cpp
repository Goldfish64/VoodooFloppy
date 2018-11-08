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
char _productString[] = "Floppy Disk";

bool VoodooFloppyStorageDevice::attach(IOService *provider) {
    IOLog("VoodooFloppyStorageDevice::attach()\n");
    if (!super::attach(provider))
        return false;
    
    // Save reference to controller.
    _controller = (VoodooFloppyController*)provider;
    IOLog("VoodooFloppyStorageDevice: Drive number %u, type 0x%X\n", ((OSNumber*)getProperty(kFloppyPropertyDriveIdKey))->unsigned8BitValue(), ((OSNumber*)getProperty(FLOPPY_IOREG_DRIVE_TYPE))->unsigned8BitValue());
    _controller->initDrive(0, FLOPPY_TYPE_1440_35);
    return true;
}

void VoodooFloppyStorageDevice::detach(IOService *provider) {
    IOLog("VoodooFloppyStorageDevice::detach()\n");
    super::detach(provider);
}

/*!
 * @function doEjectMedia
 * Eject the media.
 */
IOReturn VoodooFloppyStorageDevice::doEjectMedia() {
    IOLog("VoodooFloppyStorageDevice::doEjectMedia()\n");
    
    // Software ejection is not supported.
    return kIOReturnUnsupported;
}

IOReturn VoodooFloppyStorageDevice::doFormatMedia(UInt64 byteCapacity) {
    IOLog("VoodooFloppyStorageDevice::doFormatMedia()\n");
    
    // Low-level formatting is not supported.
    return kIOReturnUnsupported;
}

UInt32 VoodooFloppyStorageDevice::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const {
    IOLog("VoodooFloppyStorageDevice::doGetFormatCapacities()\n");
    return 0;
}

/*!
 * @function getVendorString
 * Return Vendor Name string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getVendorString() {
    IOLog("VoodooFloppyStorageDevice::getVendorString()\n");
    return NULL;
}

/*!
 * @function getProductString
 * Return Product Name string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getProductString() {
    IOLog("VoodooFloppyStorageDevice::getProductString()\n");
    return _productString;
}

/*!
 * @function getRevisionString
 * Return Product Revision string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getRevisionString() {
    IOLog("VoodooFloppyStorageDevice::getRevisionString()\n");
    return NULL;
}

/*!
 * @function getAdditionalDeviceInfoString
 * Return additional informational string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getAdditionalDeviceInfoString() {
    IOLog("VoodooFloppyStorageDevice::getAdditionalDeviceInfoString()\n");
    return NULL;
}

IOReturn VoodooFloppyStorageDevice::reportBlockSize(UInt64 *blockSize) {
    IOLog("VoodooFloppyStorageDevice::reportBlockSize()\n");
    *blockSize = 512;
    return kIOReturnSuccess;
}

/*!
 * @function reportEjectability
 * Report if the media is ejectable under software control.
 * @discussion
 * This method should only be called if the media is known to be removable.
 * @param isEjectable
 * Pointer to returned result. True indicates the media is ejectable, False indicates
 * the media cannot be ejected under software control.
 */
IOReturn VoodooFloppyStorageDevice::reportEjectability(bool *isEjectable) {
    IOLog("VoodooFloppyStorageDevice::reportEjectability()\n");
    
    // Software ejection is not supported.
    *isEjectable = false;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportMaxValidBlock(UInt64 *maxBlock) {
    IOLog("VoodooFloppyStorageDevice::reportMaxValidBlock()\n");
    *maxBlock = 2880 - 2;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::reportMediaState(bool *mediaPresent, bool *changedState) {
    IOLog("VoodooFloppyStorageDevice::reportMediaState()\n");
    *mediaPresent = true;
    return kIOReturnSuccess;
}

/*!
 * @function reportRemovability
 * Report whether the media is removable or not.
 * @discussion
 * This method reports whether the media is removable, but it does not
 * provide detailed information regarding software eject or lock/unlock capability.
 * @param isRemovable
 * Pointer to returned result. True indicates that the media is removable; False
 * indicates the media is not removable.
 */
IOReturn VoodooFloppyStorageDevice::reportRemovability(bool *isRemovable) {
    IOLog("VoodooFloppyStorageDevice::reportRemovability()\n");
    
    // Media is removable.
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
    _controller->readDrive(0, buffer, block, nblks, attributes);
    IOStorage::complete(completion, kIOReturnSuccess, nblks * 512);
    return kIOReturnSuccess;
}
