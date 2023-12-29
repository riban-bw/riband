#include <LilyGoWatch.h> // Provides watch API
#include <BLEMidi.h>

enum modes {
    MODE_KAOS,
    MODE_PADS,
};

TTGOClass *ttgo; // Pointer to singleton instance of ttgo watch object
BMA *accel; // Pointer to accelerometer sensor

uint8_t midiChan = 14;
uint8_t midiCCx = 101;
uint8_t midiCCy = 102;
uint8_t midiCC = 100;
uint8_t pulseRadius = 0;
uint8_t lastPulseRadius = 0;
uint8_t mode = MODE_KAOS;
uint8_t selPad = 255;
volatile uint32_t screenTimeout = 0;
bool powerBtnIrq = false;
bool standby = true;

// Forward declarations
void screenOn();
void screenOff();
void refresh(bool force = false);
void processTouch();
bool processAccel();
void onPowerButton();
void onBleConnect();
void onBleDisconnect();
void onMidiCC(uint8_t, uint8_t, uint8_t, uint16_t);
void drawPads();
void showStatus();

// Initialisation
void setup(void)
{
    Serial.begin(115200); // Can use USB for debug

    ttgo = TTGOClass::getWatch(); // Create instance of watch object (singleton)
    ttgo->begin(); // Iniitalise watch object

    ttgo->motor_begin();
    ttgo->openBL();

    // Configure power button interrupt
    attachInterrupt(AXP202_INT, [] {
        powerBtnIrq = true;
        }, FALLING);
    ttgo->power->enableIRQ(AXP202_PEK_SHORTPRESS_IRQ, true);
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

    // Enable BLE MIDI
    BLEMidiServer.begin("riban MIDI Watch");
    BLEMidiServer.setOnConnectCallback(onBleConnect);
    BLEMidiServer.setOnDisconnectCallback(onBleDisconnect);
    BLEMidiServer.setControlChangeCallback(onMidiCC);

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

    if (powerBtnIrq)
        onPowerButton();

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
            case MODE_KAOS:
                cc_x = x * 127 / 240;
                cc_y = y * 127 / 240;
                if (cc_x != last_cc_x) {
                    BLEMidiServer.controlChange(midiChan, midiCCx, cc_x);
                    last_cc_x = cc_x;
                }
                if (cc_y != last_cc_y) {
                    BLEMidiServer.controlChange(midiChan, midiCCy, cc_y);
                    last_cc_y = cc_y;
                }
                break;
            case MODE_PADS:
                selPad = y / 60 + x / 60 * 4;
                drawPads();
                break;
        }
        screenOn();
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
                //ttgo->tft->printf("DOWN   ");
                break;
            case DIRECTION_DISP_UP:
                //ttgo->tft->printf("UP    ");
                break;
            case DIRECTION_BOTTOM_EDGE:
                //ttgo->tft->printf("BOTTOM ");
                break;
            case DIRECTION_TOP_EDGE:
                //ttgo->tft->printf("TOP   ");
                break;
            case DIRECTION_RIGHT_EDGE:
                //ttgo->tft->printf("RIGHT  ");
                break;
            case DIRECTION_LEFT_EDGE:
                //ttgo->tft->printf("LEFT   ");
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
    refresh();
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
    refresh(standby);
    if (!standby)
        return;
    ttgo->openBL();
    standby = false;
}

void screenOff() {
    if (standby)
        return;
    standby = true;
    ttgo->closeBL();
    screenTimeout = 0;
}

void onPowerButton() {
    // Short press of power button changes mode
    ttgo->power->clearIRQ();
    powerBtnIrq = false;
    if (standby)
        screenOn();
    else if(++mode > MODE_PADS) {
        mode = MODE_KAOS;
        screenOff();
    } else {
        refresh(true);
    }
}

void refresh(bool force) {
    switch(mode) {
        case MODE_KAOS:
            if (force)
                ttgo->tft->fillScreen(TFT_BLACK); // Clear screen
            ttgo->tft->setTextColor(TFT_WHITE, TFT_BLACK);  // Adding a background colour erases previous text automatically
            break;
        case MODE_PADS:
            drawPads();
            break;
    }
    showStatus();
}

void drawPads() {
    for (uint8_t row = 0; row < 4; ++row) {
        for (uint8_t col = 0; col < 4; ++col) {
            if (selPad == col + row * 4)
                ttgo->tft->fillRect(row * 60, col * 60, 58, 58, TFT_GREEN);
            else
                ttgo->tft->fillRect(row * 60, col * 60, 58, 58, TFT_DARKGREEN);
        }
    }
}

void showStatus() {
    if (mode == MODE_PADS)
        ttgo->tft->fillCircle(230, 5, 5, BLEMidiServer.isConnected()?TFT_BLUE:TFT_DARKGREEN);
    else
        ttgo->tft->fillCircle(230, 5, 5, BLEMidiServer.isConnected()?TFT_BLUE:TFT_BLACK);
}
