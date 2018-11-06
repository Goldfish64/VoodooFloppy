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
#include <IOKit/IOBufferMemoryDescriptor.h>

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
    _irqTriggered = false;
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
    configureController(false, true, false, 7, 0);
    resetController();
    
   // IOWorkLoop* pWorkLoop = getWorkLoop();
   // _cmdGate = IOCommandGate::commandGate(this);
   // pWorkLoop->retain();
   // pWorkLoop->addEventSource(_cmdGate);
    //_cmdGate->enable();
   // IOLog("VoodooFloppy: 0x%X\n", _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooFloppyController::testAction)));
    
   /* IOBufferMemoryDescriptor *txBufDesc = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, (kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMapInhibitCache), 64000, 0x00000000FFFFF000ULL);
    txBufDesc->prepare();
    
    
    
    IODMACommand *dmaCommand = IODMACommand::withSpecification(kIODMACommandOutputHost32, 32, 0, IODMACommand::kMapped, 0, 1);
    if (!dmaCommand)
        IOLog("VoodooFloppyController: failed to dma\n");
    dmaCommand->setMemoryDescriptor(txBufDesc);
    
    UInt64 offset = 0;
    UInt32 numSegs = 1;
    IODMACommand::Segment32 seg;
    if (dmaCommand->gen32IOVMSegments(&offset, &seg, &numSegs) != kIOReturnSuccess) {
        IOLog("VoodooFloppyController: failed to dma gen\n");
    }
    
    IOLog("Voodooflop: 0x%X\n", seg.fIOVMAddr);*/
    
    IOMemoryDescriptor *memDesc = IOMemoryDescriptor::withPhysicalAddress(0x700, FLOPPY_DMALENGTH, kIODirectionInOut);
    if (!memDesc) {
        IOLog("VoodooFloppy: failed memdesc\n");
    }
    IOMemoryMap *memMam = memDesc->map();
    if (!memMam) {
        IOLog("VoodooFloppy: failed memMam\n");
    }
    uint64_t dd = memMam->getAddress();
    IOLog("VoodooFloppy: 0x%llX\n", dd);
    
    uint8_t *bytes = (uint8_t*)dd;
    
    IOSleep(5000);
    
    for (int i = 0; i < 0x4800; i++) {
        if (bytes[i] > 0)
            IOLog("0x%X ", bytes[i]);
    }
    
    IOSleep(5000);
    
  /*  for (int i = 0; i < 0x4800; i++) {
        bytes[i] = 0x43;
    }*/
    
    readSectors(0, 0, NULL, 0);
    
    IOSleep(5000);
    
    for (int i = 0; i < 512; i++) {
        IOLog("0x%X ", bytes[i]);
    }
    
    IOLog("VoodooFloppyController: MSR: 0x%X\n", inb(FLOPPY_REG_MSR));
    
    // Publish drive A if present.
    if (driveTypeA) {
       /* IOLog("VoodooFloppyController: Creating VoodooFloppyStorageDevice for drive A.\n");
        VoodooFloppyStorageDevice *floppyDevice = OSTypeAlloc(VoodooFloppyStorageDevice);
        OSDictionary *proper = OSDictionary::withCapacity(2);
        
        proper->setObject(FLOPPY_IOREG_DRIVE_NUM, OSNumber::withNumber((uint8_t)0, 8));
        proper->setObject(FLOPPY_IOREG_DRIVE_TYPE, OSNumber::withNumber(FLOPPY_TYPE_1440_35, 8));
        if (!floppyDevice || !floppyDevice->init(proper) || !floppyDevice->attach(this)) {
            IOLog("VoodooFloppyController: failed to create\n");
            OSSafeReleaseNULL(floppyDevice);
        }
        
        floppyDevice->retain();
        floppyDevice->registerService();*/
        
       // readSectors(0, 0, NULL, 0);
    }
    
    return true;
}

void VoodooFloppyController::stop(IOService *provider) {
    IOLog("VoodooFloppyController: stop()\n");
}

bool VoodooFloppyController::initDrive(uint8_t driveNumber, uint8_t driveType) {
    // Set drive info (step time = 4ms, load time = 16ms, unload time = 240ms).
    setTransferSpeed(driveType);
    setDriveData(0xC, 0x2, 0xF, true);
    
    // Calibrate drive and switch off motor.
    setMotorOn(driveNumber);
    recalibrate(driveNumber);
    setMotorOff(driveNumber);
    return true;
}


bool VoodooFloppyController::readDrive(uint8_t driveNumber, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks, IOStorageAttributes *attributes) {
    //seek(driveNumber, 1);
    
    
    
    
    
    //IOSleep(2000);
    IOLog("VoodooFloppyController: done in sleep in readdirve\n");
    return true;
}


/*
 *
 * Private functions...
 */


void VoodooFloppyController::interruptHandler(OSObject*, void *refCon, IOService*, int) {
    // IRQ was triggered, set flag.
    IOLog("VoodooFloppyController: IRQ raised!\n");
    ((VoodooFloppyController*)refCon)->_irqTriggered = true;
}

/**
 * Waits for IRQ6 to be raised.
 * @return True if the IRQ was triggered; otherwise false if it timed out.
 */
bool VoodooFloppyController::waitInterrupt(uint16_t timeout) {
    // Wait until IRQ is triggered or we time out.
    uint8_t ret = false;
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
 * Sets drive data.
 */
void VoodooFloppyController::setDriveData(uint8_t stepRate, uint16_t loadTime, uint8_t unloadTime, bool dma) {
    // Send specify command.
    writeData(FLOPPY_CMD_SPECIFY);
    uint8_t data = ((stepRate & 0xF) << 4) | (unloadTime & 0xF);
    writeData(data);
    data = (loadTime << 1 | dma ? 0 : 1);
    writeData(data);
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

int8_t VoodooFloppyController::getMotorNum(uint8_t driveNumber) {
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

bool VoodooFloppyController::setMotorOn(uint8_t driveNumber) {
    int8_t motor = getMotorNum(driveNumber);
    if (motor == -1)
        return false;
    
    // Turn motor on or off and wait 500ms for motor to spin up.
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_RESET | FLOPPY_DOR_IRQ_DMA | driveNumber | motor);
    IOSleep(500);
    return true;
}

bool VoodooFloppyController::setMotorOff(uint8_t driveNumber) {
    int8_t motor = getMotorNum(driveNumber);
    if (motor == -1)
        return false;
    
    // Turn motor off.
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_RESET | FLOPPY_DOR_IRQ_DMA);
    return true;
}

void VoodooFloppyController::setTransferSpeed(uint8_t driveType) {
    // Determine speed.
    uint8_t speed = FLOPPY_SPEED_500KBPS;
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
bool VoodooFloppyController::recalibrate(uint8_t driveNumber) {
    uint8_t st0, cyl;
    if (driveNumber >= 4)
        return false;
    
    // Turn on motor and attempt to calibrate.
    IOLog("VoodooFloppyController: Calibrating drive %u...\n", driveNumber);
    for (uint8_t i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++)
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
        uint8_t bytes[4];
        uint32_t data;
    } addr, count;
    addr.data = 0x700;//(uint32_t)pmm_dma_get_phys((uintptr_t)buffer);
    count.data = 0x4800 - 1;
    
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
void VoodooFloppyController::lbaToChs(uint32_t lba, uint16_t* cyl, uint16_t* head, uint16_t* sector) {
    *cyl = lba / (2 * FLOPPY_SECTORS_PER_TRACK);
    *head = ((lba % (2 * FLOPPY_SECTORS_PER_TRACK)) / FLOPPY_SECTORS_PER_TRACK);
    *sector = ((lba % (2 * FLOPPY_SECTORS_PER_TRACK)) % FLOPPY_SECTORS_PER_TRACK + 1);
}

// Parse and print errors.
uint8_t VoodooFloppyController::parseError(uint8_t st0, uint8_t st1, uint8_t st2) {
    if (st0 & FLOPPY_ST0_INTERRUPT_CODE || st1 > 0 || st2 > 0)
        IOLog("VoodooFloppyController: Error status ST0: 0x%X  ST1: 0x%X  ST2: 0x%X\n", st0, st1, st2);
    
    uint8_t error = 0;
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
bool VoodooFloppyController::seek(uint8_t driveNumber, uint8_t track) {
    uint8_t st0, cyl = 0;
    if (driveNumber >= 4)
        return false;
    
    // Attempt seek.
    for (uint8_t i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
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
            IOSleep(500);
            return true;
        }
    }
    
    // Seek failed if we get here.
    IOLog("VoodooFloppyController: Seek failed for %u on drive %u!\n", track, driveNumber);
    return false;
}

int8_t VoodooFloppyController::readSector(uint8_t driveNumber, uint8_t head, uint8_t track, uint8_t sector) {
    // Set drive info (step time = 4ms, load time = 16ms, unload time = 240ms).
    setTransferSpeed(0);
    setDriveData(0xC, 0x2, 0xF, true);
    
    for (uint8_t i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
        // Initialize DMA.
       // uint32_t dmaLength = 2 * 18 * 512;
        //floppy_dma_set(floppyDrive->DmaBuffer, dmaLength, false);
        setDma(false);
        
        // Send read command to disk to read both sides of track.
        head = 0;
        track = 0;
        sector = 1;
        driveNumber = 0;
        IOLog("VoodooFloppyController: Attempting to read head %u track %u secont %u...\n", head, track, sector);
        IOLog("VoodooFloppyController: got result MSR: 0x%X\n", inb(FLOPPY_REG_MSR));
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
        //while (!(inb(FLOPPY_REG_MSR) & FLOPPY_MSR_RQM));
        IOLog("VoodooFloppyController: got result MSR: 0x%X\n", inb(FLOPPY_REG_MSR));
        

        
        // Get status registers.
        uint8_t st0 = readData();
        uint8_t st1 = readData();
        uint8_t st2 = readData();
        uint8_t rTrack = readData();
        uint8_t rHead = readData();
        uint8_t rSector = readData();
        uint8_t bytesPerSector = readData();
        IOLog("VoodooFloppyController: track: %u, head: %u, sector: %u\n", rTrack, rHead, rSector);
        IOLog("VoodooFloppyController: MSR: 0x%X\n", inb(FLOPPY_REG_MSR));
        
       /* if (inb(FLOPPY_REG_MSR) & FLOPPY_MSR_NON_DMA) {
            for (int i = 0; i < 512; i++) {
                // waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
                IOLog("0x%X ", readData());
            }
        }*/
        
        
        // Determine errors if any.
        uint8_t error = parseError(st0, st1, st2);
        
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

bool VoodooFloppyController::readSectors(uint8_t driveNumber, uint32_t sectorLba, uint8_t *outBuffer, uint32_t length) {
    // Ensure drive is valid.
    if (driveNumber >= 4)
        return false;
    
    // Turn on motor.
    setMotorOn(driveNumber);
    
    // Get each block.
    uint32_t remainingLength = length;
    uint32_t bufferOffset = 0;
    uint16_t lastTrack = -1;
    
    // Determine number of sectors.
    uint32_t totalSectors = 1;// DIVIDE_ROUND_UP(length, 512); // TODO change.
    
    for (uint32_t i = 0; i < totalSectors; i++) {
        // Convert LBA to CHS.
        uint16_t head = 0, track = 0, sector = 1;
        lbaToChs(sectorLba, &track, &head, &sector);
        
        // Have we changed tracks?.
        if (lastTrack != track) {
            if (lastTrack != track && !seek(driveNumber, track)) {
                setMotorOff(driveNumber);
                return false;
            }
            
            // Get track.
            lastTrack = track;
            //readTrack(driveNumber, track);
            readSector(driveNumber, head, track, sector);
            
        }
        
        uint32_t size = remainingLength;
        if (size > 512)
            size = 512;
        
        // Copy data.
        uint32_t headOffset = head == 1 ? (18 * 512) : 0;
        //memcpy(outBuffer + bufferOffset, floppyDrive->DmaBuffer + ((sector - 1) * 512) + headOffset, size);
        
        // Move to next sector.
        sectorLba++;
        remainingLength -= size;
        bufferOffset += 512;
    }
    
    
    setMotorOff(driveNumber);
    return true;
}
