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
#include <IOKit/IODMACommand.h>

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppyController, IOService)

IOReturn VoodooFloppyController::testAction(OSObject *target, void *arg0, void *arg1, void *arg2, void *arg3) {
    while (true) {
        IOSleep(5000);
        IOLog("VoodooFloppy: test");
    }
    return kIOReturnSuccess;
}

bool VoodooFloppyController::init(OSDictionary *dictionary) {
    IOLog("VoodooFloppyController: init()\n");
    if (!super::init(dictionary))
        return false;
    
    // Ensure variables are cleared.
    _driveAType = 0;
    _driveBType = 0;
    _driveADevice = NULL;
    _driveBDevice = NULL;
    
    _workLoop = NULL;
    _tmrMotorOffSource = NULL;
    _irqTriggered = false;
    
    _dmaMemoryDesc = NULL;
    _dmaMemoryMap = NULL;
    _dmaBuffer = NULL;

    return true;
}

IOService *VoodooFloppyController::probe(IOService *provider, SInt32 *score) {
    IOLog("VoodooFloppyController: probe()\n");
    if (!super::probe(provider, score))
        return NULL;
    
    // Detect drives to see if we should match or not.
    if (!detectDrives(&_driveAType, &_driveBType)) {
        IOLog("VoodooFloppyController: No drives found in CMOS. Aborting.\n");
        return NULL;
    }
    
    // We can match since there are drives.
    return this;
}

bool VoodooFloppyController::start(IOService *provider) {
    IOLog("VoodooFloppyController: start()\n");
    if (!super::start(provider))
        return false;
    
    // Create variables.
    UInt8 version;
    IOReturn status;
    
    // Setup new workloop.
    _workLoop = IOWorkLoop::workLoop();
    if (!_workLoop) {
        IOLog("VoodooFloppyController: Failed to create IOWorkLoop.\n");
        goto fail;
    }
    
    // Register interrupt. Since there is only a single interrupt 6 in ioreg, we use 0.
    status = getProvider()->registerInterrupt(0, 0, interruptHandler, this);
    if (status != kIOReturnSuccess) {
        IOLog("VoodooFloppyController: Failed to register interrupt: 0x%X\n", status);
        goto fail;
    }
    
    // Enable interrupt.
    _irqTriggered = false;
    status = getProvider()->enableInterrupt(0);
    if (status != kIOReturnSuccess) {
        IOLog("VoodooFloppyController: Failed to enable interrupt: 0x%X\n", status);
        goto fail;
    }
    
    // Reset controller.
    resetController();
    
    // Get version. If version is 0xFF, that means there isn't a floppy controller.
    version = getControllerVersion();
    if (version == FLOPPY_VERSION_NONE) {
        IOLog("VoodooFloppyController: No floppy controller present.\n");
        goto fail;
    }
    
    // Print version and reset controller once more.
    IOLog("VoodooFloppyController: Version: 0x%X.\n", version);
    resetController();
    
    // Create descriptor for DMA buffer.
    _dmaMemoryDesc = IOMemoryDescriptor::withPhysicalAddress(FLOPPY_DMASTART, FLOPPY_DMALENGTH, kIODirectionInOut);
    if (!_dmaMemoryDesc) {
        IOLog("VoodooFloppyController: Failed to create IOMemoryDescriptor.\n");
        goto fail;
    }
    
    // Map DMA buffer into addressable memory.
    _dmaMemoryMap = _dmaMemoryDesc->map();
    if (!_dmaMemoryMap) {
        IOLog("VoodooFloppyController: Failed to map IOMemoryDescriptor.\n");
        goto fail;
    }
    
    // Get pointer to buffer.
    _dmaBuffer = (UInt8*)_dmaMemoryMap->getAddress();
    IOLog("VoodooFloppyController: Mapped %u bytes at physical address 0x%X.\n", FLOPPY_DMALENGTH, FLOPPY_DMASTART);
    
    // Create IOTimerEventSource for turning off the motor.
    _tmrMotorOffSource = IOTimerEventSource::timerEventSource(this, OSMemberFunctionCast(IOTimerEventSource::Action, this, &VoodooFloppyController::timerHandler));
    if (!_tmrMotorOffSource) {
        IOLog("VoodooFloppyController: Failed to create IOTimerEventSource.\n");
        goto fail;
    }
    
    // Add to work loop.
    status = _workLoop->addEventSource(_tmrMotorOffSource);
    if (status != kIOReturnSuccess) {
        IOLog("VoodooFloppyController: Failed to add IOTimerEventSource to work loop: 0x%X\n", status);
        goto fail;
    }
    
    // Publish drive A if present.
    if (_driveAType) {
       IOLog("VoodooFloppyController: Creating VoodooFloppyStorageDevice for drive A.\n");
        _driveADevice = OSTypeAlloc(VoodooFloppyStorageDevice);
        OSDictionary *proper = OSDictionary::withCapacity(2);
        
        proper->setObject(FLOPPY_IOREG_DRIVE_NUM, OSNumber::withNumber((UInt8)0, 8));
        proper->setObject(FLOPPY_IOREG_DRIVE_TYPE, OSNumber::withNumber(FLOPPY_TYPE_1440_35, 8));
        if (!_driveADevice || !_driveADevice->init(proper) || !_driveADevice->attach(this)) {
            IOLog("VoodooFloppyController: Failed to create VoodooFloppyStorageDevice.\n");
            goto fail;
        }
        
        // Register device.
        _driveADevice->retain();
        _driveADevice->registerService();
    }
    
    // Kext started successfully.
    return true;
    
fail:
    // If we get here that means something failed, so stop the kext.
    IOLog("VoodooFloppyController: Failed to start().\n");
    stop(provider);
    return false;
}

void VoodooFloppyController::stop(IOService *provider) {
    IOLog("VoodooFloppyController::stop()\n");
    
    // Free device objects.
    OSSafeReleaseNULL(_driveADevice);
    OSSafeReleaseNULL(_driveBDevice);
    
    // Release DMA buffer objects.
    OSSafeReleaseNULL(_dmaMemoryMap);
    OSSafeReleaseNULL(_dmaMemoryDesc);
    
    // Free IOTimerEventSource.
    OSSafeReleaseNULL(_tmrMotorOffSource);
    
    // Unregister interrupt.
    getProvider()->disableInterrupt(0);
    getProvider()->unregisterInterrupt(0);
    
    // Free work loop.
    OSSafeReleaseNULL(_workLoop);
}

bool VoodooFloppyController::initDrive(UInt8 driveNumber, UInt8 driveType) {
    // Set drive info (step time = 4ms, load time = 16ms, unload time = 240ms).
    setTransferSpeed(driveType);
    setDriveData(0xC, 0x2, 0xF, true);
    
    // Calibrate drive and switch off motor.
    setMotorOn(driveNumber);
    recalibrate(driveNumber);
    setMotorOff(driveNumber);
    return true;
}


bool VoodooFloppyController::readDrive(UInt8 driveNumber, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes) {
    //seek(driveNumber, 1);
    
    
    
    
    
    //IOSleep(2000);
    //IOLog("VoodooFloppyController: done in sleep in readdirve\n");
    readSectors(driveNumber, block, nblks, buffer);
    
    return true;
}


/*
 *
 * Private functions...
 */

void VoodooFloppyController::interruptHandler(OSObject*, void *refCon, IOService*, int) {
    // IRQ was triggered, set flag.
    IOLog("VoodooFloppyController::interruptHandler()\n");
    ((VoodooFloppyController*)refCon)->_irqTriggered = true;
}

void VoodooFloppyController::timerHandler(OSObject *owner, IOTimerEventSource *sender) {
    setMotorOff(0);
}


/**
 * Waits for IRQ6 to be raised.
 * @return True if the IRQ was triggered; otherwise false if it timed out.
 */
bool VoodooFloppyController::waitInterrupt(UInt16 timeout) {
    // Wait until IRQ is triggered or we time out.
    UInt8 ret = false;
    while (!_irqTriggered) {
        if(!timeout)
            break;
        timeout--;
        IOSleep(10);
    }
    
    // Did we hit the IRQ?
    if(_irqTriggered)
        ret = true;
    else
        IOLog("VoodooFloppyController: IRQ timeout!\n");
    
    // Reset triggered value.
    _irqTriggered = false;
    return ret;
}

/**
 * Write a byte to the floppy controller
 * @param data The byte to write.
 */
void VoodooFloppyController::writeData(UInt8 data) {
    for (UInt16 i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
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
UInt8 VoodooFloppyController::readData(void) {
    for (UInt16 i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
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
void VoodooFloppyController::senseInterrupt(UInt8 *st0, UInt8 *cyl) {
    // Send command and get result.
    writeData(FLOPPY_CMD_SENSE_INTERRUPT);
    *st0 = readData();
    *cyl = readData();
}

/**
 * Sets drive data.
 */
void VoodooFloppyController::setDriveData(UInt8 stepRate, UInt16 loadTime, UInt8 unloadTime, bool dma) {
    // Send specify command.
    writeData(FLOPPY_CMD_SPECIFY);
    UInt8 data = ((stepRate & 0xF) << 4) | (unloadTime & 0xF);
    writeData(data);
    data = (loadTime << 1 | dma ? 0 : 1);
    writeData(data);
}

/**
 * Detects floppy drives in CMOS.
 * @return True if drives were found; otherwise false.
 */
bool VoodooFloppyController::detectDrives(UInt8 *outTypeA, UInt8 *outTypeB) {
    // Get data from CMOS.
    IOLog("VoodooFloppyController: Detecting drives from CMOS...\n");
    outb(0x70, 0x10);
    UInt8 types = inb(0x71);
    
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
UInt8 VoodooFloppyController::getControllerVersion(void) {
    // Send version command and get version.
    IOLog("VoodooFloppyController::getControllerVersion()");
    writeData(FLOPPY_CMD_VERSION);
    return readData();
}

/**
 * Resets the floppy controller.
 */
void VoodooFloppyController::resetController() {
    IOLog("VoodooFloppyController: resetController()\n");
    
    // Send configure command.
    writeData(FLOPPY_CMD_CONFIGURE);
    writeData(0); // Zero.
    UInt8 data = (0 << 6) | (0 << 5) | (1 << 4) | 0; // Implied seek disabled, FIFO enabled, polling disabled, 0 FIFO threshold.
    writeData(data);
    writeData(0); // Zero for pretrack value.
    
    // Lock configuration.
    writeData(FLOPPY_CMD_LOCK);
    
    // Disable and re-enable floppy controller.
    outb(FLOPPY_REG_DOR, 0x00);
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_IRQ_DMA | FLOPPY_DOR_RESET);
    waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
    
    // Clear any interrupts on drives.
    UInt8 st0, cyl;
    for(int i = 0; i < 4; i++)
        senseInterrupt(&st0, &cyl);
}

SInt8 VoodooFloppyController::getMotorNum(UInt8 driveNumber) {
    // Get mask based on drive passed.
    switch (driveNumber) {
        case 0:
            return FLOPPY_DOR_MOT_DRIVE0;
            
        case 1:
            return FLOPPY_DOR_MOT_DRIVE1;
            
        case 2:
            return FLOPPY_DOR_MOT_DRIVE2;
            
        case 3:
            return FLOPPY_DOR_MOT_DRIVE3;
            
        default:
            return -1;
    }
}

bool VoodooFloppyController::setMotorOn(UInt8 driveNumber) {
    // Clear timeout.
    _tmrMotorOffSource->cancelTimeout();
    
    // Get motor number.
    SInt8 motor = getMotorNum(driveNumber);
    if (motor == -1)
        return false;
    
    // If motor is already on, no need to turn it on again.
    if (inb(FLOPPY_REG_DOR) & (driveNumber | motor))
        return true;
    
    // Turn motor on or off and wait 500ms for motor to spin up.
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_RESET | FLOPPY_DOR_IRQ_DMA | driveNumber | motor);
    IOSleep(500);
    return true;
}

bool VoodooFloppyController::setMotorOff(UInt8 driveNumber) {
    SInt8 motor = getMotorNum(driveNumber);
    if (motor == -1)
        return false;
    
    // Turn motor off.
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_RESET | FLOPPY_DOR_IRQ_DMA);
    return true;
}

void VoodooFloppyController::setTransferSpeed(UInt8 driveType) {
    // Determine speed.
    UInt8 speed = FLOPPY_SPEED_500KBPS;
    switch (driveType) {
            // TODO
    }
    
    // Write speed to CCR.
    outb(FLOPPY_REG_CCR, speed & 0x3);
}

/**
 * Recalibrates a drive.
 * @param driveNumber    The drive to recalibrate.
 */
bool VoodooFloppyController::recalibrate(UInt8 driveNumber) {
    UInt8 st0, cyl;
    if (driveNumber >= 4)
        return false;
    
    // Turn on motor and attempt to calibrate.
    IOLog("VoodooFloppyController: Calibrating drive %u...\n", driveNumber);
    for (UInt8 i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++)
    {
        // Send calibrate command.
        writeData(FLOPPY_CMD_RECALIBRATE);
        writeData(driveNumber);
        waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
        senseInterrupt(&st0, &cyl);
        
        // If current cylinder is zero, we are done.
        IOSleep(500);
        if (!cyl) {
            IOLog("VoodooFloppyController: Calibration of drive %u passed!\n", driveNumber);
            return true;
        }
    }
    
    // If we got here, calibration failed.
    IOLog("VoodooFloppyController: Calibration of drive %u failed!\n", driveNumber);
    return false;
}

void VoodooFloppyController::setDma(bool write) {
    // Determine address and length of buffer.
    union {
        UInt8 bytes[4];
        UInt32 data;
    } addr, count;
    addr.data = 0x500;//(UInt32)pmm_dma_get_phys((uintptr_t)buffer);
    count.data = FLOPPY_DMALENGTH - 1;
    
    // Ensure address is under 24 bits, and count is under 16 bits.
    if ((addr.data >> 24) || (count.data >> 16) || (((addr.data & 0xFFFF) + count.data) >> 16))
        panic("FLOPPY: Invalid DMA buffer location!\n");
    
    // https://wiki.osdev.org/ISA_DMA#The_Registers.
    // Mask DMA channel 2 and reset flip-flop.
    outb(0x0A, 0x06);
    outb(0x0C, 0xFF);
    
    // Send address and page register.
    outb(0x04, addr.bytes[0]);
    outb(0x04, addr.bytes[1]);
    outb(0x81, addr.bytes[2]);
    
    // Reset flip-flop and send count.
    outb(0x0C, 0xFF);
    outb(0x05, count.bytes[0]);
    outb(0x05, count.bytes[1]);
    
    // Send read/write mode.
    outb(0x0B, write ? 0x5A : 0x56);
    
    // Unmask DMA channel 2.
    outb(0x0A, 0x02);
}

// Convert LBA to CHS.
void VoodooFloppyController::lbaToChs(UInt32 lba, UInt16* cyl, UInt16* head, UInt16* sector) {
    *cyl = lba / (2 * FLOPPY_SECTORS_PER_TRACK);
    *head = ((lba % (2 * FLOPPY_SECTORS_PER_TRACK)) / FLOPPY_SECTORS_PER_TRACK);
    *sector = ((lba % (2 * FLOPPY_SECTORS_PER_TRACK)) % FLOPPY_SECTORS_PER_TRACK + 1);
}

// Parse and print errors.
UInt8 VoodooFloppyController::parseError(UInt8 st0, UInt8 st1, UInt8 st2) {
    if (st0 & FLOPPY_ST0_INTERRUPT_CODE || st1 > 0 || st2 > 0)
        IOLog("VoodooFloppyController: Error status ST0: 0x%X  ST1: 0x%X  ST2: 0x%X\n", st0, st1, st2);
    
    UInt8 error = 0;
    if (st0 & FLOPPY_ST0_INTERRUPT_CODE) {
        static const char *status[] = { 0, "command did not complete", "invalid command", "polling error" };
        IOLog("An error occurred while getting the sector: %s.\n", status[st0 >> 6]);
        error = 1;
    }
    if (st0 & FLOPPY_ST0_FAIL) {
        IOLog("Drive not ready.\n");
        error = 1;
    }
    if (st1 & FLOPPY_ST1_MISSING_ADDR_MARK || st2 & FLOPPY_ST2_MISSING_DATA_MARK) {
        IOLog("Missing address mark.\n");
        error = 1;
    }
    if (st1 & FLOPPY_ST1_NOT_WRITABLE) {
        IOLog("Disk is write-protected.\n");
        error = 2;
    }
    if (st1 & FLOPPY_ST1_NO_DATA) {
        IOLog("Sector not found.\n");
        error = 1;
    }
    if (st1 & FLOPPY_ST1_OVERRUN_UNDERRUN) {
        IOLog("Buffer overrun/underrun.\n");
        error = 1;
    }
    if (st1 & FLOPPY_ST1_DATA_ERROR) {
        IOLog("CRC error.\n");
        error = 1;
    }
    if (st1 & FLOPPY_ST1_END_OF_CYLINDER) {
        IOLog("End of track.\n");
        error = 1;
    }
    if (st2 & FLOPPY_ST2_BAD_CYLINDER) {
        IOLog("Bad track.\n");
        error = 1;
    }
    if (st2 & FLOPPY_ST2_WRONG_CYLINDER) {
        IOLog("Wrong track.\n");
        error = 1;
    }
    if (st2 & FLOPPY_ST2_DATA_ERROR_IN_FIELD) {
        IOLog("CRC error in data.\n");
        error = 1;
    }
    if (st2 & FLOPPY_ST2_CONTROL_MARK) {
        IOLog("Deleted address mark.\n");
        error = 1;
    }
    
    return error;
}

// Seek to specified track.
bool VoodooFloppyController::seek(UInt8 driveNumber, UInt8 track) {
    UInt8 st0, cyl = 0;
    if (driveNumber >= 4)
        return false;
    
    // Attempt seek.
    for (UInt8 i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
        // Send seek command.
        IOLog("VoodooFloppyController: Seeking to track %u...\n", track);
        writeData(FLOPPY_CMD_SEEK);
        writeData((0 << 2) | driveNumber); // Head 0, drive.
        writeData(track);
        
        // Wait for response and check interrupt.
        waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
        senseInterrupt(&st0, &cyl);
        
        // Ensure command completed successfully.
        if (st0 & FLOPPY_ST0_INTERRUPT_CODE) {
            IOLog("VoodooFloppyController: Error executing floppy seek command!\n");
            continue;
        }
        
        // If we have reached the requested track, return.
        if (cyl == track) {
            //IOSleep(500);
            return true;
        }
    }
    
    // Seek failed if we get here.
    IOLog("VoodooFloppyController: Seek failed for %u on drive %u!\n", track, driveNumber);
    return false;
}

SInt8 VoodooFloppyController::readTrack(UInt8 driveNumber, UInt8 track) {
    // Set drive info (step time = 4ms, load time = 16ms, unload time = 240ms).
    setTransferSpeed(0); // TODO type.
    setDriveData(0xC, 0x2, 0xF, true);
    
    for (UInt8 i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
        // Initialize DMA.
        setDma(false);
        
        // Send read command to disk to read both sides of track.
        IOLog("VoodooFloppyController: Attempting to read track %u...\n", track);
        writeData(FLOPPY_CMD_READ_DATA | FLOPPY_CMD_EXT_SKIP | FLOPPY_CMD_EXT_MFM | FLOPPY_CMD_EXT_MT);
        writeData(0 << 2 | driveNumber);
        writeData(track);     // Track.
        writeData(0);         // Head 0.
        writeData(1);        // Start at sector 1.
        writeData(FLOPPY_BYTES_SECTOR_512);
        writeData(18);        // 18 sectors per track?
        writeData(FLOPPY_GAP3_3_5);
        writeData(0xFF);
        
        // Wait for IRQ.
        waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
        
        // Get status registers.
        UInt8 st0 = readData();
        UInt8 st1 = readData();
        UInt8 st2 = readData();
        readData(); // Track.
        readData(); // Head.
        readData(); // Sector.
        readData(); // Bytes per sector.
        
        // Determine errors if any.
        UInt8 error = parseError(st0, st1, st2);
        
        // If no error, we are done.
        if (!error) {
            return 0;
        }
        else if (error > 1) {
            IOLog("VoodooFloppyController: Not retrying...\n");
            return 2;
        }
        
        IOSleep(100);
    }
    
    // Failed.
    IOLog("VoodooFloppyController: Get track failed!\n");
    return -1;
}

bool VoodooFloppyController::readSectors(UInt8 driveNumber, UInt32 sectorLba, UInt64 sectorCount, IOMemoryDescriptor *buffer) {
    // Ensure drive is valid.
    if (driveNumber >= 4)
        return false;
    
    // Turn on motor.
    setMotorOn(driveNumber);
    
    // Get each block.
  //  UInt32 remainingLength = 0;
    UInt32 bufferOffset = 0;
    static UInt16 lastTrack = -1;
    
    for (UInt32 i = 0; i < sectorCount; i++) {
        // Convert LBA to CHS.
        UInt16 head = 0, track = 0, sector = 1;
        lbaToChs(sectorLba, &track, &head, &sector);
        
        // Have we changed tracks?.
        if (lastTrack != track) {
            if (lastTrack != track && !seek(driveNumber, track)) {
                setMotorOff(driveNumber);
                return false;
            }
            
            // Get track.
            lastTrack = track;
            readTrack(driveNumber, track);
        }
        
       // UInt32 size = remainingLength;
        //if (size > 512)
         //   size = 512;
        
        // Copy data.
        UInt32 headOffset = head == 1 ? (18 * 512) : 0;
        //memcpy(outBuffer + bufferOffset, floppyDrive->DmaBuffer + ((sector - 1) * 512) + headOffset, size);
        buffer->writeBytes(bufferOffset, _dmaBuffer + ((sector - 1) * 512) + headOffset, 512);
        
        // Move to next sector.
        sectorLba++;
       // remainingLength -= size;
        bufferOffset += 512;
    }
    
    //setMotorOff(driveNumber);
    _tmrMotorOffSource->setTimeoutMS(2000);
    return true;
}
