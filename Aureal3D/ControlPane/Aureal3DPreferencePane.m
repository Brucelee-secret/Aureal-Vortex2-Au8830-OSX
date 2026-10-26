/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Programmatic NSPreferencePane compatible with Mac OS X 10.6.
 */
#import <Cocoa/Cocoa.h>
#import <IOKit/IOKitLib.h>
#import "../UserSupport/Aureal3DControlAccess.h"
#import "Aureal3DPreferencePane.h"

@implementation Aureal3DPreferencePane

- (id)initWithBundle:(NSBundle *)bundle
{
    self = [super initWithBundle:bundle];
    if (self) {
        threeDCheckbox = nil;
        statusField = nil;
        controlEntry = IO_OBJECT_NULL;
        currentValue = NO;
    }
    return self;
}

- (void)dealloc
{
    if (controlEntry != IO_OBJECT_NULL) {
        IOObjectRelease(controlEntry);
        controlEntry = IO_OBJECT_NULL;
    }
    [super dealloc];
}

- (NSView *)loadMainView
{
    NSView *view = [[[NSView alloc] initWithFrame:NSMakeRect(0, 0, 560, 210)]
        autorelease];
    [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];
    [self setMainView:view];

    threeDCheckbox = [[NSButton alloc] initWithFrame:NSMakeRect(28, 150, 500, 26)];
    [threeDCheckbox setButtonType:NSSwitchButton];
    [threeDCheckbox setTitle:@"3D Surround — аналоговый выход"];
    [threeDCheckbox setTarget:self];
    [threeDCheckbox setAction:@selector(threeDCheckboxChanged:)];
    [threeDCheckbox setEnabled:NO];
    [view addSubview:threeDCheckbox];
    [threeDCheckbox release];

    NSTextField *note = [[NSTextField alloc] initWithFrame:NSMakeRect(28, 104, 500, 32)];
    [note setStringValue:
        @"Эффект действует только на аналоговый выход. S/PDIF и микрофон не изменяются."];
    [note setEditable:NO];
    [note setSelectable:NO];
    [note setBezeled:NO];
    [note setDrawsBackground:NO];
    [note setFont:[NSFont systemFontOfSize:12]];
    [view addSubview:note];
    [note release];

    statusField = [[NSTextField alloc] initWithFrame:NSMakeRect(28, 58, 500, 38)];
    [statusField setStringValue:@"Поиск переключателя Aureal…"];
    [statusField setEditable:NO];
    [statusField setSelectable:NO];
    [statusField setBezeled:NO];
    [statusField setDrawsBackground:NO];
    [statusField setFont:[NSFont systemFontOfSize:11]];
    [view addSubview:statusField];
    [statusField release];

    NSButton *refresh = [[NSButton alloc] initWithFrame:NSMakeRect(382, 18, 146, 28)];
    [refresh setTitle:@"Повторить поиск"];
    [refresh setTarget:self];
    [refresh setAction:@selector(refreshControl:)];
    [view addSubview:refresh];
    [refresh release];

    [self setInitialKeyView:threeDCheckbox];
    [self setFirstKeyView:threeDCheckbox];
    [self setLastKeyView:refresh];
    [self refreshControl:nil];
    return view;
}

- (void)didSelect
{
    [super didSelect];
    [self refreshControl:nil];
}

- (void)refreshControl:(id)sender
{
    (void)sender;
    if (controlEntry != IO_OBJECT_NULL) {
        IOObjectRelease(controlEntry);
        controlEntry = IO_OBJECT_NULL;
    }

    [threeDCheckbox setEnabled:NO];
    [statusField setStringValue:@"Поиск переключателя Aureal…"];
    controlEntry = AurealFindThreeDControl();
    if (controlEntry == IO_OBJECT_NULL) {
        [statusField setStringValue:
            @"Контрол не найден. Загрузите AuVortex2Driver.kext и повторите поиск."];
        return;
    }

    SInt32 value = 0;
    IOReturn result = AurealReadThreeD(controlEntry, &value);
    if (result != kIOReturnSuccess) {
        [statusField setStringValue:[NSString stringWithFormat:
            @"Не удалось прочитать состояние: 0x%08x", result]];
        IOObjectRelease(controlEntry);
        controlEntry = IO_OBJECT_NULL;
        return;
    }

    currentValue = value != 0;
    [threeDCheckbox setState:currentValue ? NSOnState : NSOffState];
    [threeDCheckbox setEnabled:YES];
    [statusField setStringValue:[NSString stringWithFormat:
        @"3D Surround %@", currentValue ? @"включён" : @"выключен"]];
}

- (void)threeDCheckboxChanged:(id)sender
{
    (void)sender;
    if (controlEntry == IO_OBJECT_NULL) {
        [self refreshControl:nil];
        return;
    }

    const SInt32 requestedValue =
        [threeDCheckbox state] == NSOnState ? 1 : 0;
    IOReturn result = AurealSetThreeD(controlEntry, requestedValue);
    if (result != kIOReturnSuccess) {
        [threeDCheckbox setState:currentValue ? NSOnState : NSOffState];
        [statusField setStringValue:[NSString stringWithFormat:
            @"Драйвер отклонил переключение: 0x%08x", result]];
        return;
    }

    SInt32 readback = 0;
    result = AurealReadThreeD(controlEntry, &readback);
    if (result != kIOReturnSuccess || readback != requestedValue) {
        [threeDCheckbox setState:currentValue ? NSOnState : NSOffState];
        [statusField setStringValue:
            @"Драйвер не подтвердил новое состояние. Повторите поиск."];
        return;
    }

    currentValue = readback != 0;
    [threeDCheckbox setState:currentValue ? NSOnState : NSOffState];
    [statusField setStringValue:[NSString stringWithFormat:
        @"3D Surround %@", currentValue ? @"включён" : @"выключен"]];
}

@end
