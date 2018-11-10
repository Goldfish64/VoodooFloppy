/*
 * File: VoodooFloppyStorageDevice.hpp
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

#ifndef VoodooFloppyStorageDevice_hpp
#define VoodooFloppyStorageDevice_hpp

#include <IOKit/IOService.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include "VoodooFloppyController.hpp"

#define kFloppyDeviceProductString "Floppy Disk"

// VoodooFloppyStorageDevice class.
class VoodooFloppyStorageDevice : public IOBlockStorageDevice {
    typedef IOService super;
    OSDeclareDefaultStructors(VoodooFloppyStorageDevice);
    
public:
    // IOService overrides.
    bool attach(IOService *provider);
    void detach(IOService *provider);
    
    // IOBlockStorageDevice overrides.
    IOReturn doEjectMedia();
    IOReturn doFormatMedia(UInt64 byteCapacity);
    UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const;
    IOReturn doLockUnlockMedia(bool doLock);
    IOReturn doSynchronizeCache(void);
    char *getVendorString();
    char *getProductString();
    char *getRevisionString();
    char *getAdditionalDeviceInfoString();
    IOReturn reportBlockSize(UInt64 *blockSize);
    IOReturn reportEjectability(bool *isEjectable);
    IOReturn reportLockability(bool *isLockable);
    IOReturn reportRemovability(bool *isRemovable);
    IOReturn reportMaxValidBlock(UInt64 *maxBlock);
    IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0);
    IOReturn reportPollRequirements(bool *pollRequired, bool *pollIsExpensive);
    IOReturn reportWriteProtection(bool *isWriteProtected);
    IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion);
    
    // Floppy functions.
    void probeMedia();
    
    UInt8 getDriveNumber();
    UInt8 getDataRate();
    
    UInt32 getBlockSize();
    
private:
    // Parent controller.
    VoodooFloppyController *_controller;
    
    // Drive properties.
    UInt8 _driveNumber;
    UInt8 _driveType;
    
    UInt8 _dataRate;
    
    bool _mediaPresent;
    bool _writeProtected;
    UInt32 _blockSize;
    UInt64 _maxValidBlock;
};

#endif /* VoodooFloppyStorageDevice_hpp */
