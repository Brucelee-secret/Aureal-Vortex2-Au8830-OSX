/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * IOAudioControl objects are children of their IOAudioEngine, so search the
 * engine service-plane children rather than matching the control as a service.
 */
#import "Aureal3DControlAccess.h"
#import <IOKit/audio/IOAudioDefines.h>

#include <stdint.h>

static const uint32_t kAurealThreeDControlID = 0x41553344U; /* "AU3D" */
static const char kAurealAudioEngineClass[] = "AuVortex2AudioEngine";

static Boolean AurealNumberAsSInt32(CFTypeRef object, SInt32 *value)
{
    if (!object || CFGetTypeID(object) != CFNumberGetTypeID())
        return false;
    return CFNumberGetValue((CFNumberRef)object, kCFNumberSInt32Type, value);
}

io_registry_entry_t AurealFindThreeDControl(void)
{
    CFMutableDictionaryRef matching =
        IOServiceMatching(kAurealAudioEngineClass);
    if (!matching)
        return IO_OBJECT_NULL;

    io_iterator_t engines = IO_OBJECT_NULL;
    if (IOServiceGetMatchingServices(
            kIOMasterPortDefault, matching, &engines) != KERN_SUCCESS)
        return IO_OBJECT_NULL;

    io_registry_entry_t found = IO_OBJECT_NULL;
    io_registry_entry_t engine = IO_OBJECT_NULL;
    while ((engine = IOIteratorNext(engines)) != IO_OBJECT_NULL) {
        io_iterator_t children = IO_OBJECT_NULL;
        if (IORegistryEntryGetChildIterator(
                engine, kIOServicePlane, &children) == KERN_SUCCESS) {
            io_registry_entry_t child = IO_OBJECT_NULL;
            while ((child = IOIteratorNext(children)) != IO_OBJECT_NULL) {
                CFMutableDictionaryRef properties = NULL;
                SInt32 controlID = 0;
                Boolean isTarget = false;
                if (IORegistryEntryCreateCFProperties(
                        child, &properties, kCFAllocatorDefault, 0) == KERN_SUCCESS &&
                    properties) {
                    isTarget = AurealNumberAsSInt32(
                        CFDictionaryGetValue(
                            properties, CFSTR(kIOAudioControlIDKey)),
                        &controlID) &&
                        (uint32_t)controlID == kAurealThreeDControlID;
                }
                if (properties)
                    CFRelease(properties);

                if (isTarget) {
                    found = child;
                    break;
                }
                IOObjectRelease(child);
            }
            IOObjectRelease(children);
        }
        IOObjectRelease(engine);
        if (found != IO_OBJECT_NULL)
            break;
    }
    IOObjectRelease(engines);
    return found;
}

IOReturn AurealReadThreeD(io_registry_entry_t controlEntry, SInt32 *value)
{
    if (controlEntry == IO_OBJECT_NULL || !value)
        return kIOReturnBadArgument;

    CFMutableDictionaryRef properties = NULL;
    IOReturn result = IORegistryEntryCreateCFProperties(
        controlEntry, &properties, kCFAllocatorDefault, 0);
    if (result != KERN_SUCCESS || !properties)
        return result == KERN_SUCCESS ? kIOReturnError : result;

    SInt32 current = 0;
    const Boolean valid = AurealNumberAsSInt32(
        CFDictionaryGetValue(properties, CFSTR(kIOAudioControlValueKey)),
        &current);
    CFRelease(properties);
    if (!valid || (current != 0 && current != 1))
        return kIOReturnBadArgument;
    *value = current;
    return kIOReturnSuccess;
}

IOReturn AurealSetThreeD(io_registry_entry_t controlEntry, SInt32 value)
{
    if (controlEntry == IO_OBJECT_NULL || (value != 0 && value != 1))
        return kIOReturnBadArgument;

    CFNumberRef number = CFNumberCreate(
        kCFAllocatorDefault, kCFNumberSInt32Type, &value);
    if (!number)
        return kIOReturnNoMemory;
    const IOReturn result = IORegistryEntrySetCFProperty(
        controlEntry, CFSTR(kIOAudioControlValueKey), number);
    CFRelease(number);
    return result;
}
