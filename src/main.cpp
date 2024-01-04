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

/* TODO / known issues
    - Accelometer gestures
    - Only advertise Bluetooth when in settings menu
*/

#include "main.h"
#include <LilyGoWatch.h> // Provides watch API
#include <BLEMidi.h> // Provides BLE MIDI interface
#include <EEPROM.h>
#include "Riban_24.h"

#define MAGIC 0x7269626e // Used to check if EEPROM has been initialised

enum mode_enum {
    MODE_XY,
    MODE_PADS,
    MODE_NAVIGATE1,
    MODE_NAVIGATE2,
    MODE_SETTINGS,
    MODE_MENU,
    MODE_ENCODERS,
    MODE_POWEROFF,
    MODE_BLE,
    MODE_MIDICHAN,
    MODE_CCX,
    MODE_CCY,
    MODE_CCRX,
    MODE_BRIGHTNESS,
    MODE_TIMEOUT,
    MODE_NUM_0, MODE_NUM_1, MODE_NUM_2, MODE_NUM_3, MODE_NUM_4, MODE_NUM_5, MODE_NUM_6, MODE_NUM_7, MODE_NUM_8, MODE_NUM_9,
    MODE_NONE
};

enum setting_enum {
    SETTING_BLE,
    SETTING_MIDICHAN,
    SETTING_CCX,
    SETTING_CCY,
    SETTING_CCRX,
    SETTING_BRIGHTNESS,
    SETTINGS_TIMEOUT
};

TTGOClass* ttgo; // Pointer to singleton instance of ttgo watch object
BMA* accel; // Pointer to accelerometer sensor
TFT_eSprite* canvas; // Pointer to sprite acting as display double buffer
TFT_eSprite* menuCanvas; // Pointer to sprite acting as display double buffer
TFT_eSprite* statusCanvas; // Pointer to sprite acting as display double buffer

class gfxButton {
    public:
        gfxButton(TFT_eSprite* canvas, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t bg, uint32_t bgh, const char* text=nullptr, uint8_t mode=MODE_NONE) :
            m_canvas(canvas), m_x(x), m_y(y), m_w(w), m_h(h), m_bg(bg), m_bgh(bgh), m_mode(mode) {
                m_fg = TFT_WHITE;
                m_rad = m_h / 4;
                m_indent_x = m_w / 2;
                m_indent_y = m_h / 2;
                if (text)
                    setText(text);
            };

        void setText(const char* text) {
            free(m_text);
            m_text = (char*)malloc(strlen(text) + 1);
            sprintf(m_text, text);
        }

        void draw(bool hl=false) {
            m_canvas->fillRoundRect(m_x, m_y, m_w, m_h, m_rad, hl?m_bgh:m_bg);
            if (m_text) {
                m_canvas->setTextColor(m_fg);
                m_canvas->setTextDatum(m_align);
                m_canvas->drawString(m_text, m_x + m_indent_x, m_y + m_indent_y, 1);
                m_canvas->setTextDatum(TL_DATUM);
            }
        }

        void drawBar(uint16_t percent) {
            uint16_t x = percent * m_w / 100;
            m_canvas->fillRoundRect(m_x, m_y, m_w, m_h, m_rad, m_bg);
            m_canvas->fillRoundRect(m_x, m_y, x, m_h, m_rad, m_bgh);
            if (m_text) {
                m_canvas->setTextColor(m_fg);
                m_canvas->setTextDatum(m_align);
                m_canvas->drawString(m_text, m_x + m_indent_x, m_y + m_indent_y, 1);
                m_canvas->setTextDatum(TL_DATUM);
            }
        }

        bool bounds(uint16_t x, uint16_t y) {
            return (x >= m_x && x <= (m_x + m_w) && y >= m_y && y <= (m_y + m_h));
        }

        uint8_t getMode() {
            return m_mode;
        }

    TFT_eSprite* m_canvas;
    uint32_t m_bg, m_bgh, m_fg;
    uint16_t m_x, m_y, m_w, m_h;
    uint8_t m_rad;
    uint8_t m_mode = MODE_NONE;
    uint8_t m_indent_x;
    uint8_t m_indent_y;
    uint8_t m_align = MC_DATUM;
    char * m_text = nullptr;
};

gfxButton* menuBtns[5];
gfxButton* settingsBtns[7];
gfxButton* navigationBtns[9];
gfxButton* launchPads[16];
gfxButton* numPad[11];

uint8_t settings[] = {0, 15, 101, 102, 100, 100, 60}; // Array of 8-bit settings - see setting_enum
uint8_t pulseRadius = 0; // Radius of pulse cirle (decreases over time)
uint8_t lastPulseRadius = 0; // Radius of last pulse cirle (used to clear circle)
uint8_t mode = MODE_MENU; // Menu / display mode
uint8_t selPad = 255; // Index of selected pad
uint8_t oskSel = MODE_NONE; // Index of button selected on touch screen
uint8_t settingsOffset = 0; // Settings view scroll position
uint8_t crosshair_x = 120, crosshair_y = 110; // Coordinates of X-Y controller crosshairs
uint8_t battery; // Battery %
volatile uint32_t screenTimeout = 0; // Countdown timer until auto standby mode
uint32_t now = 0; // Time of current loop process
uint32_t cpuLoad = 0; // Main loop cycles per ms
bool standby = true; // True if in standby mode (screen off)
volatile bool irq = false;
uint8_t eepromSize = sizeof(settings);
uint8_t topDrag = 0; // Y position of top drag down

// Initialisation
void setup(void)
{
    Serial.begin(115200); // Can use USB for debug
    
    ttgo = TTGOClass::getWatch(); // Create instance of watch object (singleton)
    ttgo->begin(); // Initialise watch object
    canvas = new TFT_eSprite(ttgo->tft);
    canvas->createSprite(240, 220);
    canvas->setFreeFont(&Riban_24);
    menuCanvas = new TFT_eSprite(ttgo->tft);
    menuCanvas->createSprite(240, 220);
    menuCanvas->setFreeFont(&Riban_24);
    statusCanvas = new TFT_eSprite(ttgo->tft);
    statusCanvas->createSprite(240, 20);
    statusCanvas->setFreeFont(&Riban_24);
    
    EEPROM.begin(eepromSize + 4);
    uint32_t magic;
    EEPROM.readBytes(0, &magic, 4);
    if (magic == MAGIC) {
        EEPROM.readBytes(4, settings, eepromSize);
        ttgo->setBrightness(settings[SETTING_BRIGHTNESS]);
    }

    menuBtns[0] = new gfxButton(menuCanvas, 0, 0, 78, 73, TFT_DARKGREY, 0xa514, "XY", MODE_XY);
    menuBtns[1] = new gfxButton(menuCanvas, 80, 0, 78, 73, TFT_DARKGREY, 0xa514, "Pads", MODE_PADS);
    menuBtns[2] = new gfxButton(menuCanvas, 160, 0, 78, 73, TFT_DARKGREY, 0xa514, "Nav", MODE_NAVIGATE1);
    menuBtns[3] = new gfxButton(menuCanvas, 0, 80, 78, 73, TFT_DARKGREY, 0xa514, "ENC", MODE_ENCODERS);
    menuBtns[4] = new gfxButton(menuCanvas, 80, 80, 78, 73, TFT_DARKGREY, 0xa514, "Conf", MODE_SETTINGS);

    settingsBtns[0] = new gfxButton(canvas, 20, 0, 200, 54, TFT_DARKGREY, 0xa514, "BLE", MODE_BLE);
    settingsBtns[1] = new gfxButton(canvas, 20, 55, 200, 54, TFT_DARKGREY, 0xa514, "MIDI CHAN", MODE_MIDICHAN);
    settingsBtns[2] = new gfxButton(canvas, 20, 120, 200, 54, TFT_DARKGREY, 0xa514, "X-CC", MODE_CCX);
    settingsBtns[3] = new gfxButton(canvas, 20, 165, 200, 54, TFT_DARKGREY, 0xa514, "Y-CC", MODE_CCY);
    settingsBtns[4] = new gfxButton(canvas, 20, 230, 200, 54, TFT_DARKGREY, 0xa514, "Rx-CC", MODE_CCRX);
    settingsBtns[5] = new gfxButton(canvas, 20, 275, 200, 54, TFT_DARKGREY, 0xa514, "Brightness", MODE_BRIGHTNESS);
    settingsBtns[6] = new gfxButton(canvas, 20, 340, 200, 54, TFT_DARKGREY, 0xa514, "Screen time", MODE_TIMEOUT);
    for (uint8_t i = 0; i < 7; ++i) {
        gfxButton* btn = settingsBtns[i];
        btn->m_align = ML_DATUM;
        btn->m_indent_x = 10;
    }

    // Build launchpad grid
    uint8_t i = 0;
    for (uint8_t col = 0; col < 4; ++col) {
        for (uint8_t row = 0; row < 4; ++row) {
            launchPads[i] = new gfxButton(canvas, col * 60, row * 55, 59, 54, PAD_COLOURS[0], 0x4208);
            ++i;
        }
    }

    // Build navigation grid
    i = 0;
    for (uint8_t row = 0; row < 3; ++row) {
        for (uint8_t col = 0; col < 3; ++col) {
            uint8_t id = BTN_IDS[i];
            navigationBtns[i] = new gfxButton(canvas, col * 80, row * 73, 79, 72, PAD_COLOURS[0], 0x4208, BTN_LABELS[id], id);
            ++i;
        }
    }

    // Build numeric keyapd
    numPad[0] = new gfxButton(canvas, 0, 0, 78, 54, TFT_DARKGREY, 0xa514, "0", 0);
    char txt[2];
    for (uint8_t col = 0; col < 3; ++col) {
        for (uint8_t row = 0; row < 3; ++row) {
            uint8_t i = col + row * 3 + 1;
            sprintf(txt, "%d", i);
            numPad[i] = new gfxButton(canvas, col * 80, 55 + row * 55, 78, 54, TFT_DARKGREY, 0xa514, txt, i);
        }
    }
    numPad[10] = new gfxButton(canvas, 80, 0, 158, 54, 0xa514, TFT_DARKGREY, "   ", 10);
    numPad[10]->m_fg = TFT_BLACK;

    // Initialise haptic feedback motor
    ttgo->motor_begin();

    // Configure power button
    pinMode(AXP202_INT, INPUT_PULLUP);
    attachInterrupt(AXP202_INT, [] {
        irq = true;
    }, FALLING);
    ttgo->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ | AXP202_PEK_LONGPRESS_IRQ| AXP202_VBUS_REMOVED_IRQ | AXP202_VBUS_CONNECT_IRQ | AXP202_CHARGING_IRQ, true);
    ttgo->power->clearIRQ();

    // Configure accelerometer
    accel = ttgo->bma;
    Acfg cfg;
    cfg.odr = BMA4_OUTPUT_DATA_RATE_100HZ;
    cfg.range = BMA4_ACCEL_RANGE_2G;
    cfg.bandwidth = BMA4_ACCEL_NORMAL_AVG4;
    cfg.perf_mode = BMA4_CONTINUOUS_MODE;
    accel->accelConfig(cfg);
    accel->enableAccel();

    if (settings[SETTING_BLE])
        startBle();

    screenOn();
}

void loop()
{
    static uint32_t lastMs = 0;
    static uint32_t nextRefresh = 0;
    static uint32_t nextSecond = 0;
    static uint32_t nextTenSecond = 0;
    static uint32_t nextMinute = 0;
    static uint32_t nextPulse = 0;
    static uint32_t lastBtnPress = 0;
    static uint32_t cycleCount = 0;
    static bool btnPressed = false;
    static bool blink = false;

    cycleCount++;
    now = millis();

    if (irq) {
        ttgo->power->readIRQ();
        bool shortPress = ttgo->power->isPEKShortPressIRQ();
        bool longPress = ttgo->power->isPEKLongPressIRQ();
        bool charging = ttgo->power->isChargeing();
        irq = false;
        ttgo->power->clearIRQ();
        if (longPress) {
            if (standby) {
                screenOn();
            } else {
                onPowerButtonLongPress();
            }
        } else if (shortPress) {
            if (standby) {
                screenOn();
            } else {
                onPowerButtonShortPress();
            }
        }
    }

    processTouch();
    processAccel();

    if (pulseRadius && (nextPulse < now))
        nextPulse = now + 6;

    if (lastMs != now) {
        // 1mS (or slower)
        cpuLoad = cycleCount;
        cycleCount = 0;
        if (nextRefresh < now) {
            // Refresh rate = 20Hz
            if (!standby)
                refresh();
            nextRefresh = now + 50;
        }
        if (nextSecond < now) {
            nextSecond += 1000;
            if (screenTimeout && (--screenTimeout == 0))
                screenOff();

            if (nextTenSecond < now) {
                nextTenSecond = now + 10000;
                battery = ttgo->power->getBattPercentage();

                if (nextMinute < now) {
                    nextMinute = now + 60000;
                }
            }
            blink = !blink;
        }
    }
    lastMs = now;
}

void updateNavigationButtons() {
    uint8_t offset = (mode == MODE_NAVIGATE1) ? 0 : 9;
    for (uint8_t i = 0; i < 9; ++i) {
        uint8_t id = BTN_IDS[i + offset];
        navigationBtns[i]->setText(BTN_LABELS[id]);
        navigationBtns[i]->m_mode = id;
    }
}

void processTouch() {
    static uint32_t touchTime = 0, releaseTime = 0, lastUpdateTime = 0;
    static int16_t x, y, startX, startY, lastX, lastY;
    static uint8_t last_cc_x, last_cc_y;
    static bool touching = false, scrolling = false;
    uint8_t cc_x, cc_y;

    if (ttgo->getTouch(x, y)) {
        screenOn();
        if (!touching) {
            if (now > touchTime + 100) {
                // First touch debounced
                startX = lastX = x;
                startY = lastY = y;
                touchTime = now;
                touching = true;
                topDrag = 0;
            } else {
                return;
            }
        }
        // Touched / dragging
        if (startY < 20 && mode != MODE_XY && mode != MODE_MENU) {
            // Drag from top
            topDrag = y;
            return;
        }
        switch(mode) {
            case MODE_ENCODERS:
                {
                    int16_t dY = startY - y;
                    if (dY < 5 && dY > -5)
                        break;
                    uint8_t val = 127 * (240 - y + 20) / 220;
                    if (x < 60)
                        BLEMidiServer.noteOn(15, dY<0?16:17, 127);
                    else if (x < 120)
                        BLEMidiServer.noteOn(15, dY<0?18:19, 127);
                    else if (x < 180)
                        BLEMidiServer.noteOn(15, dY<0?20:21, 127);
                    else
                        BLEMidiServer.noteOn(15, dY<0?22:23, 127);
                    startY = y;
                }
                break;
            case MODE_XY:
                cc_x = x * 127 / 240;
                if (y > 20)
                    cc_y = 107 - y * 107 / 240;
                else
                    cc_y = 0;
                if (cc_x != last_cc_x) {
                    if (settings[SETTING_BLE])
                        BLEMidiServer.controlChange(settings[SETTING_MIDICHAN], settings[SETTING_CCX], cc_x);
                    last_cc_x = cc_x;
                    crosshair_x = x;
                }
                if (cc_y != last_cc_y) {
                    if (settings[SETTING_BLE])
                        BLEMidiServer.controlChange(settings[SETTING_MIDICHAN], settings[SETTING_CCY], cc_y);
                    last_cc_y = cc_y;
                    if (y > 20)
                        crosshair_y = y - 20;
                    else
                        crosshair_y = 0;
                }
                break;
            case MODE_PADS:
            {
                uint8_t pad = (y - 20) / 55 + x / 60 * 4;
                if (pad < 16) {
                    if (pad != selPad) {
                        if (selPad < 16)
                            BLEMidiServer.noteOn(settings[SETTING_MIDICHAN], 48 + selPad, 0);
                        BLEMidiServer.noteOn(settings[SETTING_MIDICHAN], 48 + pad, 100);
                        selPad = pad;
                    }
                }
                break;
            }
            case MODE_NAVIGATE1:
            case MODE_NAVIGATE2:
            {
                if (selPad != 255)
                    break; // Don't allow slide between buttons
                for (uint8_t i = 0; i < 9; ++i) {
                    gfxButton* btn = navigationBtns[i];
                    if (!btn->bounds(x, y))
                        continue;
                    selPad = btn->getMode();
                    if (selPad < 20)
                        BLEMidiServer.noteOn(15, selPad + 94, 100);
                    break;
                }
                break;
            }
            case MODE_MENU:
                for (uint8_t i = 0; i < 5; ++i) {
                    gfxButton* btn = menuBtns[i];
                    if (btn->bounds(x, y)) {
                        selPad = i;
                        break;
                    }
                }
                break;
            case MODE_SETTINGS:
            {
                if (settingsBtns[SETTING_BRIGHTNESS]->bounds(x, y)) {
                    int16_t dX = x - startX;
                    if (dX > 5 || dX < -5) {
                        // A bit of hysteresis
                        int16_t val = (x - settingsBtns[SETTING_BRIGHTNESS]->m_x) * 255 / settingsBtns[SETTING_BRIGHTNESS]->m_w;
                        settings[SETTING_BRIGHTNESS] = val;
                        ttgo->setBrightness(val);
                        startX = x;
                    }
                }
                else if (settingsBtns[SETTINGS_TIMEOUT]->bounds(x, y)) {
                    int16_t dX = x - startX;
                    if (dX > 5 || dX < -5) {
                        // A bit of hysteresis
                        int16_t val = (x - settingsBtns[SETTINGS_TIMEOUT]->m_x) * 120 / settingsBtns[SETTINGS_TIMEOUT]->m_w;
                        settings[SETTINGS_TIMEOUT] = val;
                        startX = x;
                    }
                }
                int16_t dY = y - startY;
                if(!scrolling) {
                    if (dY > 10 || dY < -10)
                        scrolling = true;
                } else {
                    dY = (y - startY) / 55;
                    if (dY) {
                        dY = settingsOffset - dY;
                        if (dY < 0)
                            settingsOffset = 0;
                        else if (dY > 3)
                            settingsOffset = 3; //!@todo Derive from quantity of settings
                        else
                            settingsOffset = uint8_t(dY);
                        startY = y;
                    }
                }
                break;
            }
        }
        lastX = x;
        lastY = y;
    } else if(touching) {
        // Release
        if (touchTime + 200 > now)
            return; // debounce
        releaseTime = now;
        touching = false;
        if (topDrag) {
            if (topDrag > 120)
                mode = MODE_MENU;
            topDrag = 0;
            return;
        }
        if (scrolling) {
            scrolling = false;
            return;
        }
        switch (mode) {
            case MODE_MENU:
                if (selPad < 5)
                    mode = menuBtns[selPad]->getMode();
                selPad = 255;
                break;
            case MODE_PADS:
                BLEMidiServer.noteOn(settings[SETTING_MIDICHAN], 48 + selPad, 0);
                selPad = -1;
                break;
            case MODE_NAVIGATE1:
            case MODE_NAVIGATE2:
                if (selPad < 20) {
                    BLEMidiServer.noteOn(15, selPad + 94, 0);
                } else {
                    mode = mode==MODE_NAVIGATE1?MODE_NAVIGATE2:MODE_NAVIGATE1;
                    updateNavigationButtons();
                }
                selPad = 255;
                break;
            case MODE_MIDICHAN:
            case MODE_CCX:
            case MODE_CCY:
            case MODE_CCRX:
                // Handle keypad release
                for (uint8_t i = 0; i < 11; ++i) {
                    gfxButton* btn = numPad[i];
                    if (!btn->bounds(x, y))
                        continue;
                    oskSel = btn->getMode();
                    numEntry();
                    break;
                }
                break;
            case MODE_SETTINGS:
                // Handle settings menu release
                for (uint8_t i = 0; i < 4; ++i) {
                    gfxButton* btn = settingsBtns[settingsOffset + i];
                    if (!btn->bounds(x, y))
                        continue;
                    mode = btn->getMode();
                    if (mode == MODE_BLE) {
                        toggleBle();
                        mode = MODE_SETTINGS;
                    } else if (mode == MODE_BRIGHTNESS || mode == MODE_TIMEOUT) {
                        mode = MODE_SETTINGS;
                    }
                    break;
                }
                break;
        }
    oskSel = MODE_NONE;
    }
}


bool processAccel() {
    static uint8_t prevRotation = 0;
    Accel acc;
    uint8_t rotation = accel->direction();
    if (prevRotation != rotation) {
        screenOn();
        prevRotation = rotation;
        switch (rotation) {
            case DIRECTION_DISP_DOWN:
                break;
            case DIRECTION_DISP_UP:
                break;
            case DIRECTION_BOTTOM_EDGE:
                break;
            case DIRECTION_TOP_EDGE:
                break;
            case DIRECTION_RIGHT_EDGE:
                break;
            case DIRECTION_LEFT_EDGE:
                break;
            default:
                break;
            }
    }

    if (accel->getAccel(acc)) {
        /*
        sprite->setCursor(40, 100);
        sprite->printf("%03d, %03d, %03d", acc.x, acc.y, acc.z);
        */
        return true;
    } else
        return false;
}

void onBleConnect() {
    screenOn();
}

void onBleDisconnect() {
}

void onMidiCC(uint8_t chan, uint8_t cc, uint8_t val, uint16_t timestamp) {
    if (chan == settings[SETTING_MIDICHAN]) {
        if (cc == settings[SETTING_CCRX]) {
            // Metronome CC
            screenOn();
            ttgo->motor->onec(200 * val / 127);
            pulseRadius = val;
        }
    }
}

void onMidiNoteOn(uint8_t chan, uint8_t note, uint8_t vel, uint16_t timestamp) {
    // Note-on sets pad colour. Note number = 48 + pad (48..63). Velocity = colour (0..29).
    uint8_t pad = note - 48;
    if (chan != settings[SETTING_MIDICHAN] || pad > 15 || vel > 29)
        return;
    screenOn();
    launchPads[pad]->m_bg = PAD_COLOURS[vel];
}

void screenOn() {
    screenTimeout = settings[SETTINGS_TIMEOUT];
    if (!standby)
        return;
    standby = false;
    refresh();
    ttgo->openBL();
}

void screenOff() {
    if (standby)
        return;
    standby = true;
    ttgo->closeBL();
    screenTimeout = 0;
}

void onPowerButtonShortPress() {
    // Short press of power button changes mode
    switch (mode) {
        case MODE_MIDICHAN:
        case MODE_CCX:
        case MODE_CCY:
        case MODE_CCRX:
        case MODE_BRIGHTNESS:
        case MODE_TIMEOUT:
            mode = MODE_SETTINGS;
            break;
        case MODE_MENU:
            screenOff();
            return;
        case MODE_SETTINGS:
            {
            uint32_t magic = MAGIC;
                EEPROM.writeBytes(0, &magic, 4);
                EEPROM.writeBytes(4, settings, eepromSize);
                EEPROM.commit();
            }
            // Fall through to default
        default:
            selPad = 255;
            mode = MODE_MENU;
    }
    screenOn();
}

void onPowerButtonLongPress() {
    mode = MODE_SETTINGS;
    settingsOffset = 0;
    screenOn();
}

void refresh() {
    canvas->fillScreen(TFT_BLACK); // Clear screen
    canvas->setTextColor(TFT_WHITE);  // Adding a background colour erases previous text automatically
    switch(mode) {
        case MODE_MENU:
            menuCanvas->fillSprite(TFT_BLACK);
            for (uint8_t pad = 0; pad < 5; ++pad)
                menuBtns[pad]->draw(selPad == pad);
            break;
        case MODE_ENCODERS:
            canvas->fillRoundRect(0, 0, 59, 220, 10, TFT_DARKGREY);
            canvas->fillRoundRect(60, 0, 59, 220, 10, TFT_DARKGREY);
            canvas->fillRoundRect(120, 0, 59, 220, 10, TFT_DARKGREY);
            canvas->fillRoundRect(180, 0, 59, 220, 10, TFT_DARKGREY);
            break;
        case MODE_XY:
            canvas->drawLine(crosshair_x, 0, crosshair_x, 240, TFT_YELLOW);
            canvas->drawLine(0, crosshair_y, 240, crosshair_y, TFT_YELLOW);
            if (pulseRadius)
                canvas->drawCircle(120, 140, pulseRadius--, TFT_DARKCYAN);
            break;
        case MODE_PADS:
            for (uint8_t pad = 0; pad < 16; ++pad)
                launchPads[pad]->draw(selPad == pad);
            break;
        case MODE_NAVIGATE1:
        case MODE_NAVIGATE2:
            for (uint8_t pad = 0; pad < 9; ++pad)
                navigationBtns[pad]->draw(navigationBtns[pad]->m_mode == selPad);
            break;
        case MODE_SETTINGS:
            // Settings scrollbar
            canvas->fillRect(236, 0, 4, 240, TFT_DARKGREY);
            canvas->fillRect(236, 0 + 220 / 4 * settingsOffset, 4, 220 / 4, 0xa514);

            for (uint8_t i = 0; i < 4; ++i) {
                uint8_t j = settingsOffset + i;
                gfxButton* btn = settingsBtns[j];
                btn->m_y = i * 55;
                if (j == SETTING_BRIGHTNESS)
                    btn->drawBar(100 * settings[SETTING_BRIGHTNESS] / 255);
                else if (j == SETTINGS_TIMEOUT)
                    btn->drawBar(100 * settings[SETTINGS_TIMEOUT] / 120);
                else
                    btn->draw();
                canvas->setTextDatum(MR_DATUM);
                int32_t x = 210;
                int32_t y = 27 + 55 * i;
                if (j == 0)
                    if (settings[SETTING_BLE])
                        canvas->drawString("ON", x, y, 4);
                    else
                        canvas->drawString("OFF", x, y, 4);
                else if (j == 1)
                    canvas->drawNumber(settings[j] + 1, x, y, 4);
                else
                    canvas->drawNumber(settings[j], x, y, 4);
                canvas->setTextDatum(TL_DATUM);
            }
            break;
        case MODE_MIDICHAN:
        case MODE_CCX:
        case MODE_CCY:
        case MODE_CCRX:
            // Draw numeric keypad
            for (uint8_t i = 0; i < 11; ++i)
                numPad[i]->draw();
            break;
    }

    if (topDrag > 20) {
        menuCanvas->pushSprite(0, topDrag - 220);
        canvas->pushSprite(0, topDrag);
        return;
    } else if (mode == MODE_MENU) {
        menuCanvas->pushSprite(0, 20);
    } else {
        canvas->pushSprite(0, 20);
    }
    showStatus();
}

void showStatus() {
    statusCanvas->fillSprite(TFT_BLACK);
    char s[10];
    statusCanvas->fillRect(0, 0, 239, 19, 0x1082); // Status bar background
    statusCanvas->fillRect(160, 5, 20, 10, TFT_DARKGREY); // Battery body
    statusCanvas->fillRect(180, 7, 2, 6, TFT_DARKGREY); // Battery tip
    statusCanvas->fillRect(160, 6, 20 * battery / 100, 8, battery < 10?TFT_RED:TFT_DARKGREEN); // Battery content
    if (battery > 90)
        statusCanvas->fillRect(179, 8, 2, 4, TFT_DARKGREEN); // Battery content
    statusCanvas->setTextColor(TFT_WHITE);
    sprintf(s, "%d%%", battery);
    statusCanvas->drawString(s, 190, 2, 2);
    //BLE connection
    if (settings[SETTING_BLE]) {
        statusCanvas->setTextColor(BLEMidiServer.isConnected()?TFT_BLUE:TFT_DARKGREY);
        statusCanvas->drawString("\x8D", 226, 1);
        /*
        statusCanvas->fillRoundRect(226, 1, 10, 18, 4, BLEMidiServer.isConnected()?TFT_BLUE:TFT_DARKGREY);
        statusCanvas->drawLine(228, 6, 232, 12, TFT_WHITE);
        statusCanvas->drawLine(232, 12, 230, 15, TFT_WHITE);
        statusCanvas->drawLine(230, 15, 230, 3, TFT_WHITE);
        statusCanvas->drawLine(230, 3, 232, 6, TFT_WHITE);
        statusCanvas->drawLine(232, 6, 228, 12, TFT_WHITE);
        */
    }
    statusCanvas->pushSprite(0, 0);
}

void startBle() {
    BLEMidiServer.begin("riband");
    BLEMidiServer.setOnConnectCallback(onBleConnect);
    BLEMidiServer.setOnDisconnectCallback(onBleDisconnect);
    BLEMidiServer.setControlChangeCallback(onMidiCC);
    BLEMidiServer.setNoteOnCallback(onMidiNoteOn);
}

void toggleBle() {
    if (settings[SETTING_BLE]) {
        BLEMidiServer.end();
    } else {
        startBle();
    }
    settings[SETTING_BLE] = !settings[SETTING_BLE];
}

void numEntry() {
    static uint8_t val = 0;
    static uint8_t offset = 0;
    uint8_t oMax;
    uint16_t v;

    if (oskSel == 10) {
        val = 0;
        offset = 0;
        v = 0;
    } else {
        v = val * 10 + oskSel;
    }

    if (mode == MODE_MIDICHAN) {
        if (offset == 0 && v > 1 || offset == 1 && (v < 0 || v > 16))
            return;
        oMax = 2;
    } else {
        if (offset == 0 && v > 1 || offset == 1 && v > 12 || offset == 2 && v > 127)
            return;
        oMax = 3;
    }

    char s[10];
    if (oskSel < 10) {
        // Create numeric sting from current value
        char fmt[5];
        sprintf(fmt, "%%0%dd", ++offset);
        sprintf(s, fmt, v);
    }

    for (uint8_t i = 0; i < oMax - offset; ++i)
        sprintf(s + offset + i * 2, " _");

    numPad[10]->setText(s);

    if (offset >= oMax) {
        offset = 0;
        val = 0;
        if (mode == MODE_MIDICHAN) {
            if (v > 0)
                settings[SETTING_MIDICHAN] = v - 1;
        } else {
            settings[mode - MODE_BLE] = v;
        }
        refresh(); // Show the briefly change before closing numpad
        delay(300);
        mode = MODE_SETTINGS;
        numPad[10]->setText("");
    } else {
        val = v;
    }
}
