//
//  VoodooFloppyStorageDevice.hpp
//  VoodooFloppy
//
//  Created by John Davis on 11/4/18.
//  Copyright © 2018 Goldfish64. All rights reserved.
//

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
    virtual bool attach(IOService *provider) APPLE_KEXT_OVERRIDE;
    virtual void detach(IOService *provider) APPLE_KEXT_OVERRIDE;
    
    IOReturn doEjectMedia() APPLE_KEXT_OVERRIDE;
    IOReturn doFormatMedia(UInt64 byteCapacity) APPLE_KEXT_OVERRIDE;
    UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const APPLE_KEXT_OVERRIDE;
    char *getVendorString() APPLE_KEXT_OVERRIDE;
    char *getProductString() APPLE_KEXT_OVERRIDE;
    char *getRevisionString() APPLE_KEXT_OVERRIDE;
    char *getAdditionalDeviceInfoString() APPLE_KEXT_OVERRIDE;
    IOReturn reportBlockSize(UInt64 *blockSize) APPLE_KEXT_OVERRIDE;
    IOReturn reportEjectability(bool *isEjectable) APPLE_KEXT_OVERRIDE;
    IOReturn reportMaxValidBlock(UInt64 *maxBlock) APPLE_KEXT_OVERRIDE;
    IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) APPLE_KEXT_OVERRIDE;
    IOReturn reportRemovability(bool *isRemovable) APPLE_KEXT_OVERRIDE;
    IOReturn reportWriteProtection(bool *isWriteProtected) APPLE_KEXT_OVERRIDE;
    IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion) APPLE_KEXT_OVERRIDE;
    
    
    
private:
    // Parent controller.
    VoodooFloppyController *_controller;
    
    // Drive properties.
    UInt8 _driveNumber;
    UInt8 _driveType;
    
};

#endif /* VoodooFloppyStorageDevice_hpp */
