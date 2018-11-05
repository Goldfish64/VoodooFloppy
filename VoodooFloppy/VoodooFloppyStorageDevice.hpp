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

// VoodooFloppyStorageDevice class.
class VoodooFloppyStorageDevice : public IOBlockStorageDevice {
    typedef IOService super;
    OSDeclareDefaultStructors(VoodooFloppyStorageDevice);
    
private:
    VoodooFloppyController *_controller;
    
public:
    IOReturn doEjectMedia() override;
    IOReturn doFormatMedia(UInt64 byteCapacity) override;
    UInt32 doGetFormatCapacities(UInt64 *capacities, UInt32 capacitiesMaxCount) const override;
    char *getVendorString() override;
    char *getProductString() override;
    char *getRevisionString() override;
    char *getAdditionalDeviceInfoString() override;
    IOReturn reportBlockSize(UInt64 *blockSize) override;
    IOReturn reportEjectability(bool *isEjectable) override;
    IOReturn reportMaxValidBlock(UInt64 *maxBlock) override;
    IOReturn reportMediaState(bool *mediaPresent, bool *changedState = 0) override;
    IOReturn reportRemovability(bool *isRemovable) override;
    IOReturn reportWriteProtection(bool *isWriteProtected) override;
    IOReturn doAsyncReadWrite(IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes, IOStorageCompletion *completion) override;
    
    virtual bool attach(IOService *provider) override;
    virtual void detach(IOService *provider) override;
};

#endif /* VoodooFloppyStorageDevice_hpp */
