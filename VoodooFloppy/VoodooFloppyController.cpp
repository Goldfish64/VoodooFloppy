//
//  VoodooFloppyController.cpp
//  VoodooFloppy
//
//  Created by John Davis on 11/4/18.
//  Copyright © 2018 Goldfish64. All rights reserved.
//

#include <IOKit/IOLib.h>
#include "IO.h"

#include "VoodooFloppyController.hpp"
#include "VoodooFloppyStorageDevice.hpp"

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppyController, IOService)

bool VoodooFloppyController::start(IOService *provider) {
    IOLog("VoodooFloppyController: start()\n");
    
    // Start superclass first.
    if (!super::start(provider))
        return false;
    
    // Detect drives to see if we should load or not.
    uint8_t driveTypeA, driveTypeB = 0;
    if (!detectDrives(&driveTypeA, &driveTypeB)) {
        IOLog("VoodooFloppyController: No drives found in CMOS. Aborting.\n");
        return false;
    }
    
    // Hook up interrupt handler.
    IOReturn status = getProvider()->registerInterrupt(0, 0, interruptHandler, this);
    if (status != kIOReturnSuccess) {
        IOLog("VoodooFloppyController: Failed to register interrupt handler: 0x%X\n", status);
        return false;
    }
    
    // Enable interrupt.
    irqTriggered = false;
    getProvider()->enableInterrupt(0);
    IOLog("VoodooFloppyController: Registered interrupt handler.\n");
    
    // Reset controller and get version.
    resetController();
    uint8_t version = getControllerVersion();
    
    // If version is 0xFF, that means there isn't a floppy controller.
    if (version == FLOPPY_VERSION_NONE) {
        IOLog("VoodooFloppyController: No floppy controller present. Aborting.\n");
        return false;
    }
    
    // Print version.
    IOLog("VoodooFloppyController: Version: 0x%X.\n", version);
    
    // Configure and reset controller.
    configureController(false, true, false, 0, 0);
    resetController();
    
    // Publish drive A if present.
    if (driveTypeA) {
        IOLog("VoodooFloppyController: Creating VoodooFloppyStorageDevice for drive A.\n");
        VoodooFloppyStorageDevice *floppyDevice = OSTypeAlloc(VoodooFloppyStorageDevice);
        OSDictionary *proper = OSDictionary::withCapacity(1);
        //proper->setObject("test", kIOBlockStorageDeviceTypeGeneric);
        if (!floppyDevice || !floppyDevice->init(proper) || !floppyDevice->attach(this)) {
            IOLog("VoodooFloppyController: failed to create\n");
            OSSafeReleaseNULL(floppyDevice);
        }
        
        floppyDevice->retain();
        floppyDevice->registerService();
    }
    
    return true;
}

void VoodooFloppyController::stop(IOService *provider) {
    IOLog("VoodooFloppyController: stop()\n");
}

void VoodooFloppyController::interruptHandler(OSObject*, void *refCon, IOService*, int) {
    // IRQ was triggered, set flag.
    IOLog("VoodooFloppyController: IRQ raised!\n");
    ((VoodooFloppyController*)refCon)->irqTriggered = true;
}

/**
 * Waits for IRQ6 to be raised.
 * @return True if the IRQ was triggered; otherwise false if it timed out.
 */
bool VoodooFloppyController::waitInterrupt(uint16_t timeout) {
    // Wait until IRQ is triggered or we time out.
    uint8_t ret = false;
    while (!irqTriggered) {
        if(!timeout)
            break;
        timeout--;
        IOSleep(10);
    }
    
    // Did we hit the IRQ?
    if(irqTriggered)
        ret = true;
    else
        IOLog("VoodooFloppyController: IRQ timeout!\n");
    
    // Reset triggered value.
    irqTriggered = false;
    return ret;
}

/**
 * Write a byte to the floppy controller
 * @param data The byte to write.
 */
void VoodooFloppyController::writeData(uint8_t data) {
    for (uint16_t i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
        // Wait until register is ready.
        if (inb(FLOPPY_REG_MSR) & FLOPPY_MSR_RQM) {
            outb(FLOPPY_REG_FIFO, data);
            return;
        }
        IOSleep(10);
    }
    IOLog("VoodooFloppyController: Data timeout!\n");
}

/**
 * Read a byte from the floppy controller
 * @return The byte read. If a timeout occurs, 0.
 */
uint8_t VoodooFloppyController::readData(void) {
    for (uint16_t i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
        // Wait until register is ready.
        if (inb(FLOPPY_REG_MSR) & FLOPPY_MSR_RQM)
            return inb(FLOPPY_REG_FIFO);
        IOSleep(10);
    }
    IOLog("VoodooFloppyController: Data timeout!\n");
    return 0;
}

/**
 * Gets any interrupt data.
 * @param st0 Pointer to ST0 value.
 * @param cyl Pointer to cyl value.
 */
void VoodooFloppyController::senseInterrupt(uint8_t *st0, uint8_t *cyl) {
    // Send command and get result.
    writeData(FLOPPY_CMD_SENSE_INTERRUPT);
    *st0 = readData();
    *cyl = readData();
}

/**
 * Detects floppy drives in CMOS.
 * @return True if drives were found; otherwise false.
 */
bool VoodooFloppyController::detectDrives(uint8_t *outTypeA, uint8_t *outTypeB) {
    // Get data from CMOS.
    IOLog("VoodooFloppyController: Detecting drives from CMOS...\n");
    outb(0x70, 0x10);
    uint8_t types = inb(0x71);
    
    // Drive types.
    const char *driveTypes[6] = { "None", "360KB 5.25\"",
        "1.2MB 5.25\"", "720KB 3.5\"", "1.44MB 3.5\"", "2.88MB 3.5\""};
    
    // Parse drives.
    *outTypeA = types >> 4; // Get high nibble.
    *outTypeB = types & 0xF; // Get low nibble by ANDing out low nibble.
    
    // Did we find any drives?
    if (*outTypeA > FLOPPY_TYPE_2880_35 || *outTypeB > FLOPPY_TYPE_2880_35)
        return false;
    IOLog("VoodooFloppyController: Drive A: %s\nVoodooFloppyController: Drive B: %s\n", driveTypes[*outTypeA], driveTypes[*outTypeB]);
    return (*outTypeA > 0 || *outTypeB > 0);
}

/**
 * Gets the version of the floppy controller.
 * @return The version byte.
 */
uint8_t VoodooFloppyController::getControllerVersion(void) {
    // Send version command and get version.
    writeData(FLOPPY_CMD_VERSION);
    return readData();
}

/**
 * Resets the floppy controller.
 */
void VoodooFloppyController::resetController(void) {
    IOLog("VoodooFloppyController: resetController()\n");
    
    // Disable and re-enable floppy controller.
    outb(FLOPPY_REG_DOR, 0x00);
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_IRQ_DMA | FLOPPY_DOR_RESET);
    waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
    
    // Clear any interrupts on drives.
    uint8_t st0, cyl;
    for(int i = 0; i < 4; i++)
        senseInterrupt(&st0, &cyl);
}

// Configure default values.
// EIS - No Implied Seeks.
// EFIFO - FIFO Disabled.
// POLL - Polling Enabled.
// FIFOTHR - FIFO Threshold Set to 1 Byte.
// PRETRK - Pre-Compensation Set to Track 0.
void VoodooFloppyController::configureController(bool eis, bool efifo, bool poll, uint8_t fifothr, uint8_t pretrk) {
    IOLog("VoodooFloppyController: configureController()\n");
    
    // Send configure command.
    writeData(FLOPPY_CMD_CONFIGURE);
    writeData(0x00);
    uint8_t data = (!eis << 6) | (!efifo << 5) | (poll << 4) | fifothr;
    writeData(data);
    writeData(pretrk);
    
    // Lock configuration.
    writeData(FLOPPY_CMD_LOCK);
}
