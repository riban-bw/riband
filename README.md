# ttgowatch_blemidi
BLE MIDI firmware for TTGO watch

# Overview

This firmware is for the TTGO Watch 2020 V3. It may work on other models but has not been tested. It provides BLE MIDI interface for control and monitoring of MIDI devices. It is specifically designed to work with Zynthian but should be able to work with other devices, DAWs, etc. with minimal change.

# Usage

The power button works normally (press for 2s to turn on and 6s to turn off) but also works as a mode button when pressed for less than 2s. It changes between the various modes:

* Standby (screen off but still connected)
* KAOSS style X-Y touch pad, sending two MIDI CC messages
* Pad launcher - 4x4 grid of pads that will send CC message when touched

Specific received MIDI messages will trigger the watch to vibrate and display a pulsed circle in the X-Y view.

Touching the screen, rotating the watch, connecting Bluetooth or receiving a relevant MIDI message will wake it from standby.

Pressing the power button for 2s when the watch is on will show the admin options which allow access to the settings menu and powering off.

Settings menu allows Bluetooth to be toggled, MIDI channel and CCs to be changed.

When BLE is enabled the watch is always visible as a Bluetooth device and offers no authentication. Bluetooth clients may connect to the watch. When a BLE MIDI connection is made, a blue indication appears at the top right of the screen. 

# Building

The firmware has been written using PlatformIO. Opening the project in a PlatformIO environment, e.g. VSCode plugin should pull the dependencies. Running PlatformIO build processes should build the image and using PlatformIO's firmware flash function should allow uploading the firmware to the watch via USB.