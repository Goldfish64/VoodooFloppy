/*
 * File: VoodooFloppyController.cpp
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
#include "IO.h"

#include "VoodooFloppyController.hpp"
#include "VoodooFloppyStorageDevice.hpp"
#include <IOKit/IODMACommand.h>

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppyController, IOService)

// Power states.
enum {
    kFloppyPowerStateSleep  = 0,
    kFloppyPowerStateNormal = 1,
    kFloppyPowerStateCount
};

static const IOPMPowerState FloppyPowerStateArray[kFloppyPowerStateCount] = {
    { 1, 0,0,0,0,0,0,0,0,0,0,0 },
    { 1, kIOPMDeviceUsable, IOPMPowerOn, IOPMPowerOn, 0,0,0,0,0,0,0,0 }
};

/*! @function init
 @abstract Initializes generic IOService data structures (expansion data, etc). */
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

/*! @function probe
 @abstract During an IOService object's instantiation, probes a matched service to see if it can be used.
 @discussion The registration process for an IOService object (the provider) includes instantiating possible driver clients. The <code>probe</code> method is called in the client instance to check the matched service can be used before the driver is considered to be started. Since matching screens many possible providers, in many cases the <code>probe</code> method can be left unimplemented by IOService subclasses. The client is already attached to the provider when <code>probe</code> is called.
 @param provider The registered IOService object that matches a driver personality's matching dictionary.
 @param score Pointer to the current driver's probe score, which is used to order multiple matching drivers in the same match category. It defaults to the value of the <code>IOProbeScore</code> property in the drivers property table, or <code>kIODefaultProbeScore</code> if none is specified. The <code>probe</code> method may alter the score to affect start order.
 @result An IOService instance or zero when the probe is unsuccessful. In almost all cases the value of <code>this</code> is returned on success. If another IOService object is returned, the probed instance is detached and freed, and the returned instance is used in its stead for <code>start</code>. */
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

/*! @function start
 @abstract During an IOService object's instantiation, starts the IOService object that has been selected to run on the provider.
 @discussion The <code>start</code> method of an IOService instance is called by its provider when it has been selected (due to its probe score and match category) as the winning client. The client is already attached to the provider when <code>start</code> is called.<br>Implementations of <code>start</code> must call <code>start</code> on their superclass at an appropriate point. If an implementation of <code>start</code> has already called <code>super::start</code> but subsequently determines that it will fail, it must call <code>super::stop</code> to balance the prior call to <code>super::start</code> and prevent reference leaks.
 @result <code>true</code> if the start was successful; <code>false</code> otherwise (which will cause the instance to be detached and usually freed). */
bool VoodooFloppyController::start(IOService *provider) {
    DBGLOG("VoodooFloppyController: start()\n");
    if (!super::start(provider))
        return false;
    
    // Initialize power management and join tree.
    PMinit();
    registerPowerDriver(this, (IOPMPowerState*)FloppyPowerStateArray, kFloppyPowerStateCount);
    provider->joinPMtree(this);
    
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
    
    // Print version and configure controller.
    IOLog("VoodooFloppyController: Version: 0x%X.\n", version);
    configureController();
    
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
    
    // Create IOCommandGate.
    _cmdGate = IOCommandGate::commandGate(this);
    if (!_cmdGate) {
        IOLog("VoodooFloppyController: Failed to create IOCommandGate.\n");
        goto fail;
    }
    
    // Add to work loop.
    status = _workLoop->addEventSource(_cmdGate);
    if (status != kIOReturnSuccess) {
        IOLog("VoodooFloppyController: Failed to add IOCommandGate to work loop: 0x%X\n", status);
        goto fail;
    }
    
    // Publish drive A if present.
    if (_driveAType) {
       IOLog("VoodooFloppyController: Creating VoodooFloppyStorageDevice for drive A.\n");
        _driveADevice = OSTypeAlloc(VoodooFloppyStorageDevice);
        _currentDevice = _driveADevice;
        
        OSDictionary *proper = OSDictionary::withCapacity(2);
        
        proper->setObject(kFloppyPropertyDriveIdKey, OSNumber::withNumber((UInt8)0, 8));
        proper->setObject(FLOPPY_IOREG_DRIVE_TYPE, OSNumber::withNumber(_driveAType, 8));
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
    IOLog("VoodooFloppyController::start(): fail.\n");
    stop(provider);
    return false;
}

/*! @function stop
 @abstract During an IOService termination, the stop method is called in its clients before they are detached & it is destroyed.
 @discussion The termination process for an IOService (the provider) will call stop in each of its clients, after they have closed the provider if they had it open, or immediately on termination. */
void VoodooFloppyController::stop(IOService *provider) {
    DBGLOG("VoodooFloppyController::stop()\n");
    
    // Free device objects.
    OSSafeReleaseNULL(_driveADevice);
    OSSafeReleaseNULL(_driveBDevice);
    
    // Free command gate.
    OSSafeReleaseNULL(_cmdGate);
    
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
    super::stop(provider);
}

/*! @function setPowerState
 @abstract Requests a power managed driver to change the power state of its device.
 @discussion A power managed driver must override <code>setPowerState</code> to take part in system power management. After a driver is registered with power management, the system uses <code>setPowerState</code> to power the device off and on for system sleep and wake.
 Calls to @link PMinit PMinit@/link and @link registerPowerDriver registerPowerDriver@/link enable power management to change a device's power state using <code>setPowerState</code>. <code>setPowerState</code> is called in a clean and separate thread context.
 @param powerStateOrdinal The number in the power state array of the state the driver is being instructed to switch to.
 @param whatDevice A pointer to the power management object which registered to manage power for this device. In most cases, <code>whatDevice</code> will be equal to your driver's own <code>this</code> pointer.
 @result The driver must return <code>IOPMAckImplied</code> if it has complied with the request when it returns. Otherwise if it has started the process of changing power state but not finished it, the driver should return a number of microseconds which is an upper limit of the time it will need to finish. Then, when it has completed the power switch, it should call @link acknowledgeSetPowerState acknowledgeSetPowerState@/link. */
IOReturn VoodooFloppyController::setPowerState(unsigned long powerStateOrdinal, IOService *whatDevice) {
    DBGLOG("VoodooFloppyController::setPowerState()\n");
    
    // Call super.
    IOReturn status = super::setPowerState(powerStateOrdinal, whatDevice);
    if (status != kIOReturnSuccess)
        return status;
    
    // If the gate is not yet created, we can't do anything yet.
    if (!_cmdGate)
        return kIOReturnSuccess;
    
    // Wake up command gate if we moving to normal power state.
    if (powerStateOrdinal == kFloppyPowerStateNormal)
        _cmdGate->enable();
    return _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooFloppyController::setPowerStateGated), &powerStateOrdinal);
}

IOReturn VoodooFloppyController::probeDriveMedia(VoodooFloppyStorageDevice *floppyDevice) {
    return _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooFloppyController::probeMediaGated), floppyDevice);
}

IOReturn VoodooFloppyController::readWriteDrive(VoodooFloppyStorageDevice *floppyDevice, IOMemoryDescriptor *buffer, UInt64 block, UInt64 nblks) {
    return _cmdGate->runAction(OSMemberFunctionCast(IOCommandGate::Action, this, &VoodooFloppyController::readWriteGated), floppyDevice, buffer, &block, &nblks);
}

void VoodooFloppyController::selectDrive(VoodooFloppyStorageDevice *floppyDevice) {
    if (_currentDevice == floppyDevice)
        return;
    
    // Set current drive.
    _currentDevice = floppyDevice;
    
    // Determine speed.
    UInt8 speed = FLOPPY_SPEED_500KBPS;
    switch (0) {
            // TODO
    }
    
    // Recalibrate drive.
    recalibrate();
}


/*
 *
 * Private functions...
 */

void VoodooFloppyController::interruptHandler(OSObject *target, void *refCon, IOService *nub, int source) {
    // IRQ was triggered, set flag.
    //DBGLOG("VoodooFloppyController::interruptHandler()\n");
    ((VoodooFloppyController*)refCon)->_irqTriggered = true;
}

void VoodooFloppyController::timerHandler(OSObject *owner, IOTimerEventSource *sender) {
    // Turn off motor after inactivity.
    //DBGLOG("VoodooFloppyController::timerHandler()\n");
    setMotorOff();
}

IOReturn VoodooFloppyController::setPowerStateGated(UInt32 *powerState) {
    switch (*powerState) {
        case kFloppyPowerStateNormal:
            // Reconfigure and reset controller.
            resetController();
            configureController();
            
            break;
            
        case kFloppyPowerStateSleep:
            // Disable gate to prevent further actions.
            _cmdGate->disable();
            break;
    }
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyController::readWriteGated(VoodooFloppyStorageDevice *floppyDevice, IOMemoryDescriptor *buffer, UInt64 *block, UInt64 *nblks) {
    DBGLOG("VoodooFloppyController::readWriteGated()\n");
    
    // Ensure buffer direction is valid.
    IODirection bufferDirection = buffer->getDirection();
    if (bufferDirection != kIODirectionIn && bufferDirection != kIODirectionOut)
        return kIOReturnInvalid;
    
    // Determine if we are reading or writing.
    bool write = bufferDirection == kIODirectionOut;
    
    // Select drive.
    selectDrive(floppyDevice);
    
    // Read/write sectors.
    UInt32 bufferOffset = 0;
    UInt16 lastTrack = -1;
    UInt32 currentSectorLba = (UInt32)*block;
    UInt64 remainingSectors = *nblks;
    while (currentSectorLba < *block + *nblks) {
        // Convert LBA to CHS.
        UInt16 head = 0, track = 0, sector = 1;
        lbaToChs(currentSectorLba, &track, &head, &sector);
        
        // Have we changed tracks?. If so we need to seek.
        if (lastTrack != track) {
            IOReturn status = seek(track);
            if (status != kIOReturnSuccess)
                return status;
            lastTrack = track;
        }
        
        // Variables used for determing remaining sectors in track.
        UInt16 nextHead = 0, nextTrack = 0, nextSector = 1;
        UInt32 nextSectorLba = currentSectorLba;
        UInt8 nextSectorCount = 0;
        
        // Calculate remaining sectors in track.
        do {
            nextSectorLba++;
            nextSectorCount++;
            lbaToChs(nextSectorLba, &nextTrack, &nextHead, &nextSector);
        } while (nextTrack == track && nextSectorCount < remainingSectors);
        
        // Determine total bytes.
        IOByteCount byteCount = nextSectorCount * floppyDevice->getBlockSize();
        
        // Are we writing? If so we need to write data to DMA buffer.
        if (write && buffer->readBytes(bufferOffset, _dmaBuffer, byteCount) != byteCount)
            return kIOReturnIOError;
        
        // Read/write sectors from/to disk.
        IOReturn status = readWriteSectors(write, track, head, sector, nextSectorCount);
        if (status != kIOReturnSuccess)
            return status;
        
        // Are we reading? If so we need to read data from DMA buffer.
        if (!write && buffer->writeBytes(bufferOffset, _dmaBuffer, byteCount) != byteCount)
            return kIOReturnIOError;
        
        // Move to next sector.
        currentSectorLba += nextSectorCount;
        remainingSectors -= nextSectorCount;
        bufferOffset += 512 * nextSectorCount;
    }
    
    // Operation was successful.
    return kIOReturnSuccess;
}

IOReturn VoodooFloppyController::probeMediaGated(VoodooFloppyStorageDevice *floppyDevice) {
    DBGLOG("VoodooFloppyController::probeMediaGated()\n");
    selectDrive(floppyDevice);
    
    // Try to calibrate to check if media is present.
    if (seek(10) != kIOReturnSuccess || recalibrate() != kIOReturnSuccess)
        return kIOReturnNoMedia;
    else {
        // Try to read track.
        if (seek(5) != kIOReturnSuccess || readWriteSectors(false, 5, 0, 5, 1) != kIOReturnSuccess)
            return kIOReturnNoMedia;
    }
    
    // Media is present.
    return kIOReturnSuccess;
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
bool VoodooFloppyController::writeData(UInt8 data) {
    for (UInt16 i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
        // Wait until register is ready.
        if (inb(FLOPPY_REG_MSR) & FLOPPY_MSR_RQM) {
            outb(FLOPPY_REG_FIFO, data);
            return true;
        }
        IOSleep(10);
    }
    DBGLOG("VoodooFloppyController: Data timeout!\n");
    return false;
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
    DBGLOG("VoodooFloppyController: Data timeout!\n");
    return 0xFF;
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
UInt8 VoodooFloppyController::getControllerVersion() {
    DBGLOG("VoodooFloppyController::getControllerVersion()");
    
    // Send version command and get version.
    writeData(FLOPPY_CMD_VERSION);
    return readData();
}

void VoodooFloppyController::configureController() {
    DBGLOG("VoodooFloppyController::configureController()\n");
    
    // Send configure command.
    writeData(FLOPPY_CMD_CONFIGURE);
    writeData(0); // Zero.
    UInt8 data = (0 << 6) | (0 << 5) | (1 << 4) | 0; // Implied seek disabled, FIFO enabled, polling disabled, 0 FIFO threshold.
    writeData(data);
    writeData(0); // Zero for pretrack value.
    
    // Lock configuration.
    writeData(FLOPPY_CMD_LOCK);
    
    // Reset controller.
    resetController();
}

/**
 * Resets the floppy controller.
 */
void VoodooFloppyController::resetController() {
    DBGLOG("VoodooFloppyController::resetController()\n");
    
    // Disable and re-enable floppy controller.
    outb(FLOPPY_REG_DOR, 0x00);
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_IRQ_DMA | FLOPPY_DOR_RESET);
    waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
    
    // Clear any interrupts on drives.
    UInt8 st0, cyl;
    for (int i = 0; i < 4; i++)
        senseInterrupt(&st0, &cyl);
}

bool VoodooFloppyController::isControllerReady() {
    //DBGLOG("VoodooFloppyController::isControllerReady()\n");
    
    // Ensure we can send a command and that no operations are in progress.
    bool result = (inb(FLOPPY_REG_MSR) & (FLOPPY_MSR_RQM | FLOPPY_MSR_DIO)) == FLOPPY_MSR_RQM;
    
    // If controller is not ready, reset and try again.
    // If it's still not ready, fail.
    if (!result) {
        DBGLOG("VoodooFloppyController::isControllerReady(): not ready\n");
        resetController();
        if ((inb(FLOPPY_REG_MSR) & (FLOPPY_MSR_RQM | FLOPPY_MSR_DIO)) != FLOPPY_MSR_RQM)
            return false;
    }
    
    // Controller is ready.
    return true;
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

bool VoodooFloppyController::setMotorOn() {
    //DBGLOG("VoodooFloppyController::setMotorOn()\n");
    
    // Clear motor off timeout.
    _tmrMotorOffSource->cancelTimeout();
    
    // Get motor number.
    UInt8 driveNumber = _currentDevice->getDriveNumber();
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

bool VoodooFloppyController::setMotorOff() {
    DBGLOG("VoodooFloppyController::setMotorOff()\n");
    UInt8 driveNumber = _currentDevice->getDriveNumber();
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



void VoodooFloppyController::setDma(UInt32 length, bool write) {
    // Ensure length is within limits.
    if (length > FLOPPY_DMALENGTH)
        length = FLOPPY_DMALENGTH;
    
    // Determine address and length of buffer.
    union {
        UInt8 bytes[4];
        UInt32 data;
    } addr, count;
    addr.data = FLOPPY_DMASTART;
    count.data = length - 1;
    
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
IOReturn VoodooFloppyController::parseError(UInt8 st0, UInt8 st1, UInt8 st2) {
    if (st0 & FLOPPY_ST0_INTERRUPT_CODE || st1 > 0 || st2 > 0)
        DBGLOG("VoodooFloppyController: Error status ST0: 0x%X  ST1: 0x%X  ST2: 0x%X\n", st0, st1, st2);
    
    IOReturn error = kIOReturnSuccess;
    if (st0 & FLOPPY_ST0_INTERRUPT_CODE) {
        static const char *status[] = { 0, "command did not complete", "invalid command", "polling error" };
        DBGLOG("VoodooFloppyController: An error occurred while getting the sector: %s.\n", status[st0 >> 6]);
        error = kIOReturnIOError;
    }
    if (st0 & FLOPPY_ST0_FAIL) {
        DBGLOG("VoodooFloppyController: Drive not ready.\n");
        error = kIOReturnNotReady;
    }
    if (st1 & FLOPPY_ST1_MISSING_ADDR_MARK || st2 & FLOPPY_ST2_MISSING_DATA_MARK) {
        DBGLOG("VoodooFloppyController: Missing address mark.\n");
        error = kIOReturnIOError;
    }
    if (st1 & FLOPPY_ST1_NOT_WRITABLE) {
        DBGLOG("VoodooFloppyController: Disk is write-protected.\n");
        error = kIOReturnNotWritable;
    }
    if (st1 & FLOPPY_ST1_NO_DATA) {
        DBGLOG("VoodooFloppyController: Sector not found.\n");
        error = kIOReturnIOError;
    }
    if (st1 & FLOPPY_ST1_OVERRUN_UNDERRUN) {
        DBGLOG("VoodooFloppyController: Buffer overrun/underrun.\n");
        error = kIOReturnDMAError;
    }
    if (st1 & FLOPPY_ST1_DATA_ERROR) {
        DBGLOG("VoodooFloppyController: CRC error.\n");
        error = kIOReturnIOError;
    }
    if (st1 & FLOPPY_ST1_END_OF_CYLINDER) {
        DBGLOG("VoodooFloppyController: End of track.\n");
        error = kIOReturnIOError;
    }
    if (st2 & FLOPPY_ST2_BAD_CYLINDER) {
        DBGLOG("VoodooFloppyController: Bad track.\n");
        error = kIOReturnIOError;
    }
    if (st2 & FLOPPY_ST2_WRONG_CYLINDER) {
        DBGLOG("VoodooFloppyController: Wrong track.\n");
        error = kIOReturnIOError;
    }
    if (st2 & FLOPPY_ST2_DATA_ERROR_IN_FIELD) {
        DBGLOG("VoodooFloppyController: CRC error in data.\n");
        error = kIOReturnIOError;
    }
    if (st2 & FLOPPY_ST2_CONTROL_MARK) {
        DBGLOG("VoodooFloppyController: Deleted address mark.\n");
        error = kIOReturnIOError;
    }
    
    return error;
}

IOReturn VoodooFloppyController::checkForMedia(bool *mediaPresent, UInt8 currentTrack) {
    //DBGLOG("VoodooFloppyController::checkForMedia()\n");
    
    // If the disk change bit is set, seek to some track and attempt re-calibration.
    // Only a successful seek/calibrate that actually did something can clear the bit.
    // We only want to try this once, because if the bit is still set after seeks,
    // there probably isn't media in the drive.
    IOReturn result = kIOReturnSuccess;
    *mediaPresent = true;
    if (inb(FLOPPY_REG_DIR) & kFloppyDirDskChg) {
        DBGLOG("VoodooFloppyController::checkForMedia(): no media, attempting clear.\n");
        *mediaPresent = false;
        
        // Recalibrate.
        result = recalibrate();
        if (result != kIOReturnSuccess)
            return result;
        
        // Seek back to where we were.
        result = seek(currentTrack);
        if (result != kIOReturnSuccess)
            return result;
        
        // If bit is still set, no media is present.
        if (inb(FLOPPY_REG_DIR) & kFloppyDirDskChg)
            result = kIOReturnNoMedia;
    }
    
    return result;
}

IOReturn VoodooFloppyController::recalibrate() {
    DBGLOG("VoodooFloppyController::recalibrate()\n");
    IOReturn result = kIOReturnSuccess;
    UInt8 st0, cyl = 0;
    bool seekCleared = false;
    
    // Attempt to calibrate.
    for (UInt8 i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
        // Make sure we are ready.
        if (!isControllerReady()) {
            result = kIOReturnNotReady;
            goto done;
        }
        
        // Turn on motor.
        if (!setMotorOn()) {
            result = kIOReturnNotPermitted;
            goto done;
        }
        
        // Send calibrate command.
        writeData(FLOPPY_CMD_RECALIBRATE);
        writeData(_currentDevice->getDriveNumber());
        waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
        senseInterrupt(&st0, &cyl);
        
        // If the disk change bit is set, seek to some track and attempt re-calibration.
        // Only a successful seek/calibrate that actually did something can clear the bit.
        // We only want to try this once, because if the bit is still set after seeks,
        // there probably isn't media in the drive.
        if (inb(FLOPPY_REG_DIR) & kFloppyDirDskChg) {
            if (!seekCleared) {
                DBGLOG("VoodooFloppyController::recalibrate(): no media, attempting clear.\n");
                seek(10);
                seekCleared = true;
                continue;
            } else {
                result = kIOReturnNoMedia;
                goto done;
            }
        }
        
        // If current cylinder is zero, we are done.
        if (!cyl) {
            result = kIOReturnSuccess;
            goto done;
        }
    }
    
    // Calibrate failed if we get here.
    DBGLOG("VoodooFloppyController::recalibrate(): fail.\n");
    result = kIOReturnIOError;
    
done:
    _tmrMotorOffSource->setTimeoutMS(kFloppyMotorTimeoutMs);
    return result;
}

// Seek to specified track.
IOReturn VoodooFloppyController::seek(UInt8 track) {
    DBGLOG("VoodooFloppyController::seek(%u)\n", track);
    IOReturn result = kIOReturnSuccess;
    UInt8 st0, cyl = 0;
    
    // Attempt seek.
    for (UInt8 i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
        // Make sure we are ready.
        if (!isControllerReady()) {
            result = kIOReturnNotReady;
            goto done;
        }
        
        // Turn on motor.
        if (!setMotorOn()) {
            result = kIOReturnNotPermitted;
            goto done;
        }
        
        // Send seek command.
        writeData(FLOPPY_CMD_SEEK);
        writeData((0 << 2) | _currentDevice->getDriveNumber()); // Head 0, drive.
        writeData(track);
        
        // Wait for response and check interrupt.
        waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
        senseInterrupt(&st0, &cyl);
        
        // Ensure command completed successfully.
        if (st0 & FLOPPY_ST0_INTERRUPT_CODE)
            continue;
        
        // If we have reached the requested track, return.
        if (cyl == track) {
            result = kIOReturnSuccess;
            goto done;
        }
    }
    
    // Seek failed if we get here.
    DBGLOG("VoodooFloppyController::seek(%u): fail.\n", track);
    result = kIOReturnIOError;
    
done:
    _tmrMotorOffSource->setTimeoutMS(kFloppyMotorTimeoutMs);
    return result;
}

IOReturn VoodooFloppyController::readWriteSectors(bool write, UInt8 track, UInt8 head, UInt8 sector, UInt8 count) {
    DBGLOG("VoodooFloppyController::readWriteSectors(write %u, track %u, head %u, sector %u, count %u)\n", write, track, head, sector, count);
    IOReturn result = kIOReturnSuccess;
    bool mediaPresent = false;
    
    for (UInt8 i = 0; i < FLOPPY_CMD_RETRY_COUNT; i++) {
        // Make sure we are ready.
        if (!isControllerReady()) {
            result = kIOReturnNotReady;
            goto done;
        }
        
        // Turn on motor.
        if (!setMotorOn()) {
            result = kIOReturnNotPermitted;
            goto done;
        }
        
        // Check for media.
        result = checkForMedia(&mediaPresent, track);
        if (result != kIOReturnSuccess)
            goto done;
        
        // Write speed to CCR.
        setTransferSpeed(0);
        setDriveData(0xC, 0x2, 0xF, true);
        
        // Initialize DMA.
        setDma(count * _currentDevice->getBlockSize(), write);
        
        // Send read command to disk to read both sides of track.
        writeData((write ? FLOPPY_CMD_WRITE_DATA : FLOPPY_CMD_READ_DATA) | FLOPPY_CMD_EXT_SKIP | FLOPPY_CMD_EXT_MFM | FLOPPY_CMD_EXT_MT);
        writeData(head << 2 | _currentDevice->getDriveNumber());
        writeData(track);     // Track.
        writeData(head);         // Head 0.
        writeData(sector);        // Start at sector 1.
        writeData(FLOPPY_BYTES_SECTOR_512);
        writeData(18);        // 18 sectors per track?
        writeData(FLOPPY_GAP3_3_5);
        writeData(0xFF);
        
        // Wait for IRQ.
        waitInterrupt(FLOPPY_IRQ_WAIT_TIME);
        
        // Check for media. If media was not present before, try the read again.
        result = checkForMedia(&mediaPresent, track);
        if (result != kIOReturnSuccess)
            goto done;
        if (!mediaPresent)
            continue;
        
        UInt8 resultBytes[7];
        for (UInt8 i = 0; i < 7; i++) {
            resultBytes[i] = readData();
        }
        DBGLOG("VoodooFloppyController::readWriteSectors(write %u, track %u, head %u, sector %u) result: 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n", write, track, head, sector, resultBytes[0], resultBytes[1], resultBytes[2], resultBytes[3], resultBytes[4], resultBytes[5], resultBytes[6]);
        
        // Determine errors if any.
        result = parseError(resultBytes[0], resultBytes[1], resultBytes[2]);
        
        // If no error, we are done.
        if (result == kIOReturnSuccess || result == kIOReturnNotWritable)
            goto done;
        
        // Recalibrate the drive if it wasn't a DMA issue.
        if (result != kIOReturnDMAError) {
            seek(10);
            result = recalibrate();
            if (result != kIOReturnSuccess)
                return result;
            
            // Seek back to position.
            result = seek(track);
            if (result != kIOReturnSuccess)
                return result;
            IOSleep(100);
        }
    }
    
    // Failed.
    DBGLOG("VoodooFloppyController::readWriteSectors(write %u, track %u, head %u, sector %u) fail.\n", write, track, head, sector);
    result = kIOReturnIOError;
    
done:
    _tmrMotorOffSource->setTimeoutMS(kFloppyMotorTimeoutMs);
    return result;
}
