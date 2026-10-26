/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * User-space access to the AU8830 analog 3D Surround IOAudioControl.
 */
#ifndef AUREAL_3D_CONTROL_ACCESS_H
#define AUREAL_3D_CONTROL_ACCESS_H

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

io_registry_entry_t AurealFindThreeDControl(void);
IOReturn AurealReadThreeD(io_registry_entry_t controlEntry, SInt32 *value);
IOReturn AurealSetThreeD(io_registry_entry_t controlEntry, SInt32 value);

#endif
