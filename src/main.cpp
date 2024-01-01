/*  riband - BLE MIDI wearable writsband
Copyright (C) 2023  riban ltd <info@riban.co.uk>

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
    - Set colours / state of pads from MIDI note-on/off
    - Improve BLE connection indication
*/

#include <LilyGoWatch.h> // Provides watch API
#include <BLEMidi.h> // Provides BLE MIDI interface
#include <EEPROM.h>

#define MAGIC 0x7269626e // Used to check if EEPROM has been initialised

enum mode_enum {
    MODE_XY,
    MODE_PADS,
    MODE_SETTINGS,
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
TFT_eSprite* sprite; // Pointer to sprite acting as display double buffer

class gfxButton {
    public:
        gfxButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint32_t bg, uint32_t bgh, const char* text=nullptr, uint8_t mode=MODE_NONE) :
            m_x(x), m_y(y), m_w(w), m_h(h), m_bg(bg), m_bgh(bgh), m_mode(mode) {
                m_fg = TFT_WHITE;
                m_rad = m_h / 4;
                m_indent_x = m_w / 2;
                m_indent_y = m_h / 2;
                if (text) {
                    m_text = (char*)malloc(strlen(text));
                    sprintf(m_text, text, strlen(text));
                }
            };

        void draw(bool hl=false) {
            sprite->fillRoundRect(m_x, m_y, m_w, m_h, m_rad, hl?m_bgh:m_bg);
            if (m_text) {
                sprite->setTextColor(m_fg);
                sprite->setTextDatum(m_align);
                sprite->drawString(m_text, m_x + m_indent_x, m_y + m_indent_y, 4);
                sprite->setTextDatum(TL_DATUM);
            }
        }

        void drawBar(uint16_t percent) {
            uint16_t x = percent * m_w / 100;
            sprite->fillRoundRect(m_x, m_y, m_w, m_h, m_rad, m_bg);
            sprite->fillRoundRect(m_x, m_y, x, m_h, m_rad, m_bgh);
            if (m_text) {
                sprite->setTextColor(m_fg);
                sprite->setTextDatum(m_align);
                sprite->drawString(m_text, m_x + m_indent_x, m_y + m_indent_y, 4);
                sprite->setTextDatum(TL_DATUM);
            }
        }

        bool bounds(uint16_t x, uint16_t y) {
            return (x >= m_x && x <= (m_x + m_w) && y >= m_y && y <= (m_y + m_h));
        }

        uint8_t getMode() {
            return m_mode;
        }

    uint32_t m_bg, m_bgh, m_fg;
    uint16_t m_x, m_y, m_w, m_h;
    uint8_t m_rad;
    uint8_t m_mode = MODE_NONE;
    uint8_t m_indent_x;
    uint8_t m_indent_y;
    uint8_t m_align = MC_DATUM;
    char * m_text = nullptr;
};

gfxButton* settingsBtns[] = {
    new gfxButton(20, 0, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "BLE", MODE_BLE),
    new gfxButton(20, 60, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "MIDI CHAN", MODE_MIDICHAN),
    new gfxButton(20, 120, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "X-CC", MODE_CCX),
    new gfxButton(20, 180, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "Y-CC", MODE_CCY),
    new gfxButton(20, 240, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "Rx-CC", MODE_CCRX),
    new gfxButton(20, 300, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "Brightness", MODE_BRIGHTNESS),
    new gfxButton(20, 300, 200, 58, TFT_DARKGREY, TFT_LIGHTGREY, "Screen time", MODE_TIMEOUT)};
gfxButton* launchPads[16];
gfxButton* numPad[11];

uint8_t settings[] = {0, 15, 101, 102, 100, 100, 60}; // Array of 8-bit settings - see setting_enum
uint8_t pulseRadius = 0; // Radius of pulse cirle (decreases over time)
uint8_t lastPulseRadius = 0; // Radius of last pulse cirle (used to clear circle)
uint8_t mode = MODE_XY; // Menu / display mode
uint8_t selPad = -1; // Index of selected pad
uint8_t oskSel = MODE_NONE; // Index of button selected on touch screen
uint8_t settingsOffset = 0; // Settings view scroll position
uint8_t crosshair_x, crosshair_y; // Coordinates of X-Y controller crosshairs
uint8_t battery; // Battery %
volatile uint32_t screenTimeout = 0; // Countdown timer until auto standby mode
uint32_t now = 0; // Time of current loop process
bool standby = true; // True if in standby mode (screen off)
volatile bool irq = false;
uint8_t eepromSize = sizeof(settings);

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
void showStatus();
void numEntry();

// Initialisation
void setup(void)
{
    Serial.begin(115200); // Can use USB for debug

    ttgo = TTGOClass::getWatch(); // Create instance of watch object (singleton)
    ttgo->begin(); // Initialise watch object
    sprite = new TFT_eSprite(ttgo->tft);
    sprite->createSprite(240, 240);

    EEPROM.begin(eepromSize + 4);
    uint32_t magic;
    EEPROM.readBytes(0, &magic, 4);
    if (magic == MAGIC) {
        EEPROM.readBytes(4, settings, eepromSize);
        ttgo->setBrightness(settings[SETTING_BRIGHTNESS]);
    }

    // Configure settings menu buttons
    for (uint8_t i = 0; i < 7; ++i) {
        gfxButton* btn = settingsBtns[i];
        btn->m_align = ML_DATUM;
        btn->m_indent_x = 10;
    }

    // Build launchpad grid
    for (uint8_t col = 0; col < 4; ++col) {
        for (uint8_t row = 0; row < 4; ++row) {
            launchPads[row + 4 * col] = new gfxButton(col * 60, row * 60, 58, 58, TFT_DARKGREEN, TFT_GREEN);
        }
    }

    // Build numeric keyapd
    numPad[0] = new gfxButton(0, 0, 78, 58, TFT_DARKGREY, TFT_LIGHTGREY, "0", 0);
    char txt[2];
    for (uint8_t col = 0; col < 3; ++col) {
        for (uint8_t row = 0; row < 3; ++row) {
            uint8_t i = col + row * 3 + 1;
            sprintf(txt, "%d", i);
            numPad[i] = new gfxButton(col * 80, 60 + row * 60, 78, 58, TFT_DARKGREY, TFT_LIGHTGREY, txt, i);
        }
    }
    numPad[10] = new gfxButton(80, 0, 158, 58, TFT_LIGHTGREY, TFT_DARKGREY, "   ", 10);
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
    static uint32_t lastNow = 0;
    static uint32_t nextSecond = 0;
    static uint32_t nextPulse = 0;
    static uint32_t lastBtnPress = 0;
    static bool btnPressed = false;
    static bool blink = false;

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

    if (now != lastNow) {
        // 1ms (or slower)
        processTouch();
        processAccel();
        if (!standby && now > lastNow + 20)
            refresh();
    }

    if (pulseRadius && (nextPulse < now))
        nextPulse = now + 6;

    if (nextSecond < now) {
        nextSecond += 1000;
        if (screenTimeout && (--screenTimeout == 0))
            screenOff();
        battery = ttgo->power->getBattPercentage();
        blink = !blink;
    }

    lastNow = now;
}

void processTouch() {
    static uint32_t touchTime = 0, releaseTime = 0, lastUpdateTime = 0;
    static int16_t x, y, startX, startY, lastX, lastY;
    static uint8_t last_cc_x, last_cc_y;
    static bool dragging = false, scrolling = false;
    uint8_t cc_x, cc_y;

    if(ttgo->getTouch(x, y)) {
        screenOn();
        if (!dragging) {
            if (now > touchTime + 100) {
                // First touch debounced
                startX = lastX = x;
                startY = lastY = y;
                touchTime = now;
                dragging = true;
            }
        } else {
            // Dragging
            switch(mode) {
                case MODE_XY:
                    cc_x = x * 127 / 240;
                    cc_y = 127 - y * 127 / 240;
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
                        crosshair_y = y;
                    }
                    break;
                case MODE_PADS:
                {
                    uint8_t pad = y / 60 + x / 60 * 4;
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
                        dY = (y - startY) / 60;
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
        }
    } else if(dragging) {
        // Release
        if (touchTime + 200 > now)
            return; // debounce
        releaseTime = now;
        dragging = false;
        if (scrolling) {
            scrolling = false;
            return;
        }
        switch (mode) {
            case MODE_PADS:
                BLEMidiServer.noteOn(settings[SETTING_MIDICHAN], 48 + selPad, 0);
                selPad = -1;
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
        sprite->setTextFont(4);
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
    showStatus();
}

void onMidiCC(uint8_t chan, uint8_t cc, uint8_t val, uint16_t timestamp) {
    if (chan == settings[SETTING_MIDICHAN] && cc == settings[SETTING_CCRX]) {
        screenOn();
        ttgo->motor->onec(200 * val / 127);
        pulseRadius = val;
    }
}

void screenOn() {
    screenTimeout = settings[SETTINGS_TIMEOUT];
    if (!standby)
        return;
    ttgo->openBL();
    standby = false;
    refresh();
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
        default:
            if (mode == MODE_SETTINGS) {
                uint32_t magic = MAGIC;
                EEPROM.writeBytes(0, &magic, 4);
                EEPROM.writeBytes(4, settings, eepromSize);
                EEPROM.commit();
            }
            if(++mode > MODE_PADS) {
                mode = MODE_XY;
                refresh();
                screenOff();
                return;
            }
    }
    screenOn();
    refresh();
}

void onPowerButtonLongPress() {
    mode = MODE_SETTINGS;
    settingsOffset = 0;
    screenOn();
    refresh();
}

void refresh() {
    sprite->fillScreen(TFT_BLACK); // Clear screen
    sprite->setTextColor(TFT_WHITE);  // Adding a background colour erases previous text automatically
    sprite->setTextFont(4);
    switch(mode) {
        case MODE_XY:
            sprite->drawLine(crosshair_x, 0, crosshair_x, 240, TFT_RED);
            sprite->drawLine(0, crosshair_y, 240, crosshair_y, TFT_RED);
            if (pulseRadius)
                sprite->drawCircle(120, 120, pulseRadius--, TFT_DARKCYAN);
            break;
        case MODE_PADS:
            for (uint8_t pad = 0; pad < 16; ++pad)
                launchPads[pad]->draw(selPad == pad);
            break;
        case MODE_SETTINGS:
            for (uint8_t i = 0; i < 4; ++i) {
                uint8_t j = settingsOffset + i;
                gfxButton* btn = settingsBtns[j];
                btn->m_y = i * 60;
                if (j == SETTING_BRIGHTNESS)
                    btn->drawBar(100 * settings[SETTING_BRIGHTNESS] / 255);
                else if (j == SETTINGS_TIMEOUT)
                    btn->drawBar(100 * settings[SETTINGS_TIMEOUT] / 120);
                else
                    btn->draw();
                sprite->setTextDatum(MR_DATUM);
                int32_t x = 210;
                int32_t y = 29 + 60 * i;
                if (j == 0)
                    if (settings[SETTING_BLE])
                        sprite->drawString("ON", x, y, 4);
                    else
                        sprite->drawString("OFF", x, y, 4);
                else if (j == 1)
                    sprite->drawNumber(settings[j] + 1, x, y, 4);
                else
                    sprite->drawNumber(settings[j], x, y, 4);
                sprite->setTextDatum(TL_DATUM);
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
    showStatus();
    sprite->pushSprite(0, 0);
}

void showStatus() {
    sprite->setTextFont(2);
    sprite->setCursor(200, 0);
    sprite->setTextColor(BLEMidiServer.isConnected()?TFT_BLUE:TFT_WHITE);
    sprite->printf("%d%%", battery);
}

void startBle() {
    BLEMidiServer.begin("riband");
    BLEMidiServer.setOnConnectCallback(onBleConnect);
    BLEMidiServer.setOnDisconnectCallback(onBleDisconnect);
    BLEMidiServer.setControlChangeCallback(onMidiCC);
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

    sprintf(numPad[10]->m_text, s);

    if (offset >= oMax) {
        offset = 0;
        val = 0;
        if (mode == MODE_MIDICHAN) {
            if (v > 0)
                settings[SETTING_MIDICHAN] = v - 1;
        } else {
            settings[mode - MODE_BLE] = v;
        }
        refresh();
        delay(300);
        mode = MODE_SETTINGS;
        sprintf(numPad[10]->m_text, "");
    } else {
        val = v;
    }
}
