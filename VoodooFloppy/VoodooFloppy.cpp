#include <IOKit/IOLib.h>
#include <IOKit/IOFilterInterruptEventSource.h>
#include "VoodooFloppy.hpp"
#include "IO.h"

// This required macro defines the class's constructors, destructors,
// and several other methods I/O Kit requires.
OSDefineMetaClassAndStructors(VoodooFloppy, IOService)

// Define the driver's superclass.
#define super IOService

static const char *driveTypes[6] = { "no floppy drive", "360KB 5.25\" floppy drive",
    "1.2MB 5.25\" floppy drive", "720KB 3.5\"", "1.44MB 3.5\"", "2.88MB 3.5\""};

static void irq_callback(OSObject *target, void *refCon, IOService *nub, int source) {
    // IRQ was triggered.
    *(bool*)refCon = true;
}

/**
 * Waits for IRQ6 to be raised.
 * @return True if the IRQ was triggered; otherwise false if it timed out.
 */
bool VoodooFloppy::wait_for_irq(uint16_t timeout) {
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
        IOLog("VoodooFloppy: IRQ timeout!\n");
    
    // Reset triggered value.
    irqTriggered = false;
    return ret;
}

/**
 * Write a byte to the floppy controller
 * @param data The byte to write.
 */
void VoodooFloppy::write_data(uint8_t data) {
    for (uint16_t i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
        // Wait until register is ready.
        if (inb(FLOPPY_REG_MSR) & FLOPPY_MSR_RQM) {
            outb(FLOPPY_REG_FIFO, data);
            return;
        }
        IOSleep(10);
    }
    IOLog("VoodooFloppy: Data timeout!\n");
}

/**
 * Read a byte from the floppy controller
 * @return The byte read. If a timeout occurs, 0.
 */
uint8_t VoodooFloppy::read_data(void) {
    for (uint16_t i = 0; i < FLOPPY_IRQ_WAIT_TIME; i++) {
        // Wait until register is ready.
        if (inb(FLOPPY_REG_MSR) & FLOPPY_MSR_RQM)
            return inb(FLOPPY_REG_FIFO);
        IOSleep(10);
    }
    IOLog("VoodooFloppy: Data timeout!\n");
    return 0;
}

/**
 * Gets any interrupt data.
 * @param st0 Pointer to ST0 value.
 * @param cyl Pointer to cyl value.
 */
void VoodooFloppy::sense_interrupt(uint8_t* st0, uint8_t* cyl) {
    // Send command and get result.
    write_data(FLOPPY_CMD_SENSE_INTERRUPT);
    *st0 = read_data();
    *cyl = read_data();
}

/**
 * Detects floppy drives in CMOS.
 * @return True if drives were found; otherwise false.
 */
bool VoodooFloppy::detect(uint8_t *outTypeA, uint8_t *outTypeB) {
    IOLog("VoodooFloppy: Detecting drives from CMOS...\n");
    outb(0x70, 0x10);
    uint8_t types = inb(0x71);
    
    // Parse drives.
    *outTypeA = types >> 4; // Get high nibble.
    *outTypeB = types & 0xF; // Get low nibble by ANDing out low nibble.
    
    // Did we find any drives?
    if (*outTypeA > FLOPPY_TYPE_2880_35 || *outTypeB > FLOPPY_TYPE_2880_35)
        return false;
    IOLog("VoodooFloppy: Drive A: %s\nVoodooFloppy: Drive B: %s\n", driveTypes[*outTypeA], driveTypes[*outTypeB]);
    return (*outTypeA > 0 || *outTypeB > 0);
}

/**
 * Gets the version of the floppy controller.
 * @return The version byte.
 */
uint8_t VoodooFloppy::version(void) {
    // Send version command and get version.
    write_data(FLOPPY_CMD_VERSION);
    return read_data();
}

/**
 * Resets the floppy controller.
 */
void VoodooFloppy::reset(void) {
    IOLog("FLOPPY: Resetting controller...\n");
    
    // Disable and re-enable floppy controller.
    outb(FLOPPY_REG_DOR, 0x00);
    outb(FLOPPY_REG_DOR, FLOPPY_DOR_IRQ_DMA | FLOPPY_DOR_RESET);
    wait_for_irq(FLOPPY_IRQ_WAIT_TIME);
    
    // Clear any interrupts on drives.
    uint8_t st0, cyl;
    for(int i = 0; i < 4; i++)
        sense_interrupt(&st0, &cyl);
}

bool VoodooFloppy::init(OSDictionary *dict) {
    bool result = super::init(dict);
    IOLog("VoodooFloppy: Initializing...\n");
    return result;
}

void VoodooFloppy::free(void) {
    super::free();
}

IOService *VoodooFloppy::probe(IOService *provider, SInt32 *score) {
    IOLog("VoodooFloppy: probe()...\n");
    
    // Detect drives to see if we should load or not.
    uint8_t driveTypeA, driveTypeB = 0;
    if (!detect(&driveTypeA, &driveTypeB)) {
        IOLog("VoodooFloppy: No drives found in CMOS. Aborting.\n");
        return NULL;
    }
    
    // Go ahead with probe.
    return super::probe(provider, score);
}


void VoodooFloppy::interruptHandler(OSObject*, void* refCon, IOService*, int) {
    IOLog("VoodooFloppy: IRQ raised");
    *(bool*)refCon = true;
}

void VoodooFloppy::packetReadyMouse(IOInterruptEventSource *, int) {
    IOLog("VoodooFloppy: IRQ raised");
}

bool VoodooFloppy::start(IOService *provider) {
    IOLog("VoodooFloppy: start\n");
    //_workLoop = IOWorkLoop::workLoop();
    // _interruptSource = IOInterruptEventSource::interruptEventSource(this, OSMemberFunctionCast(IOInterruptEventAction, this, &VoodooFloppy::packetReadyMouse));
    //IOLog("VoodooFloppy:: addEvent: 0x%X\n", _workLoop->addEventSource(_interruptSource));
    
    IOLog("VoodooFloppy: registerInterrupt() 0x%X\n", getProvider()->registerInterrupt(0, 0, interruptHandler, &irqTriggered));
    IOLog("VoodooFloppy: enableInterrupt() 0x%X\n",getProvider()->enableInterrupt(0));
    
    IOLog("VoodooFloppy: registered interrupt\n");
    
    
    //int d = kIOReturnSuccess;
    
    // Reset controller and get version.
    reset();
    uint8_t floppyVersion = version();
    
    
    // If version is 0xFF, that means there isn't a floppy controller.
    if (floppyVersion == FLOPPY_VERSION_NONE) {
        IOLog("VoodooFloppy: No floppy controller present. Aborting.\e[0m\n");
        return false;
    }
    
    // Print version.
    IOLog("VoodooFloppy: Version: 0x%X.\n", floppyVersion);
    
    bool result = super::start(provider);
    return result;
}


void VoodooFloppy::stop(IOService *provider) {
    IOLog("Floppy stop\n");
    super::stop(provider);
}

