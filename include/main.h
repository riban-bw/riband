/*  riband - BLE MIDI wearable writsband
Copyright (C) 2023-2024  riban ltd <info@riban.co.uk>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdint>

static const uint32_t PAD_COLOURS[] = {
    0x3186, // disabled
    0x0dc0, // starting
    0x0680, // playing
    0xf800, // stopping
    // groups
    0x6124,
    0x3b4c,
    0x4b42,
    0x6250,
    0x4b93,
    0x4cb9,
    0x0300,
    0xb54b,
    0x9b26,
    0x730c,
    0xd38e,
    0x000c,
    0x0471,
    0xf342,
    0xbcef,
    0x552a,
    0xfb76,
    0xcc2c,
    0x4cb9,
    0xb2b9,
    0xb410,
    0x003f,
    0x9df5,
    0xf89f,
    0x3418,
    0x9bfd
};

static const char* BTN_LABELS[] = {
    "\x86", // Menu
    "\x85", // Mixer
    "\x8C", //Ctrl
    "ZS3",
    "ALT",
    "\x82", // Metronome
    "\x87", // Pads
    "F1",
    "\x89", // Rec
    "\x8A", // Stop
    "\x8B", // Play
    "F2",
    "\x83", // Back
    "\x7E", // Up arrow
    "\x84", // Select
    "F3",
    "\x81", // Left arrow
    "\x7F", // Down arrow
    "\x80", // Right arrow
    "F4",
    "\x88", //Page
};

static uint8_t BTN_IDS [] = {0, 13, 20, 16, 1, 18, 12, 17, 14,
                             2, 3, 20, 4, 5, 6, 8, 9, 10
    };

// Forward declarations
void screenOn();
void screenOff();
void refresh();
void processTouch();
bool processAccel();
void onPowerButtonLongPress();
void onPowerButtonShortPress();
void startBle();
void toggleBle();
void onBleConnect();
void onBleDisconnect();
void onMidiCC(uint8_t, uint8_t, uint8_t, uint16_t);
void onMidiNoteOn(uint8_t, uint8_t, uint8_t, uint16_t);
void showStatus();
void numEntry();