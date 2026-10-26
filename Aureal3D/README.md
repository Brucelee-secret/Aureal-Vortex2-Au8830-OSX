# Aureal 3D preference pane

This separate preference pane controls the AU8830 driver's existing analog
AC'97 3D Surround setting. The driver registers a 3D toggle on the analog
audio engine only. S/PDIF and microphone capture are not changed.

## Build on Mac OS X 10.6.8

1. Open `ControlPane/Aureal3D.xcodeproj` in Xcode 4.2.
2. Build the `Aureal3D` target for 64-bit Intel.
3. Copy `Aureal3D.prefPane` from the build products into
   `~/Library/PreferencePanes/`.
4. Quit and reopen System Preferences, then open the Aureal 3D pane.

The checkbox starts enabled because the driver keeps its existing 3D-enabled
startup behavior. If the pane cannot find the driver control, load the matching
`AuVortex2Driver.kext` and click **Повторить поиск**.
