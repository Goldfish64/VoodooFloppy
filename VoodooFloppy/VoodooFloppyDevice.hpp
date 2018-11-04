//
//  VoodooFloppyDevice.hpp
//  VoodooFloppy
//
//  Created by John Davis on 11/4/18.
//  Copyright © 2018 Goldfish64. All rights reserved.
//

#ifndef VoodooFloppyDevice_hpp
#define VoodooFloppyDevice_hpp

#include <IOKit/IOService.h>
#include <IOKit/storage/IOBlockStorageDevice.h>

// VoodooFloppyDevice class.
class VoodooFloppyDevice : IOBlockStorageDevice {
    typedef IOService super;
    OSDeclareDefaultStructors(VoodooFloppyDevice);
};

#endif /* VoodooFloppyDevice_hpp */
