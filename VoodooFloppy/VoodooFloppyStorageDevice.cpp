/*
 * File: VoodooFloppyStorageDevice.cpp
 *
 * Copyright (c) 2018 John Davis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <IOKit/IOLib.h>
#include <IOKit/storage/IOBlockStorageDriver.h>

#include "VoodooFloppyStorageDevice.hpp"
#include "IO.h"

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppyStorageDevice, IOBlockStorageDevice)
char _productString[] = "Floppy Disk";

bool VoodooFloppyStorageDevice::attach(IOService *provider) {
    DBGLOG("VoodooFloppyStorageDevice::attach()\n");
    if (!super::attach(provider))
        return false;
    
    // Media is present.
    _mediaPresent = true;
    _writeProtected = false;
    _blockSize = 512;
    _maxValidBlock = 2880 - 1;
    
    // Save reference to controller.
    _controller = (VoodooFloppyController*)provider;
    DBGLOG("VoodooFloppyStorageDevice: Drive number %u, type 0x%X\n", ((OSNumber*)getProperty(kFloppyPropertyDriveIdKey))->unsigned8BitValue(), ((OSNumber*)getProperty(FLOPPY_IOREG_DRIVE_TYPE))->unsigned8BitValue());
    
    probeMedia();
    //_controller->initDrive(0, FLOPPY_TYPE_1440_35);
    return true;
}

void VoodooFloppyStorageDevice::detach(IOService *provider) {
    DBGLOG("VoodooFloppyStorageDevice::detach()\n");
    super::detach(provider);
}

/*!
 * @function doEjectMedia
 * Eject the media.
 */
IOReturn VoodooFloppyStorageDevice::doEjectMedia() {
    DBGLOG("VoodooFloppyStorageDevice::doEjectMedia()\n");
    
    // Software ejection is not supported.
    return kIOReturnUnsupported;
}

IOReturn VoodooFloppyStorageDevice::doFormatMedia(UInt64 byteCapacity) {
    DBGLOG("VoodooFloppyStorageDevice::doFormatMedia()\n");
    
    // Low-level formatting is not supported.
    return kIOReturnUnsupported;
}

UInt32 VoodooFloppyStorageDevice::doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const {
    DBGLOG("VoodooFloppyStorageDevice::doGetFormatCapacities()\n");
    return 0;
}

/*!
 * @function getVendorString
 * Return Vendor Name string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getVendorString() {
    DBGLOG("VoodooFloppyStorageDevice::getVendorString()\n");
    return NULL;
}

/*!
 * @function getProductString
 * Return Product Name string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getProductString() {
    DBGLOG("VoodooFloppyStorageDevice::getProductString()\n");
    return _productString;
}

/*!
 * @function getRevisionString
 * Return Product Revision string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getRevisionString() {
    DBGLOG("VoodooFloppyStorageDevice::getRevisionString()\n");
    return NULL;
}

/*!
 * @function getAdditionalDeviceInfoString
 * Return additional informational string for the device.
 * @result
 * A pointer to a static character string.
 */
char *VoodooFloppyStorageDevice::getAdditionalDeviceInfoString() {
    DBGLOG("VoodooFloppyStorageDevice::getAdditionalDeviceInfoString()\n");
    return NULL;
}

/*!
 * @function reportBlockSize
 * Report the block size for the device, in bytes.
 * @param blockSize
 * Pointer to returned block size value.
 */
IOReturn VoodooFloppyStorageDevice::reportBlockSize(UInt64 *blockSize) {
    DBGLOG("VoodooFloppyStorageDevice::reportBlockSize()\n");
    
    // Get block (sector) size.
    *blockSize = _blockSize;
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
    DBGLOG("VoodooFloppyStorageDevice::reportEjectability()\n");
    
    // Software ejection is not supported.
    *isEjectable = false;
    return kIOReturnSuccess;
}

/*!
 * @function reportMaxValidBlock
 * Report the highest valid block for the device.
 * @param maxBlock
 * Pointer to returned result
 */
IOReturn VoodooFloppyStorageDevice::reportMaxValidBlock(UInt64 *maxBlock) {
    DBGLOG("VoodooFloppyStorageDevice::reportMaxValidBlock()\n");
    
    // Get max valid block (sector).
    *maxBlock = _maxValidBlock;
    return kIOReturnSuccess;
}

/*!
 * @function reportMediaState
 * Report the device's media state.
 * @discussion
 * This method reports whether we have media in the drive or not, and
 * whether the state has changed from the previously reported state.
 *
 * A result of kIOReturnSuccess is always returned if the test for media is successful,
 * regardless of media presence. The mediaPresent result should be used to determine
 * whether media is present or not. A return other than kIOReturnSuccess indicates that
 * the Transport Driver was unable to interrogate the device. In this error case, the
 * outputs mediaState and changedState will *not* be stored.
 * @param mediaPresent Pointer to returned media state. True indicates media is present
 * in the device; False indicates no media is present.
 */
IOReturn VoodooFloppyStorageDevice::reportMediaState(bool *mediaPresent, bool *changedState) {
    DBGLOG("VoodooFloppyStorageDevice::reportMediaState()\n");
    
    // Is media present or not?
    *mediaPresent = _mediaPresent;
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
    DBGLOG("VoodooFloppyStorageDevice::reportRemovability()\n");
    
    // Media is removable.
    *isRemovable = true;
    return kIOReturnSuccess;
}

/*!
 * @function reportWriteProtection
 * Report whether the media is write-protected or not.
 * @param isWriteProtected
 * Pointer to returned result. True indicates that the media is write-protected (it
 * cannot be written); False indicates that the media is not write-protected (it
 * is permissible to write).
 */
IOReturn VoodooFloppyStorageDevice::reportWriteProtection(bool *isWriteProtected) {
    DBGLOG("VoodooFloppyStorageDevice::reportWriteProtection()\n");
    
    // Is media write protected?
    *isWriteProtected = _writeProtected;
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyStorageDevice::doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion) {
    IODirection direction = buffer->getDirection();
    IOLog("VoodooFloppyStorageDevice::doAsyncReadWrite(start %llu, %llu blocks, 0x%X)\n", block, nblks, direction);
    //_controller->readDrive(0, buffer, block, nblks, attributes);
    
  /*  IOReturn status = kIOReturnSuccess;
    if (direction == kIODirectionIn) // Rading from device.
        status = _controller->readSectors(this, block, nblks, buffer);
    else if (direction == kIODirectionOut) // Writing to device.
        status = _controller->writeSectors(this, block, nblks, buffer);*/
    
    IOReturn status = _controller->readWriteDrive(buffer, block, nblks, attributes);
    
    if (status != kIOReturnSuccess) {
        // If media is gone, let the upper layers know.
        if (status == kIOReturnNoMedia) {
            IOMediaState mediaState = kIOMediaStateOffline;
            messageClients(kIOMessageMediaStateHasChanged, &mediaState);
        } else if (status == kIOReturnNotWritable) {
            _writeProtected = true;
            messageClients(kIOMessageMediaParametersHaveChanged);
        }
        return status;
    }
    
    IOStorage::complete(completion, kIOReturnSuccess, nblks * _blockSize);
    return kIOReturnSuccess;
}

bool VoodooFloppyStorageDevice::probeMedia() {
    DBGLOG("VoodooFloppyStorageDevice::probeMedia()\n");
    bool newMediaPresent = true;
    
    // Try to calibrate.
    _mediaPresent = false;
    if (_controller->seek(10) != kIOReturnSuccess || _controller->recalibrate() != kIOReturnSuccess)
        newMediaPresent = false;
    else {
        // Try to read track.
        if (_controller->seek(5) != kIOReturnSuccess || _controller->readSectors(5, 0, 5, 1) != kIOReturnSuccess)
            newMediaPresent = false;
    }

    // Did the media state change?
    if (newMediaPresent != _mediaPresent) {
        IOMediaState mediaState = newMediaPresent ? kIOMediaStateOnline : kIOMediaStateOffline;
        messageClients(kIOMessageMediaStateHasChanged, &mediaState);
    }
    _mediaPresent = newMediaPresent;
    return newMediaPresent;
}


/*!
 * @function getDriveNumber
 * Gets the drive number.
 */
UInt8 VoodooFloppyStorageDevice::getDriveNumber() {
    // Return drive number.
    return _driveNumber;
}
