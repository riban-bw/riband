#include <LilyGoWatch.h> // Provides watch API
#include <BLEMidi.h>

enum modes {
    MODE_XY, // X-Y CC pad
    MODE_PADS, // Grid of launch pads
    MODE_ADMIN, // Config/power-off option
    MODE_SETTINGS, // Settings view
    MODE_POWEROFF,
    MODE_BLE,
    MODE_MIDICHAN,
    MODE_XCC,
    MODE_YCC,
    MODE_RXCC,
    MODE_NONE
};

TTGOClass *ttgo; // Pointer to singleton instance of ttgo watch object
BMA *accel; // Pointer to accelerometer sensor

class gfxButton {
    public:
        gfxButton(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t bg, uint16_t bgh, const char* text=nullptr, uint8_t mode=MODE_NONE) :
            m_x(x), m_y(y), m_w(w), m_h(h), m_bg(bg), m_bgh(bgh), m_mode(mode) {
                m_rad = m_h / 4;
                if (text) {
                    m_text = (char*)malloc(strlen(text));
                    sprintf(m_text, text, strlen(text));
                }
            };

        void draw(bool hl=false) {
            ttgo->tft->fillRoundRect(m_x, m_y, m_w, m_h, m_rad, hl?m_bgh:m_bg);
            if (m_text) {
                ttgo->tft->setTextColor(TFT_WHITE);
                ttgo->tft->setTextFont(4);
                ttgo->tft->setCursor(m_x + 10, m_y + 10);
                ttgo->tft->print(m_text);
            }
        }

        bool bounds(uint16_t x, uint16_t y) {
            return (x >= m_x && x <= (m_x + m_w) && y >= m_y && y <= (m_y + m_h));
        }

        uint8_t getMode() {
            return m_mode;
        }

    uint16_t m_x, m_y, m_w, m_h;
    uint32_t m_bg, m_bgh;
    uint8_t m_rad;
    uint8_t m_mode = MODE_NONE;
    char * m_text = nullptr;
};

gfxButton* adminBtns[] = {
    new gfxButton(20, 60, 200, 40, TFT_OLIVE, TFT_GOLD, "SETTINGS", MODE_SETTINGS),
    new gfxButton(20, 140, 200, 40, TFT_OLIVE, TFT_GOLD, "POWER OFF", MODE_POWEROFF)};
gfxButton* settingsBtns[] = {
    new gfxButton(20, 0, 200, 40, TFT_OLIVE, TFT_GOLD, "BLE", MODE_BLE),
    new gfxButton(20, 50, 200, 40, TFT_OLIVE, TFT_GOLD, "MIDI CHAN", MODE_MIDICHAN),
    new gfxButton(20, 100, 200, 40, TFT_OLIVE, TFT_GOLD, "X-CC", MODE_XCC),
    new gfxButton(20, 150, 200, 40, TFT_OLIVE, TFT_GOLD, "Y-CC", MODE_YCC),
    new gfxButton(20, 200, 200, 40, TFT_OLIVE, TFT_GOLD, "Rx-CC", MODE_RXCC)};
gfxButton* launchPads[16];

uint8_t midiChan = 14; // MIDI channel for incoming and outgoing messages
uint8_t midiCCx = 101; // MIDI CC sent when X axis swiped
uint8_t midiCCy = 102; // MIDI CC sent when Y axis swiped
uint8_t midiCC = 100; // MIDI CC of incoming CC trigger
uint8_t pulseRadius = 0; // Radius of pulse cirle (decreases over time)
uint8_t lastPulseRadius = 0; // Radius of last pulse cirle (used to clear circle)
uint8_t mode = MODE_XY; // Menu / display mode
uint8_t selPad = 0; // Index of selected pad
uint8_t oskSel = MODE_NONE; // Index of button selected on touch screen
volatile uint32_t screenTimeout = 0; // Countdown timer until auto standby mode
uint32_t powerButtonDebounce = 0; // Scheduled time for post-debounce switch change handler
uint32_t powerButtonLongTime = -1; // Scheduled time for long press
bool standby = true; // True if in standby mode (screen off)
volatile bool irq = false;
bool bleEnabled = false;

// Forward declarations
void screenOn();
void screenOff();
void refresh();
void processTouch();
bool processAccel();
void onPowerButtonLongPress();
void onPowerButtonShortPress();
void onBleConnect();
void onBleDisconnect();
void onMidiCC(uint8_t, uint8_t, uint8_t, uint16_t);
void showStatus();
void toggleBle();

// Initialisation
void setup(void)
{
    Serial.begin(115200); // Can use USB for debug

    for (uint8_t col = 0; col < 4; ++col) {
        for (uint8_t row = 0; row < 4; ++row) {
            launchPads[row + 4 * col] = new gfxButton(col * 60, row * 60, 58, 58, TFT_DARKGREEN, TFT_GREEN);
        }
    }

    ttgo = TTGOClass::getWatch(); // Create instance of watch object (singleton)
    ttgo->begin(); // Iniitalise watch object

    ttgo->motor_begin();
    ttgo->openBL();

    // Configure power button
    pinMode(AXP202_INT, INPUT_PULLUP);
    attachInterrupt(AXP202_INT, [] {
        irq = true;
    }, FALLING);

    //!Clear IRQ unprocessed first
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

    uint32_t now = millis();
    processTouch();
    processAccel();

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

    if (screenTimeout && now != lastNow && (--screenTimeout == 0))
        screenOff();

    if (pulseRadius && (nextPulse < now)) {
        nextPulse = now + 6;
        ttgo->tft->drawCircle(120, 120, lastPulseRadius, TFT_BLACK);
        lastPulseRadius = pulseRadius;
        ttgo->tft->drawCircle(120, 120, pulseRadius--, TFT_DARKCYAN);
    }

    if (nextSecond < now) {
        nextSecond += 1000;
        blink = !blink;
    }

    lastNow = now;
}

void processTouch() {
    static int16_t x,y;
    static uint8_t last_cc_x, last_cc_y;
    uint8_t cc_x, cc_y;

    if(ttgo->getTouch(x, y)) {
        switch(mode) {
            case MODE_XY:
                cc_x = x * 127 / 240;
                cc_y = y * 127 / 240;
                if (cc_x != last_cc_x) {
                    if (bleEnabled)
                        BLEMidiServer.controlChange(midiChan, midiCCx, cc_x);
                    last_cc_x = cc_x;
                }
                if (cc_y != last_cc_y) {
                    if (bleEnabled)
                        BLEMidiServer.controlChange(midiChan, midiCCy, cc_y);
                    last_cc_y = cc_y;
                }
                break;
            case MODE_PADS:
            {
                uint8_t pad = y / 60 + x / 60 * 4;
                if (pad < 16) {
                    if (pad != selPad) {
                        launchPads[selPad]->draw();
                        selPad = pad;
                        launchPads[selPad]->draw(true);
                    }
                }
                break;
            }
            case MODE_ADMIN:
            {
                uint8_t oldOsk = oskSel;
                oskSel = MODE_NONE;
                for (uint8_t i = 0; i < 2; ++i) {
                    gfxButton* btn = adminBtns[i];
                    if (btn->bounds(x, y))
                        oskSel = btn->getMode();
                }
                if (oskSel != oldOsk)
                    refresh(); // Highlight touched button
                break;
            }
            case MODE_SETTINGS:
            {
                uint8_t oldOsk = oskSel;
                oskSel = MODE_NONE;
                for (uint8_t i = 0; i < 5; ++i) {
                    gfxButton* btn = settingsBtns[i];
                    if (btn->bounds(x, y))
                        oskSel = btn->getMode();
                }
                if (oskSel != oldOsk)
                    refresh();
                break;
            }
        }
    } else {
        switch(oskSel) {
            case MODE_POWEROFF:
                ttgo->powerOff();
                break;
            case MODE_BLE:
                toggleBle();
                refresh();
            case MODE_NONE:
                break;
            default:
                mode = oskSel;
                refresh();
                break;
        }
        oskSel = MODE_NONE;
    }
    screenOn();
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
        ttgo->tft->setTextFont(4);
        ttgo->tft->setCursor(40, 100);
        ttgo->tft->printf("%03d, %03d, %03d", acc.x, acc.y, acc.z);
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
    if (chan == midiChan && cc == midiCC) {
        screenOn();
        ttgo->motor->onec(200 * val / 127);
        pulseRadius = val;
    }
}

void screenOn() {
    screenTimeout = 20000;
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
    if(++mode > MODE_PADS) {
        mode = MODE_XY;
        screenOff();
    } else {
        refresh();
    }
}

void onPowerButtonLongPress() {
    mode = MODE_ADMIN;
    refresh();
}

void refresh() {
    switch(mode) {
        case MODE_XY:
            ttgo->tft->fillScreen(TFT_BLACK); // Clear screen
            ttgo->tft->setTextColor(TFT_WHITE, TFT_BLACK);  // Adding a background colour erases previous text automatically
            break;
        case MODE_PADS:
            for (uint8_t pad = 0; pad < 16; ++pad)
                launchPads[pad]->draw(selPad == pad);
            break;
        case MODE_ADMIN:
            ttgo->tft->fillScreen(TFT_BLACK);
            for (uint8_t i = 0; i < 2; ++i)
                adminBtns[i]->draw();
            break;
        case MODE_SETTINGS:
            ttgo->tft->fillScreen(TFT_BLACK);
            for (uint8_t i = 0; i < 5; ++i)
                settingsBtns[i]->draw();
            ttgo->tft->setCursor(170, 10);
            ttgo->tft->printf("%s", bleEnabled?"ON":"OFF");
            ttgo->tft->setCursor(170, 60);
            ttgo->tft->printf("%d", midiChan);
            ttgo->tft->setCursor(170, 110);
            ttgo->tft->printf("%d", midiCCx);
            ttgo->tft->setCursor(170, 160);
            ttgo->tft->printf("%d", midiCCy);
            ttgo->tft->setCursor(170, 210);
            ttgo->tft->printf("%d", midiCC);
            break;
    }
    showStatus();
}

void showStatus() {
    ttgo->tft->fillRect(180, 0, 239, 1, bleEnabled && BLEMidiServer.isConnected()?TFT_BLUE:TFT_BLACK);
    if (mode == MODE_PADS && !BLEMidiServer.isConnected())
        launchPads[12]->draw();
}

void toggleBle() {
    if (bleEnabled) {
        BLEMidiServer.end();
    } else {
        BLEMidiServer.begin("riban MIDI Watch");
        BLEMidiServer.setOnConnectCallback(onBleConnect);
        BLEMidiServer.setOnDisconnectCallback(onBleDisconnect);
        BLEMidiServer.setControlChangeCallback(onMidiCC);
    }
    bleEnabled = !bleEnabled;
}