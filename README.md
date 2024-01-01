# riband
BLE MIDI firmware for TTGO watch

# Overview

This firmware is for the TTGO Watch 2020 V3. It may work on other models but has not been tested. It provides BLE MIDI interface for control and monitoring of MIDI devices. It is specifically designed to work with Zynthian but should be able to work with other devices, DAWs, etc. with minimal change.

# Usage

There is a hardware power button which is used to turn on (2s press), turn off (6s press) change function (short press) and access settings (2s press). Pressing when in configuration menu will return to previous menu and pressing when screen is off will wake screen without changing mode. Short press changes between these mdoes:

* Standby (screen off but still connected)
* KAOSS style X-Y touch pad, sending two MIDI CC messages (default 101/102)
* Pad launcher - 4x4 grid of pads that will send CC note-on/off 48..63 when touched/released

Receiving a MIDI CC (number configured in settings - default 101) will trigger the watch to vibrate and display a pulsed circle in the X-Y view.

Touching the screen, pressing the button, rotating the watch, connecting Bluetooth or receiving a relevant MIDI message will wake the screen if it is off.

Settings menu allows Bluetooth to be toggled, MIDI channel and CCs to be changed and screen brightness and timeout to be adjusted . The numeric keypad accepts only valid values of the correct length, e.g. for MIDI channel, press 2 digits with the first digit being less than 2. After entering all digits the value is set. Clear the current entry by touching the value display window.

When BLE is enabled the watch is always visible as a Bluetooth device called, "riband" and offers no authentication. Bluetooth clients may connect to the watch. When BLE MIDI is connected, a blue indication appears at the top right of the screen. 

# Building

The firmware has been written using PlatformIO. Opening the project in a PlatformIO environment, e.g. VSCode plugin should pull the dependencies. Running PlatformIO build processes should build the image and using PlatformIO's firmware flash function should allow uploading the firmware to the watch via USB.