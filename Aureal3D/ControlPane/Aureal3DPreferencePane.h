/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * System Preferences pane for the AU8830 analog 3D Surround control.
 */
#import <PreferencePanes/PreferencePanes.h>
#import <IOKit/IOKitLib.h>

@interface Aureal3DPreferencePane : NSPreferencePane
{
@private
    NSButton *threeDCheckbox;
    NSTextField *statusField;
    io_registry_entry_t controlEntry;
    BOOL currentValue;
}

- (NSView *)loadMainView;
- (void)refreshControl:(id)sender;
- (void)threeDCheckboxChanged:(id)sender;

@end
