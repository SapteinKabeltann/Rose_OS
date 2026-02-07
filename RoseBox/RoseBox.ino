#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <time.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <GxEPD2_BW.h>
#include "icons.h"
#include "home.h"
#include "App.h"

// Øk stack for loop-task (Lua + display + BLE bruker mye – unngår abort/stack overflow på core 1)
size_t getArduinoLoopTaskStackSize(void) {
    return 16384;
}

// BLE (Nordic UART) – samme UUID som RoseOS for kompatibilitet
#define BLE_SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_UUID_RX        "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define BLE_CHAR_UUID_TX        "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static BLEServer* pBLEServer = nullptr;
static BLECharacteristic* pBLETxChar = nullptr;
static bool bleDeviceConnected = false;
static bool bleOldDeviceConnected = false;

// Display settings (lagres i NVS, som RoseOS)
static bool displayInverted = false;
static int refreshCount = 1;
static bool partialRefreshEnabled = true;
static Preferences preferences;
static int partialUpdateCount = 0;

// E-Paper Display Pins (LilyGo T5 v2.3.1)
#define EPD_CS    5
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  4

// TF/SD Card Pins (LilyGo T5 - bak på kortet: CS 13, MOSI 15, SCK 14, MISO 2)
#define SD_CS     13

// Battery voltage (ADC med spenningsdeler på LilyGo T5)
#define BATTERY_PIN 35

GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
    GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// --- Status ---
bool sdReady = false;
bool littlefsReady = false;
bool spiffsReady = false;  // Sketch Data Upload bruker ofte SPIFFS-partisjon
WiFiClient tcpClient;

// WiFi lagret for BLE-kommandoer og auto-connect ved oppstart
static String wifiStoredSSID = "";
static String wifiStoredPass = "";
static bool ntpConfigDone = false;  // NTP settes én gang når WiFi er koblet

// BLE-injisert knapp: CMD:BUTTON:SHORT/LONG/LONG:5SEC setter denne; keyboardGetKey() returnerer og clearer
static String s_bleInjectedKey = "";

// BLE: liten buffer (20–50 byte anbefalt) – unngår StoreProhibited. Prosessering skjer i loop(), ikke i callback.
#define BLE_RX_BUFFER_MAX 128
static String bleRxBuffer = "";
static volatile bool bleDataPending = false;

// Forward for BLE
static void processBLECommand(String cmd);
static void initBLE();
static void bleSend(String msg);
static void handleBLEConnection();

// (GPIO brukes direkte: pinMode(39, INPUT), digitalRead(39) i keyboardGetKey)

// --- Display / skjerm (applyDisplayWindow, getAppIcon) ---
// Software-inversjon: EPD_FG = forgrunn (normalt svart), EPD_BG = bakgrunn (normalt hvit)
static inline uint16_t EPD_FG() { return displayInverted ? GxEPD_WHITE : GxEPD_BLACK; }
static inline uint16_t EPD_BG() { return displayInverted ? GxEPD_BLACK : GxEPD_WHITE; }

// Batteri (LiPo 4.0V = 100%, 3.0V = 0%)
static int getBatteryPercent() {
    int raw = analogRead(BATTERY_PIN);
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
    float percent = ((voltage - 3.0f) / (4.0f - 3.0f)) * 100.0f;
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    return (int)percent;
}

static void drawBatteryIcon(int x, int y) {
    int percent = getBatteryPercent();
    display.drawBitmap(x, y, epd_bitmap_Battery, 20, 10, EPD_FG());
    int bars = (percent + 12) / 25;
    const int barWidth = 3, barGap = 1, barHeight = 6;
    int startBarX = x + 2, startBarY = y + 2;
    for (int i = 0; i < bars && i < 4; i++) {
        int bx = startBarX + i * (barWidth + barGap);
        display.fillRect(bx, startBarY, barWidth, barHeight, EPD_FG());
    }
}

// Tid fra NTP (eller "--:--" / "dd.mm --:--" hvis ikke synket ennå)
static void getTimeString(char* buf, size_t len, bool includeDate) {
    time_t now;
    time(&now);
    struct tm* t = localtime(&now);
    if (t && now > 0) {
        if (includeDate)
            strftime(buf, len, "%d.%m  %H:%M", t);
        else
            strftime(buf, len, "%H:%M", t);
    } else {
        if (includeDate)
            snprintf(buf, len, "--.--  --:--");
        else
            snprintf(buf, len, "--:--");
    }
}

// --- BLE callbacks (unngår GATT 147 ved å ikke kreve pairing) ---
class RoseBoxBLEServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) override {
        bleDeviceConnected = true;
        Serial.println("BLE: Client connected");
    }
    void onDisconnect(BLEServer* pServer) override {
        bleDeviceConnected = false;
        Serial.println("BLE: Client disconnected");
    }
};

// Kun append i callback – tung prosessering i loop() for å unngå StoreProhibited.
class RoseBoxBLERxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string rx = pCharacteristic->getValue();
        if (rx.length() == 0) return;
        size_t len = rx.length();
        if (len > 50) len = 50;  // Små pakker: begrens per mottak
        bleRxBuffer += String(rx.substr(0, len).c_str());
        if (bleRxBuffer.length() > BLE_RX_BUFFER_MAX)
            bleRxBuffer = bleRxBuffer.substring(bleRxBuffer.length() - (BLE_RX_BUFFER_MAX / 2));
        bleDataPending = true;
    }
};

// Prosesser BLE-buffer i hovedloop (ikke i callback) – unngår tunge operasjoner i BLE-kontekst.
static void processBLEFromBuffer() {
    if (!bleDataPending || bleRxBuffer.length() == 0) return;
    bleDataPending = false;
    while (bleRxBuffer.length() > 0) {
        int idxLn = bleRxBuffer.indexOf('\n');
        if (idxLn < 0) idxLn = bleRxBuffer.indexOf('\r');
        int idxCmd = (bleRxBuffer.startsWith("CMD:")) ? bleRxBuffer.indexOf("CMD:", 4) : -1;
        int idx = -1;
        bool useLn = (idxLn >= 0 && (idxCmd < 0 || idxLn <= idxCmd));
        bool useCmd = (idxCmd >= 0 && (idxLn < 0 || idxCmd < idxLn));
        if (useLn) idx = idxLn;
        else if (useCmd) idx = idxCmd;
        if (idx < 0) break;
        String line = bleRxBuffer.substring(0, idx);
        if (useLn) {
            bleRxBuffer = bleRxBuffer.substring(idx + 1);
            if (bleRxBuffer.length() > 0 && (bleRxBuffer[0] == '\n' || bleRxBuffer[0] == '\r'))
                bleRxBuffer = bleRxBuffer.substring(1);
        } else {
            bleRxBuffer = bleRxBuffer.substring(idx);
        }
        line.trim();
        if (line.length() > 0) {
            Serial.println("BLE RX: " + line);
            processBLECommand(line);
        }
    }
    bleRxBuffer.trim();
    if (bleRxBuffer.length() > 0 && bleRxBuffer.startsWith("CMD:")) {
        Serial.println("BLE RX: " + bleRxBuffer);
        processBLECommand(bleRxBuffer);
        bleRxBuffer = "";
    }
    if (bleRxBuffer.length() > BLE_RX_BUFFER_MAX)
        bleRxBuffer = bleRxBuffer.substring(bleRxBuffer.length() - (BLE_RX_BUFFER_MAX / 2));
}

static void initBLE() {
    Serial.println("Initializing BLE...");
    BLEDevice::setMTU(64);  // Små pakker (20–50 byte) reduserer BLE-stack og StoreProhibited
    BLEDevice::init("RoseBox");
    pBLEServer = BLEDevice::createServer();
    if (!pBLEServer) {
        Serial.println("BLE: createServer failed (low memory?)");
        return;
    }
    pBLEServer->setCallbacks(new RoseBoxBLEServerCallbacks());

    BLEService* pSvc = pBLEServer->createService(BLE_SERVICE_UUID);
    if (!pSvc) {
        Serial.println("BLE: createService failed");
        return;
    }
    pBLETxChar = pSvc->createCharacteristic(BLE_CHAR_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
    if (!pBLETxChar) {
        Serial.println("BLE: createCharacteristic TX failed");
        return;
    }
    pBLETxChar->addDescriptor(new BLE2902());
    BLECharacteristic* pRx = pSvc->createCharacteristic(BLE_CHAR_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
    if (pRx) pRx->setCallbacks(new RoseBoxBLERxCallbacks());
    pSvc->start();

    BLEAdvertising* pAdv = BLEDevice::getAdvertising();
    if (pAdv) {
        pAdv->addServiceUUID(BLE_SERVICE_UUID);
        pAdv->setScanResponse(true);
        pAdv->setMinPreferred(0x06);
        pAdv->setMaxPreferred(0x12);
    }
    BLEDevice::startAdvertising();
    Serial.println("BLE: Ready, advertising as 'RoseBox'");
}

static void bleSend(String msg) {
    if (bleDeviceConnected && pBLETxChar != nullptr) {
        pBLETxChar->setValue(msg.c_str());
        pBLETxChar->notify();
        Serial.println("BLE TX: " + msg);
    }
}

static void processBLECommand(String cmd) {
    cmd.trim();
    if (cmd.startsWith("CMD:WIFI:SSID:")) {
        wifiStoredSSID = cmd.substring(14);
        saveWiFiConfig();
        bleSend("OK:SSID saved");
    } else if (cmd.startsWith("CMD:WIFI:PASS:")) {
        wifiStoredPass = cmd.substring(14);
        saveWiFiConfig();
        bleSend("OK:Password saved");
    } else if (cmd == "CMD:WIFI:CONNECT") {
        bleSend("OK:Connecting...");
        if (wifiStoredSSID.length() > 0) {
            WiFi.begin(wifiStoredSSID.c_str(), wifiStoredPass.c_str());
        }
        bleSend("OK:Connect requested");
    } else if (cmd == "CMD:WIFI:STATUS") {
        String s = (WiFi.status() == WL_CONNECTED) ? "Connected to " + WiFi.localIP().toString() : "Not connected";
        bleSend("STATUS:" + s);
    } else if (cmd == "CMD:SYSTEM:INFO") {
        String info = "INFO:RoseBox v1.0|SD:" + String(sdReady ? "Yes" : "No");
        info += "|WiFi:" + String(WiFi.status() == WL_CONNECTED ? "Yes" : "No");
        info += "|BLE:Yes";
        bleSend(info);
    } else if (cmd == "CMD:DISPLAY:PARTIAL:ON") {
        partialRefreshEnabled = true;
        saveDisplaySettings();
        bleSend("OK:Partial ON");
    } else if (cmd == "CMD:DISPLAY:PARTIAL:OFF") {
        partialRefreshEnabled = false;
        saveDisplaySettings();
        bleSend("OK:Partial OFF");
    } else if (cmd == "CMD:DISPLAY:INVERT:ON") {
        displayInverted = true;
        saveDisplaySettings();
        bleSend("OK:Invert ON");
    } else if (cmd == "CMD:DISPLAY:INVERT:OFF") {
        displayInverted = false;
        saveDisplaySettings();
        bleSend("OK:Invert OFF");
    } else if (cmd == "CMD:DISPLAY:STATUS") {
        String s = "DISPLAY:Invert=" + String(displayInverted ? "ON" : "OFF");
        s += "|Partial=" + String(partialRefreshEnabled ? "ON" : "OFF");
        bleSend(s);
    } else if (cmd == "CMD:BUTTON" || cmd == "CMD:BUTTON:SHORT" || cmd == "CMD:BUTTON:PRESS") {
        s_bleInjectedKey = "ENTER";
        bleSend("OK:Button short");
    } else if (cmd == "CMD:BUTTON:LONG") {
        s_bleInjectedKey = "LONG_ENTER";
        bleSend("OK:Button long");
    } else if (cmd == "CMD:BUTTON:LONG:5SEC" || cmd == "CMD:BUTTON:5SEC" || cmd == "CMD:BUTTON:VERYLONG") {
        s_bleInjectedKey = "LONG_ENTER_5SEC";
        bleSend("OK:Button long 5s");
    } else if (cmd == "CMD:BUTTON:HELP") {
        bleSend("BUTTON: CMD:BUTTON|SHORT|PRESS=short, CMD:BUTTON:LONG=long, CMD:BUTTON:LONG:5SEC|5SEC|VERYLONG=5s");
    } else {
        bleSend("ERROR:Unknown command");
    }
}

static void handleBLEConnection() {
    if (!pBLEServer) return;
    yield();
    delay(1);
    if (!bleDeviceConnected && bleOldDeviceConnected) {
        delay(100);
        pBLEServer->startAdvertising();
        Serial.println("BLE: Restarted advertising");
        bleOldDeviceConnected = bleDeviceConnected;
    }
    if (bleDeviceConnected && !bleOldDeviceConnected) {
        bleOldDeviceConnected = bleDeviceConnected;
    }
}

static void applyDisplayWindow(bool incrementPartialCount) {
    if (partialRefreshEnabled) {
        if (incrementPartialCount) {
            partialUpdateCount++;
            if (partialUpdateCount >= 15) {
                display.setFullWindow();
                partialUpdateCount = 0;
            } else {
                display.setPartialWindow(0, 0, display.width(), display.height());
            }
        } else {
            if (partialUpdateCount >= 15) {
                display.setFullWindow();
            } else {
                display.setPartialWindow(0, 0, display.width(), display.height());
            }
        }
    } else {
        display.setFullWindow();
        if (incrementPartialCount) partialUpdateCount = 0;
    }
}

// Ikoner for app-meny (brukes av native screenDrawHomeWithMenu)
static const unsigned char* getAppIcon(const char* name) {
    if (strcmp(name, "terminal") == 0) return epd_bitmap_terminal_icon;
    if (strcmp(name, "clock") == 0) return icon_clock;
    if (strcmp(name, "notes") == 0) return icon_notes;
    if (strcmp(name, "photo") == 0) return icon_Photo;
    if (strcmp(name, "apps") == 0) return icon_apps;
    if (strcmp(name, "settings") == 0 || strcmp(name, "setup") == 0) return icon_settings;
    return icon_apps;  // ukjente apper
}

// --- Display settings (NVS, som RoseOS) ---
static void loadDisplaySettings() {
    preferences.begin("RoseBox", true);
    displayInverted = preferences.getBool("inverted", false);
    refreshCount = preferences.getInt("refresh", 1);
    partialRefreshEnabled = preferences.getBool("partial", true);
    preferences.end();
    // Software-inversjon: EPD_FG()/EPD_BG() bruker displayInverted automatisk
}

static void saveDisplaySettings() {
    preferences.begin("RoseBox", false);
    preferences.putBool("inverted", displayInverted);
    preferences.putInt("refresh", refreshCount);
    preferences.putBool("partial", partialRefreshEnabled);
    preferences.end();
}

static void loadWiFiConfig() {
    preferences.begin("RoseBox", true);
    wifiStoredSSID = preferences.getString("wifi_ssid", "");
    wifiStoredPass = preferences.getString("wifi_pass", "");
    preferences.end();
}

static void saveWiFiConfig() {
    preferences.begin("RoseBox", false);
    preferences.putString("wifi_ssid", wifiStoredSSID);
    preferences.putString("wifi_pass", wifiStoredPass);
    preferences.end();
}

// ========== Native C++ HAL (for hardkodede apper, uten Lua) ==========
#define FILE_LIST_MAX 24
static char fileListNames[FILE_LIST_MAX][32];
static int fileListCount;

// Tastatur: returnerer tom streng ved ingen trykk, ellers "ENTER", "LONG_ENTER", "LONG_ENTER_5SEC"
// BLE kan injisere et trykk via CMD:BUTTON:SHORT / LONG / LONG:5SEC
static uint8_t s_lastBtn = HIGH;
static uint32_t s_pressStart = 0;
static String keyboardGetKey() {
    if (s_bleInjectedKey.length() > 0) {
        String k = s_bleInjectedKey;
        s_bleInjectedKey = "";
        return k;
    }
    int btn = digitalRead(39);
    if (btn == LOW && s_lastBtn == HIGH) {
        s_pressStart = millis();
    }
    if (btn == HIGH && s_lastBtn == LOW) {
        s_lastBtn = HIGH;
        uint32_t duration = millis() - s_pressStart;
        if (duration >= 5000) return String("LONG_ENTER_5SEC");
        if (duration >= 800)  return String("LONG_ENTER");
        return String("ENTER");
    }
    s_lastBtn = btn;
    return String("");
}

static void screenClear() {
    applyDisplayWindow(true);
    display.fillScreen(EPD_BG());
}
static void screenDrawText(int x, int y, const char* text) {
    display.setCursor(x, y);
    display.print(text);
}
static void screenSetTextColor(bool white) {
    display.setTextColor(white ? EPD_BG() : EPD_FG());
}
static void screenUpdate() {
    applyDisplayWindow(false);
    display.display(partialRefreshEnabled);
}
static void screenDrawLine(int x0, int y0, int x1, int y1) {
    display.drawLine(x0, y0, x1, y1, EPD_FG());
}
static void screenFillRect(int x, int y, int w, int h) {
    display.fillRect(x, y, w, h, EPD_FG());
}
static void screenDrawRect(int x, int y, int w, int h) {
    display.drawRect(x, y, w, h, EPD_FG());
}
static void screenSetTextSize(int s) { display.setTextSize(s); }
static int screenGetWidth() { return display.width(); }
static int screenGetHeight() { return display.height(); }
static int screenGetTextWidth(const char* text) {
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    return (int)w;
}

// Hjem med app-meny (1-basert selected)
static void screenDrawHomeWithMenu(const char* names[], int n, int selected) {
    const int gridSlots = 5;
    const int iconSize = 32;
    const int gridCols = 5;
    const int cellW = 250 / gridCols;
    const int gridY = 38;
    const int cellH = 52;
    const int iconXOffset = (cellW - iconSize) / 2;
    const int iconYOffset = 4;

    applyDisplayWindow(true);
    display.firstPage();
    do {
        yield();
        display.fillScreen(EPD_BG());
        display.drawBitmap(0, 0, epd_bitmap_home_menu, 250, 122, EPD_FG());
        drawBatteryIcon(250 - 25, 5);
        display.setTextSize(1);
        display.setTextColor(EPD_FG());
        // Footer: klokke nederst til høyre (størrelse 2 = 100% større)
        display.setTextSize(2);
        char timeStr[16];
        getTimeString(timeStr, sizeof(timeStr), false);
        int tw = screenGetTextWidth(timeStr);
        display.setCursor(display.width() - tw - 4, display.height() - 14);
        display.print(timeStr);
        display.setTextSize(1);

        for (int i = 0; i < gridSlots; i++) {
            int cellX = i * cellW;
            int cellY = gridY;
            bool selectedCell = (i + 1 == selected);

            if (selectedCell) {
                display.fillRoundRect(cellX + 1, cellY + 1, cellW - 2, cellH - 2, 3, EPD_FG());
            } else {
                display.drawRoundRect(cellX + 1, cellY + 1, cellW - 2, cellH - 2, 3, EPD_FG());
            }

            if (i < n) {
                int iconX = cellX + iconXOffset;
                int iconY = cellY + iconYOffset;
                const unsigned char* icon = getAppIcon(names[i]);
                if (selectedCell) {
                    display.drawBitmap(iconX, iconY, icon, iconSize, iconSize, EPD_BG());
                } else {
                    display.drawBitmap(iconX, iconY, icon, iconSize, iconSize, EPD_FG());
                }
                int16_t tx1, ty1;
                uint16_t tw, th;
                display.getTextBounds(names[i], 0, 0, &tx1, &ty1, &tw, &th);
                int textX = cellX + (cellW - (int)tw) / 2;
                int textY = cellY + iconSize + iconYOffset + 2;
                display.setCursor(textX, textY);
                if (selectedCell) display.setTextColor(EPD_BG());
                display.print(names[i]);
                if (selectedCell) display.setTextColor(EPD_FG());
            }
        }
    } while (display.nextPage());
}

static bool displayGetInverted() { return displayInverted; }
static void displaySetInverted(bool v) { displayInverted = v; }
static int displayGetRefreshCount() { return refreshCount; }
static void displaySetRefreshCount(int v) { if (v >= 1 && v <= 3) refreshCount = v; }
static bool displayGetPartial() { return partialRefreshEnabled; }
static void displaySetPartial(bool v) { partialRefreshEnabled = v; }
static void displaySaveSettingsCpp() { saveDisplaySettings(); }

static bool tcpConnectCpp(const char* host, int port) {
    if (tcpClient.connected()) tcpClient.stop();
    return tcpClient.connect(host, (uint16_t)port, 5000);
}
static void tcpSendCpp(const char* str) { if (tcpClient.connected()) tcpClient.print(str); }
static int tcpAvailableCpp() { return tcpClient.available(); }
static String tcpReadCpp(int maxLen) {
    String s;
    while (maxLen-- > 0 && tcpClient.available()) s += (char)tcpClient.read();
    return s;
}
static bool tcpConnectedCpp() { return tcpClient.connected(); }
static void tcpStopCpp() { tcpClient.stop(); }

static int fileListFlashCpp(const char* path) {
    String p = (path[0] == '/') ? String(path) : String("/") + path;
    fileListCount = 0;
    File root = LittleFS.open(p.c_str());
    if (!root || !root.isDirectory()) { root.close(); return 0; }
    File file = root.openNextFile();
    while (file && fileListCount < FILE_LIST_MAX) {
        String n = file.name();
        int lastSlash = n.lastIndexOf('/');
        String base = (lastSlash >= 0) ? n.substring(lastSlash + 1) : n;
        if (base.endsWith(".lua") || base.endsWith(".luac")) {
            String name = base;
            if (name.endsWith(".luac")) name = name.substring(0, name.length() - 5);
            else if (name.endsWith(".lua")) name = name.substring(0, name.length() - 4);
            name.toCharArray(fileListNames[fileListCount], 32);
            fileListCount++;
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return fileListCount;
}
static int fileListSDCpp(const char* path) {
    fileListCount = 0;
    if (!sdReady) return 0;
    String p = (path[0] == '/') ? String(path) : String("/") + path;
    File root = SD.open(p.c_str());
    if (!root || !root.isDirectory()) { root.close(); return 0; }
    File file = root.openNextFile();
    while (file && fileListCount < FILE_LIST_MAX) {
        String n = file.name();
        file.close();
        int lastSlash = n.lastIndexOf('/');
        String base = (lastSlash >= 0) ? n.substring(lastSlash + 1) : n;
        if (base.endsWith(".lua") || base.endsWith(".luac")) {
            String name = base;
            if (name.endsWith(".luac")) name = name.substring(0, name.length() - 5);
            else if (name.endsWith(".lua")) name = name.substring(0, name.length() - 4);
            name.toCharArray(fileListNames[fileListCount], 32);
            fileListCount++;
        }
        file = root.openNextFile();
    }
    root.close();
    return fileListCount;
}

// ========== Hardkodede apper (setup/loop) ==========
static const char* APP_NAMES[] = { "terminal", "clock", "settings", "apps" };
static const int APP_NAMES_COUNT = 4;
static int homeSelectedIndex = 1;
static uint32_t lastHomeClockUpdate = 0;  // for oppdatering av footer-klokke hvert minutt
static uint32_t homeOpenedAt = 0;  // ignorer LONG_ENTER kort tid etter tilbake til hjem (unngår dobbel åpning)

static void appHome_setup() {
    homeSelectedIndex = 1;
    lastHomeClockUpdate = millis();
    homeOpenedAt = millis();  // nytt hjem – ignorer knapp i 500 ms
    screenDrawHomeWithMenu(APP_NAMES, APP_NAMES_COUNT, homeSelectedIndex);
}
static void appHome_loop() {
    // Oppdater footer-klokke hvert minutt (partial refresh når partial er på)
    if (millis() - lastHomeClockUpdate >= 60000) {
        lastHomeClockUpdate = millis();
        screenDrawHomeWithMenu(APP_NAMES, APP_NAMES_COUNT, homeSelectedIndex);
    }
    String key = keyboardGetKey();
    if (key.length() == 0) return;
    // Etter exit fra app: samme langtrykk kan registreres på hjem – ignorer kort tid
    if (key == "LONG_ENTER" && (millis() - homeOpenedAt < 500))
        return;
    if (key == "LONG_ENTER") {
        int idx = homeSelectedIndex - 1;  // 0-based app index
        if (idx >= 0 && idx < APP_NAMES_COUNT)
            launchApp(idx + 1);  // apps[1]=terminal, etc.
        return;
    }
    homeSelectedIndex = (homeSelectedIndex % APP_NAMES_COUNT) + 1;
    lastHomeClockUpdate = millis();  // reset så vi ikke tegner på nytt med én gang
    screenDrawHomeWithMenu(APP_NAMES, APP_NAMES_COUNT, homeSelectedIndex);
}

static void appClock_setup() {
    screenClear();
    screenDrawText(10, 10, "Clock");
    char timeStr[32];
    getTimeString(timeStr, sizeof(timeStr), true);
    screenSetTextSize(2);
    screenDrawText(10, 35, timeStr);
    screenSetTextSize(1);
    screenDrawText(10, 95, "Lang trykk = tilbake");
    screenUpdate();
}
static void appClock_loop() {
    String key = keyboardGetKey();
    if (key.length() == 0) return;
    if (key == "LONG_ENTER_5SEC" || key == "LONG_ENTER") {
        launchApp(0);
        return;
    }
    screenClear();
    screenDrawText(10, 10, "Clock");
    char timeStr[32];
    getTimeString(timeStr, sizeof(timeStr), true);
    screenSetTextSize(2);
    screenDrawText(10, 35, timeStr);
    screenSetTextSize(1);
    screenDrawText(10, 95, "Lang trykk = tilbake");
    screenUpdate();
}

#define TERMINAL_MAX_LINES 10
#define TERMINAL_LINE_LEN  64
static char terminalLines[TERMINAL_MAX_LINES][TERMINAL_LINE_LEN];
static int terminalLineCount;
static int terminalCmdIndex;
static bool terminalRemoteMode;
static const char* terminalCommands[] = { "info", "wifi", "clear", "connect 192.168.1.1", "disconnect", "help", "exit" };
static const int terminalCommandsCount = 7;

static void terminalAddOutput(const char* line) {
    if (terminalLineCount >= TERMINAL_MAX_LINES) {
        for (int i = 0; i < TERMINAL_MAX_LINES - 1; i++)
            strncpy(terminalLines[i], terminalLines[i + 1], TERMINAL_LINE_LEN - 1);
        terminalLineCount = TERMINAL_MAX_LINES - 1;
    }
    strncpy(terminalLines[terminalLineCount], line, TERMINAL_LINE_LEN - 1);
    terminalLines[terminalLineCount][TERMINAL_LINE_LEN - 1] = '\0';
    terminalLineCount++;
}
static void terminalRedraw() {
    screenClear();
    int y = 0;
    const int LINE_H = 10;
    for (int i = 0; i < terminalLineCount; i++) {
        screenDrawText(0, y, terminalLines[i]);
        y += LINE_H;
    }
    const char* prompt = terminalRemoteMode ? "[TCP] " : "> ";
    String curCmd = String(prompt) + String(terminalCommands[terminalCmdIndex]);
    screenDrawText(0, y, curCmd.c_str());
    screenUpdate();
}
static void terminalRunInfo() {
    terminalAddOutput("-- System --");
    char buf[32];
    snprintf(buf, sizeof(buf), "Heap: %u bytes", (unsigned)ESP.getFreeHeap());
    terminalAddOutput(buf);
    snprintf(buf, sizeof(buf), "Uptime: %lu s", (unsigned long)(millis() / 1000));
    terminalAddOutput(buf);
    terminalAddOutput("");
}
static void terminalRunWifi() {
    terminalAddOutput("-- WiFi --");
    if (WiFi.status() == WL_CONNECTED) {
        terminalAddOutput("Status: CONNECTED");
        terminalAddOutput(("IP: " + WiFi.localIP().toString()).c_str());
    } else {
        terminalAddOutput("Status: DISCONNECTED");
        terminalAddOutput("IP: (ikke koblet)");
    }
    terminalAddOutput("");
}
static void terminalRunHelp() {
    terminalAddOutput("-- Kommandoer --");
    terminalAddOutput("Kort trykk = bytt, Lang = kjør");
    terminalAddOutput("info   = systeminfo");
    terminalAddOutput("wifi   = WiFi status/IP");
    terminalAddOutput("clear  = tøm skjerm");
    terminalAddOutput("connect IP [port] = TCP (telnet)");
    terminalAddOutput("disconnect = frakoble TCP");
    terminalAddOutput("help   = denne hjelpen");
    terminalAddOutput("exit   = lukk terminal");
    terminalAddOutput("");
}
static void terminalRunCommand(const char* cmd) {
    if (!cmd || !*cmd) return;
    if (strcmp(cmd, "info") == 0) { terminalRunInfo(); return; }
    if (strcmp(cmd, "wifi") == 0) { terminalRunWifi(); return; }
    if (strcmp(cmd, "clear") == 0) { terminalLineCount = 0; return; }
    if (strcmp(cmd, "help") == 0) { terminalRunHelp(); return; }
    if (strcmp(cmd, "exit") == 0) { launchApp(0); return; }
    if (strncmp(cmd, "connect ", 8) == 0) {
        const char* rest = cmd + 8;
        char ip[32] = "192.168.1.1";
        int port = 23;
        int a, b, c, d;
        if (sscanf(rest, "%d.%d.%d.%d", &a, &b, &c, &d) >= 4) {
            snprintf(ip, sizeof(ip), "%d.%d.%d.%d", a, b, c, d);
            const char* p = rest;
            while (*p && *p != ' ') p++;
            while (*p == ' ') p++;
            if (*p) port = atoi(p);
        }
        terminalAddOutput(("Kobler til " + String(ip) + ":" + String(port) + "...").c_str());
        terminalRedraw();
        if (tcpConnectCpp(ip, port)) {
            terminalRemoteMode = true;
            terminalAddOutput("Koblet. Lang trykk = disconnect.");
        } else {
            terminalAddOutput("Kunne ikke koble til.");
        }
        return;
    }
    if (strcmp(cmd, "disconnect") == 0) {
        tcpStopCpp();
        terminalRemoteMode = false;
        terminalAddOutput("Frakoblet.");
        return;
    }
    terminalAddOutput(("Ukjent: " + String(cmd) + " (prøv help)").c_str());
}
static void appTerminal_setup() {
    terminalLineCount = 0;
    terminalCmdIndex = 0;
    terminalRemoteMode = false;
    terminalAddOutput("RoseBox Terminal v1.0");
    terminalAddOutput("Kort=bytt kommando, Lang=kjør");
    terminalRunHelp();
    terminalRedraw();
}
static void appTerminal_loop() {
    if (terminalRemoteMode && !tcpConnectedCpp()) {
        terminalRemoteMode = false;
        terminalAddOutput("Frakoblet (server lukket).");
        terminalRedraw();
        return;
    }
    if (terminalRemoteMode && tcpConnectedCpp()) {
        bool hadData = false;
        while (tcpAvailableCpp() > 0) {
            String s = tcpReadCpp(64);
            if (s.length() > 0) {
                s.replace("\r", "");
                terminalAddOutput(s.c_str());
                hadData = true;
            }
        }
        if (hadData) terminalRedraw();
    }

    String key = keyboardGetKey();
    if (key.length() == 0) return;

    if (key == "LONG_ENTER_5SEC") { launchApp(0); return; }

    if (terminalRemoteMode) {
        if (key == "LONG_ENTER") {
            tcpStopCpp();
            terminalRemoteMode = false;
            terminalAddOutput("Frakoblet.");
        } else {
            tcpSendCpp("\n");
        }
        terminalRedraw();
        return;
    }

    if (key == "LONG_ENTER") {
        const char* cmd = terminalCommands[terminalCmdIndex];
        terminalRunCommand(cmd);
    } else {
        terminalCmdIndex = (terminalCmdIndex + 1) % terminalCommandsCount;
    }
    terminalRedraw();
}

static int settingsSelectedIndex = 0;
static const int SETTINGS_LINE_H = 12;
static const int SETTINGS_ROW_YS[] = { 6, 18, 30, 76 };

static void settingsDrawRow(int y, const char* text, bool selected) {
    if (selected) {
        screenFillRect(0, y - 2, screenGetWidth(), SETTINGS_LINE_H + 2);
        screenSetTextColor(true);
        screenDrawText(5, y, text);
        screenSetTextColor(false);
    } else {
        screenDrawText(5, y, text);
    }
}
static void settingsRedraw() {
    screenClear();
    int y = 6;
    char buf[48];
    bool inv = displayGetInverted();
    snprintf(buf, sizeof(buf), "Inverter: %s", inv ? "ON" : "OFF");
    settingsDrawRow(y, buf, settingsSelectedIndex == 0);
    y += SETTINGS_LINE_H;
    snprintf(buf, sizeof(buf), "Refresh (1-3): %d", displayGetRefreshCount());
    settingsDrawRow(y, buf, settingsSelectedIndex == 1);
    y += SETTINGS_LINE_H;
    snprintf(buf, sizeof(buf), "Partial: %s", displayGetPartial() ? "ON" : "OFF");
    settingsDrawRow(y, buf, settingsSelectedIndex == 2);
    y += SETTINGS_LINE_H + 4;
    screenDrawLine(0, y, screenGetWidth(), y);
    y += 6;
    String ipStr = (WiFi.status() == WL_CONNECTED) ? ("IP: " + WiFi.localIP().toString()) : "IP: (ikke koblet)";
    screenDrawText(5, y, ipStr.c_str());
    y += SETTINGS_LINE_H + 6;
    screenDrawLine(0, y, screenGetWidth(), y);
    y += 6;
    settingsDrawRow(y, ">>> Lagre og lukk <<<", settingsSelectedIndex == 3);
    y += SETTINGS_LINE_H + 4;
    screenDrawText(5, y, "Lang >5s=lagre+hjem");
    screenUpdate();
}
static void appSettings_setup() {
    settingsSelectedIndex = 0;
    settingsRedraw();
}
static void appSettings_loop() {
    String key = keyboardGetKey();
    if (key.length() == 0) return;

    if (key == "LONG_ENTER_5SEC") {
        displaySaveSettingsCpp();
        launchApp(0);
        return;
    }
    if (key == "LONG_ENTER") {
        if (settingsSelectedIndex == 3) {
            displaySaveSettingsCpp();
            launchApp(0);
            return;
        }
        if (settingsSelectedIndex == 0) displaySetInverted(!displayGetInverted());
        if (settingsSelectedIndex == 1) displaySetRefreshCount((displayGetRefreshCount() % 3) + 1);
        if (settingsSelectedIndex == 2) displaySetPartial(!displayGetPartial());
        settingsRedraw();
        return;
    }
    settingsSelectedIndex = (settingsSelectedIndex + 1) % 4;
    settingsRedraw();
}

static int appsSelectedSide = 1;   // 1=Intern, 2=SD
static int appsSelectedIndex = 1;
static int appsInternCount = 0;    // bruker APP_NAMES som "Intern"
static int appsSDCount = 0;
#define APPS_DIVIDER_X 125

static void appApps_setup() {
    appsInternCount = APP_NAMES_COUNT;
    appsSDCount = fileListSDCpp("/apps");
    appsSelectedSide = 1;
    appsSelectedIndex = 1;
    if (appsInternCount == 0 && appsSDCount > 0) appsSelectedSide = 2;

    screenClear();
    screenDrawLine(APPS_DIVIDER_X, 0, APPS_DIVIDER_X, screenGetHeight());
    screenDrawText(5, 2, "Intern");
    screenDrawText(APPS_DIVIDER_X + 5, 2, "SD");
    screenDrawLine(0, 14, screenGetWidth(), 14);
    int yLeft = 18, yRight = 18;
    for (int i = 0; i < appsInternCount; i++) {
        char txt[40];
        snprintf(txt, sizeof(txt), "%s %s", (appsSelectedSide == 1 && appsSelectedIndex == i + 1) ? ">" : " ", APP_NAMES[i]);
        screenDrawText(5, yLeft, txt);
        yLeft += 12;
    }
    if (appsInternCount == 0) screenDrawText(5, yLeft, "  (ingen)");
    for (int i = 0; i < appsSDCount && i < FILE_LIST_MAX; i++) {
        char txt[40];
        snprintf(txt, sizeof(txt), "%s %s", (appsSelectedSide == 2 && appsSelectedIndex == i + 1) ? ">" : " ", fileListNames[i]);
        screenDrawText(APPS_DIVIDER_X + 5, yRight, txt);
        yRight += 12;
    }
    if (appsSDCount == 0) screenDrawText(APPS_DIVIDER_X + 5, yRight, "  (ingen)");
    screenDrawText(5, screenGetHeight() - 12, "Kort=velg  Lang=apne");
    screenUpdate();
}
static void appApps_loop() {
    String key = keyboardGetKey();
    if (key.length() == 0) return;

    if (key == "LONG_ENTER_5SEC") { launchApp(0); return; }

    if (key == "LONG_ENTER") {
        if (appsSelectedSide == 1 && appsSelectedIndex >= 1 && appsSelectedIndex <= appsInternCount) {
            launchApp(appsSelectedIndex);  // 1-based -> terminal=1, clock=2, ...
            return;
        }
        if (appsSelectedSide == 2) {
            // SD-lua ikke støttet i native-modus
            return;
        }
        return;
    }

    if (appsSelectedSide == 1) {
        if (appsSelectedIndex < appsInternCount) appsSelectedIndex++;
        else {
            if (appsSDCount > 0) { appsSelectedSide = 2; appsSelectedIndex = 1; }
            else appsSelectedIndex = 1;
        }
    } else {
        if (appsSelectedIndex < appsSDCount) appsSelectedIndex++;
        else { appsSelectedSide = 1; appsSelectedIndex = 1; }
    }

    screenClear();
    screenDrawLine(APPS_DIVIDER_X, 0, APPS_DIVIDER_X, screenGetHeight());
    screenDrawText(5, 2, "Intern");
    screenDrawText(APPS_DIVIDER_X + 5, 2, "SD");
    screenDrawLine(0, 14, screenGetWidth(), 14);
    int yLeft = 18, yRight = 18;
    for (int i = 0; i < appsInternCount; i++) {
        char txt[40];
        snprintf(txt, sizeof(txt), "%s %s", (appsSelectedSide == 1 && appsSelectedIndex == i + 1) ? ">" : " ", APP_NAMES[i]);
        screenDrawText(5, yLeft, txt);
        yLeft += 12;
    }
    if (appsInternCount == 0) screenDrawText(5, yLeft, "  (ingen)");
    for (int i = 0; i < appsSDCount && i < FILE_LIST_MAX; i++) {
        char txt[40];
        snprintf(txt, sizeof(txt), "%s %s", (appsSelectedSide == 2 && appsSelectedIndex == i + 1) ? ">" : " ", fileListNames[i]);
        screenDrawText(APPS_DIVIDER_X + 5, yRight, txt);
        yRight += 12;
    }
    if (appsSDCount == 0) screenDrawText(APPS_DIVIDER_X + 5, yRight, "  (ingen)");
    screenDrawText(5, screenGetHeight() - 12, "Kort=velg  Lang=apne");
    screenUpdate();
}

// App-array og launchApp (definert her; deklareret i App.h)
App apps[APP_COUNT] = {
    { appHome_setup,    appHome_loop,    "Home" },
    { appTerminal_setup, appTerminal_loop, "Terminal" },
    { appClock_setup,   appClock_loop,   "Clock" },
    { appSettings_setup, appSettings_loop, "Settings" },
    { appApps_setup,    appApps_loop,    "Apps" },
};
App* currentApp = nullptr;

void launchApp(int index) {
    if (currentApp != nullptr) {
        // Valgfri cleanup: ingen dynamisk alloc som må frigjøres
    }
    if (index < 0 || index >= APP_COUNT) index = 0;
    currentApp = &apps[index];
    currentApp->setup();
}

// (Lua fjernet – native apper bruker C++ HAL)

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- RoseBox Boot (native apps) ---");

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);

  littlefsReady = LittleFS.begin(false);
  if (littlefsReady) Serial.println("LittleFS: mounted");
  else Serial.println("LittleFS: mount failed");
  spiffsReady = SPIFFS.begin(false);
  if (spiffsReady) Serial.println("SPIFFS mounted");

  sdReady = SD.begin(SD_CS);
  if (!sdReady) Serial.println("SD card fail (CS 13)");

  display.init(115200, true, 2, false);
  display.setRotation(1);
  loadDisplaySettings();
  loadWiFiConfig();
  // Auto-connect til lagret WiFi ved oppstart (SSID/pass fra NVS)
  if (wifiStoredSSID.length() > 0) {
    WiFi.begin(wifiStoredSSID.c_str(), wifiStoredPass.c_str());
    Serial.println("WiFi: connecting to " + wifiStoredSSID + " ...");
  }
  initBLE();
  pinMode(39, INPUT);
  display.setTextColor(EPD_FG());

  // Boot-animasjon: logo + prikker
  const int screenW = display.width();
  const int screenH = display.height();
  const int bootW = 80, bootH = 50;
  const int bootX = (screenW - bootW) / 2;
  const int bootY = (screenH - bootH) / 2 - 8;
  const int dotY = bootY + bootH + 8;
  const int dotSpacing = 16;
  const int dotRadius = 3;
  const int dotLeftX = bootX + (bootW - (2 * dotSpacing)) / 2;

  for (int frame = 0; frame < 6; frame++) {
    int numDots = (frame % 3) + 1;
    if (frame == 0) display.setFullWindow();
    else display.setPartialWindow(0, 0, screenW, screenH);
    display.firstPage();
    do {
      yield();
      display.fillRect(0, 0, screenW, screenH, EPD_BG());
      display.drawBitmap(bootX, bootY, epd_bitmap_boot_logo, bootW, bootH, EPD_FG());
      for (int i = 0; i < 3; i++) {
        int cx = dotLeftX + i * dotSpacing;
        if (i < numDots) display.fillCircle(cx, dotY, dotRadius, EPD_FG());
        else display.drawCircle(cx, dotY, dotRadius, EPD_FG());
      }
    } while (display.nextPage());
    delay(150);
  }

  launchApp(0);  // Start hjemskjerm
  Serial.println("RoseBox: native app launcher ready");
}

void loop() {
  handleBLEConnection();
  if (bleDataPending) processBLEFromBuffer();
  // NTP: sett tid én gang når WiFi er koblet (Norge CET+ sommer)
  if (WiFi.status() == WL_CONNECTED && !ntpConfigDone) {
    configTime(3600, 3600, "pool.ntp.org", "time.nis.gov");
    ntpConfigDone = true;
    Serial.println("NTP: config sent, time will sync shortly");
  }
  if (currentApp != nullptr) {
    currentApp->loop();
  }
  delay(10);
}
