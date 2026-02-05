/*
 * RoseOS for ESP32 LilyGo T5 E-Paper Display
 * 
 * A lightweight operating system with:
 * - Lua-based apps on SD card
 * - BLE communication via Web Bluetooth
 * - WiFi configuration
 * - Graceful SD card handling
 * 
 * Hardware: LilyGo TTGO T5 v2.3.1 2.13" E-Paper
 */

#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <GxEPD2_BW.h>
#include <WebServer.h>
#include "icons.h"
#include "home.h"

// ============================================================
// WEB CONTROLLER HTML
// ============================================================
const char* html_controller PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="no">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
    <title>RoseOS Controller</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        :root {
            --bg-primary: #0f0f1a;
            --bg-secondary: #1a1a2e;
            --bg-card: rgba(255, 255, 255, 0.05);
            --accent: #fd79a8;
            --text-primary: #ffffff;
            --text-secondary: #a0a0b0;
            --border: rgba(255, 255, 255, 0.1);
        }
        body {
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            background: linear-gradient(135deg, var(--bg-primary) 0%, var(--bg-secondary) 100%);
            min-height: 100vh;
            color: var(--text-primary);
            padding: 20px;
            display: flex;
            flex-direction: column;
            align-items: center;
        }
        .container { width: 100%; max-width: 400px; }
        
        header { text-align: center; margin-bottom: 30px; }
        h1 { 
            font-size: 32px; 
            margin-bottom: 5px;
            background: linear-gradient(45deg, #fd79a8, #a29bfe);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .status { font-size: 14px; color: var(--text-secondary); display: flex; align-items: center; justify-content: center; gap: 8px; }
        .dot { width: 8px; height: 8px; border-radius: 50%; background: #00b894; box-shadow: 0 0 10px #00b894; }
        
        .card {
            background: var(--bg-card);
            backdrop-filter: blur(10px);
            border: 1px solid var(--border);
            border-radius: 20px;
            padding: 25px;
            margin-bottom: 20px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
        }
        
        .title { font-size: 12px; text-transform: uppercase; letter-spacing: 1px; color: var(--text-secondary); margin-bottom: 15px; font-weight: 700; }
        
        .nav-grid {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 15px;
        }
        
        .btn {
            background: rgba(255,255,255,0.08);
            border: 1px solid rgba(255,255,255,0.05);
            color: white;
            padding: 20px;
            border-radius: 15px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            display: flex;
            flex-direction: column;
            align-items: center;
            justify-content: center;
            gap: 10px;
            -webkit-tap-highlight-color: transparent;
        }
        
        .btn:active { transform: scale(0.95); background: rgba(255,255,255,0.15); }
        .btn i { font-size: 24px; margin-bottom: 5px; }
        .btn-primary { background: var(--accent); color: #000; border: none; box-shadow: 0 5px 15px rgba(253, 121, 168, 0.3); }
        .btn-full { grid-column: span 2; }
        
        .input-group { position: relative; }
        input {
            width: 100%;
            padding: 15px 50px 15px 15px;
            background: rgba(0,0,0,0.3);
            border: 1px solid var(--border);
            border-radius: 12px;
            color: white;
            font-size: 16px;
            outline: none;
        }
        .send-btn {
            position: absolute;
            right: 5px;
            top: 5px;
            bottom: 5px;
            width: 40px;
            background: var(--accent);
            border: none;
            border-radius: 8px;
            color: #000;
            font-size: 18px;
            display: flex;
            align-items: center;
            justify-content: center;
            cursor: pointer;
        }
        
        .quick-actions {
            display: flex;
            gap: 10px;
            overflow-x: auto;
            padding-bottom: 5px;
            margin-top: 15px;
        }
        .chip {
            background: rgba(255,255,255,0.1);
            padding: 8px 16px;
            border-radius: 20px;
            font-size: 13px;
            white-space: nowrap;
            cursor: pointer;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>RoseOS</h1>
            <div class="status">
                <div class="dot"></div> <span id="ip-display">Connecting...</span>
            </div>
        </header>

        <div class="card">
            <div class="title">Navigation</div>
            <div class="nav-grid">
                <button class="btn" onclick="cmd('CMD:BUTTON')">
                    <span>👆</span> Short Press
                </button>
                <button class="btn" onclick="cmd('CMD:BUTTON:LONG')">
                    <span>👇</span> Long Press
                </button>
                <button class="btn btn-full" onclick="cmd('CMD:APP:EXIT')">
                    <span>🚪</span> Exit App
                </button>
            </div>
        </div>

        <div class="card">
            <div class="title">Send Note / Text</div>
            <div class="input-group">
                <input type="text" id="textInput" placeholder="Type message..." onkeypress="handleEnter(event)">
                <button class="send-btn" onclick="sendText()">➤</button>
            </div>
            <div class="quick-actions">
                <div class="chip" onclick="setInput('Hello World')">Hello World</div>
                <div class="chip" onclick="setInput('Reminder: ')">Reminder</div>
                <div class="chip" onclick="setInput('Meeting at ')">Meeting</div>
            </div>
        </div>
        
        <div class="card">
             <div class="title">System</div>
             <div class="nav-grid">
                 <button class="btn" onclick="cmd('CMD:SYSTEM:INFO')">ℹ️ Info</button>
                 <button class="btn" onclick="cmd('CMD:SD:REFRESH')">🔄 Refresh Apps</button>
             </div>
        </div>
    </div>

    <script>
        // Get IP from current location
        const deviceIP = window.location.hostname;
        document.getElementById('ip-display').innerText = deviceIP;

        function cmd(command) {
            fetch('/api/cmd?q=' + encodeURIComponent(command))
                .catch(err => console.error('Error:', err));
        }

        function sendText() {
            const input = document.getElementById('textInput');
            if (input.value.trim()) {
                cmd('CMD:TEXT:' + input.value);
                input.value = '';
            }
        }
        
        function setInput(text) {
            document.getElementById('textInput').value = text;
            document.getElementById('textInput').focus();
        }

        function handleEnter(e) {
            if (e.key === 'Enter') sendText();
        }
    </script>
</body>
</html>
)rawliteral";

// ============================================================
// HARDWARE CONFIGURATION - LilyGo T5 v2.3.1
// ============================================================

// E-Paper Display Pins
#define EPD_MOSI  23
#define EPD_MISO  -1
#define EPD_SCK   18
#define EPD_CS    5
#define EPD_DC    17
#define EPD_RST   16
#define EPD_BUSY  4

// SD Card Pins (LilyGo T5)
#define SD_CS     13
#define SD_MOSI   15
#define SD_MISO   2
#define SD_SCK    14

// Button Pin
#define BUTTON_PIN 39

// Battery voltage pin (ADC with voltage divider on LilyGo T5)
#define BATTERY_PIN 35

// Display dimensions
#define DISPLAY_WIDTH  250
#define DISPLAY_HEIGHT 122

// ============================================================
// DISPLAY DRIVER
// ============================================================

GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
    GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ============================================================
// WEB SERVER CONFIGURATION
// ============================================================
WebServer server(80);

// ============================================================
// FORWARD DECLARATIONS
// ============================================================
void handleAppInput(String input);
void runAppsApp();
void runSettingsApp();
void saveDisplaySettings();
void loadDisplaySettings();
void saveWiFiConfig();
void connectWiFi();
void initWebServer();
void handleWireless();
bool initSDCard();
void loadAppsFromSD();
void drawAppMenu();
void launchApp(String appName);
void executeApp(String appName);
void exitApp();
void runClockApp();
void runNotesApp();
void runPhotoApp();
void runGenericApp(String name, String script);
void runSysInfoApp();
void refreshDisplay();
void handleButtonPress(bool longPress);
void checkButton();
void bleSend(String message);
void processBLECommand(String cmd);
String readAppScript(String appName);
void drawAppScreen(String title, String content);
int getBatteryPercent();
void drawBatteryIcon(int x, int y);
void startDisplayUpdate();
void endDisplayUpdate();
void initDisplay();
void initBLE();



// ============================================================
// BLE CONFIGURATION
// ============================================================

#define SERVICE_UUID        "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_RX "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHARACTERISTIC_UUID_TX "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

BLEServer* pServer = nullptr;
BLECharacteristic* pTxCharacteristic = nullptr;
bool deviceConnected = false;
bool oldDeviceConnected = false;
String bleReceiveBuffer = "";

// ============================================================
// SYSTEM STATE
// ============================================================

Preferences preferences;

// SD Card state
bool sdCardPresent = false;
SPIClass sdSPI(HSPI);

// WiFi state
String wifiSSID = "";
String wifiPassword = "";
bool wifiConnected = false;

// App management
#define MAX_APPS 20
String appList[MAX_APPS];
int appCount = 0;
int selectedAppIndex = 0;
bool inAppMenu = true;
String currentApp = "";

// Button handling
unsigned long lastButtonPress = 0;
const unsigned long debounceDelay = 300;
const unsigned long longPressTime = 1000;
unsigned long buttonPressStart = 0;
bool buttonPressed = false;
bool lastButtonState = HIGH;
long simulatedPressDuration = 0; // 0 = none, >0 = simulated press in ms

// Lua script state
String currentScript = "";
bool appRunning = false;

// Display settings (saved to NVS)
bool displayInverted = false;       // Invert black/white
int refreshCount = 1;               // Number of refreshes per update (1-3)
bool partialRefreshEnabled = true;  // Use partial refresh (faster, may ghost)
unsigned long lastClockUpdate = 0;  // For periodic home screen refresh

// Norwegian day names
const char* dayNamesNO[] = {"Søn", "Man", "Tir", "Ons", "Tor", "Fre", "Lør"};

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void processBLECommand(String cmd);
void handleButtonPress(bool longPress);
void checkButton();
void handleBLEConnection();
void refreshDisplay();
void launchApp(String appName);
void exitApp();
void handleAppInput(String input);
void runAppsApp();
void runSettingsApp();
void saveDisplaySettings();
void loadDisplaySettings();
void saveWiFiConfig();
void connectWiFi();
void initWebServer();
void handleWireless();

// ============================================================
// BLE CALLBACKS
// ============================================================

class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
        deviceConnected = true;
        Serial.println("BLE: Client connected");
        // Request larger MTU for longer messages (WiFi credentials, etc.)
        pServer->updatePeerMTU(pServer->getConnId(), 185);
    }

    void onDisconnect(BLEServer* pServer) {
        deviceConnected = false;
        Serial.println("BLE: Client disconnected");
    }
};

class RxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) {
        std::string rxValue = pCharacteristic->getValue();
        if (rxValue.length() > 0) {
            String received = String(rxValue.c_str());
            Serial.println("BLE RX: " + received);
            bleReceiveBuffer = received;
            processBLECommand(received);
        }
    }
};

// ============================================================
// BLE FUNCTIONS
// ============================================================

void initBLE() {
    Serial.println("Initializing BLE...");
    
    // Set larger MTU to allow longer commands (WiFi SSID/password)
    BLEDevice::setMTU(185);
    
    BLEDevice::init("RoseOS");
    pServer = BLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());
    
    BLEService* pService = pServer->createService(SERVICE_UUID);
    
    pTxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_TX,
        BLECharacteristic::PROPERTY_NOTIFY
    );
    pTxCharacteristic->addDescriptor(new BLE2902());
    
    BLECharacteristic* pRxCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID_RX,
        BLECharacteristic::PROPERTY_WRITE
    );
    pRxCharacteristic->setCallbacks(new RxCallbacks());
    
    pService->start();
    
    BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMinPreferred(0x12);
    BLEDevice::startAdvertising();
    
    Serial.println("BLE: Ready, advertising as 'RoseOS'");
}

void bleSend(String message) {
    if (deviceConnected && pTxCharacteristic != nullptr) {
        pTxCharacteristic->setValue(message.c_str());
        pTxCharacteristic->notify();
        Serial.println("BLE TX: " + message);
    }
}

void processBLECommand(String cmd) {
    cmd.trim();
    
    if (cmd.startsWith("CMD:WIFI:SSID:")) {
        wifiSSID = cmd.substring(14);
        saveWiFiConfig();
        bleSend("OK:SSID saved");
    }
    else if (cmd.startsWith("CMD:WIFI:PASS:")) {
        wifiPassword = cmd.substring(14);
        saveWiFiConfig();
        bleSend("OK:Password saved");
    }
    else if (cmd == "CMD:WIFI:CONNECT") {
        bleSend("OK:Connecting...");
        connectWiFi();
    }
    else if (cmd == "CMD:WIFI:STATUS") {
        String status = wifiConnected ? "Connected to " + wifiSSID : "Not connected";
        if (wifiConnected) {
            status += " IP:" + WiFi.localIP().toString();
        }
        bleSend("STATUS:" + status);
    }
    else if (cmd == "CMD:WIFI:DISCONNECT") {
        WiFi.disconnect();
        wifiConnected = false;
        bleSend("OK:Disconnected");
        if (inAppMenu) refreshDisplay();
    }
    else if (cmd == "CMD:APP:LIST") {
        String list = "APPS:";
        for (int i = 0; i < appCount; i++) {
            if (i > 0) list += ",";
            list += appList[i];
        }
        if (appCount == 0) list += "(none)";
        bleSend(list);
    }
    else if (cmd.startsWith("CMD:APP:RUN:")) {
        String appName = cmd.substring(12);
        launchApp(appName);
    }
    else if (cmd == "CMD:APP:EXIT") {
        exitApp();
    }
    else if (cmd == "CMD:BUTTON" || cmd == "CMD:BUTTON:SHORT") {
        simulatedPressDuration = 100;
        if (inAppMenu) handleButtonPress(false); 
    }
    else if (cmd == "CMD:BUTTON:MEDIUM") {
        simulatedPressDuration = 2500;
        // No direct handle call as medium isn't standard outside settings
    }
    else if (cmd == "CMD:BUTTON:LONG" || cmd == "CMD:BUTTON:VERYLONG") {
        simulatedPressDuration = 5500;
        if (inAppMenu) handleButtonPress(true);
    }
    else if (cmd == "CMD:SYSTEM:INFO") {
        String info = "INFO:RoseOS v1.0|SD:" + String(sdCardPresent ? "Yes" : "No");
        info += "|Apps:" + String(appCount);
        info += "|WiFi:" + String(wifiConnected ? "Yes" : "No");
        info += "|BLE:Yes";
        bleSend(info);
    }
    else if (cmd == "CMD:SD:REFRESH") {
        loadAppsFromSD();
        bleSend("OK:Apps refreshed, found " + String(appCount));
        if (inAppMenu) refreshDisplay();
    }
    else if (cmd.startsWith("CMD:TEXT:")) {
        String text = cmd.substring(9);
        handleAppInput(text);
    }
    // Display settings commands
    else if (cmd == "CMD:DISPLAY:PARTIAL:ON") {
        partialRefreshEnabled = true;
        saveDisplaySettings();
        bleSend("OK:Partial refresh ON");
    }
    else if (cmd == "CMD:DISPLAY:PARTIAL:OFF") {
        partialRefreshEnabled = false;
        saveDisplaySettings();
        bleSend("OK:Partial refresh OFF");
    }
    else if (cmd == "CMD:DISPLAY:INVERT:ON") {
        displayInverted = true;
        saveDisplaySettings();
        bleSend("OK:Display inverted ON");
    }
    else if (cmd == "CMD:DISPLAY:INVERT:OFF") {
        displayInverted = false;
        saveDisplaySettings();
        bleSend("OK:Display inverted OFF");
    }
    else if (cmd.startsWith("CMD:DISPLAY:REFRESH:")) {
        int count = cmd.substring(20).toInt();
        if (count >= 1 && count <= 3) {
            refreshCount = count;
            saveDisplaySettings();
            bleSend("OK:Refresh count=" + String(refreshCount));
        } else {
            bleSend("ERROR:Refresh count must be 1-3");
        }
    }
    else if (cmd == "CMD:DISPLAY:STATUS") {
        String info = "DISPLAY:Invert=" + String(displayInverted ? "ON" : "OFF");
        info += "|Partial=" + String(partialRefreshEnabled ? "ON" : "OFF");
        info += "|Refresh=" + String(refreshCount);
        bleSend(info);
    }
    else {
        bleSend("ERROR:Unknown command");
    }
}

// ============================================================
// WIFI FUNCTIONS
// ============================================================

void loadWiFiConfig() {
    preferences.begin("roseos", true);
    wifiSSID = preferences.getString("wifi_ssid", "");
    wifiPassword = preferences.getString("wifi_pass", "");
    preferences.end();
    
    Serial.println("WiFi config loaded: SSID=" + (wifiSSID.length() > 0 ? wifiSSID : "(none)"));
}

void saveWiFiConfig() {
    preferences.begin("roseos", false);
    preferences.putString("wifi_ssid", wifiSSID);
    preferences.putString("wifi_pass", wifiPassword);
    preferences.end();
    
    Serial.println("WiFi config saved");
}

void connectWiFi() {
    if (wifiSSID.length() == 0) {
        Serial.println("WiFi: No SSID configured");
        bleSend("ERROR:No SSID configured");
        return;
    }
    
    Serial.println("WiFi: Connecting to " + wifiSSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        Serial.println("\nWiFi: Connected! IP: " + WiFi.localIP().toString());
        bleSend("OK:Connected IP:" + WiFi.localIP().toString());
    } else {
        wifiConnected = false;
        Serial.println("\nWiFi: Connection failed");
        bleSend("ERROR:Connection failed");
    }
    
    if (inAppMenu) refreshDisplay();
}

// ============================================================
// SD CARD FUNCTIONS
// ============================================================

bool initSDCard() {
    Serial.println("Initializing SD card...");
    
    // Initialize HSPI for SD card
    sdSPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS, sdSPI)) {
        Serial.println("SD card: Not found or failed to mount");
        sdCardPresent = false;
        return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("SD card: No card inserted");
        sdCardPresent = false;
        return false;
    }
    
    Serial.printf("SD card: Type=%d, Size=%lluMB\n", cardType, SD.cardSize() / (1024 * 1024));
    sdCardPresent = true;
    
    // Create apps directory if it doesn't exist
    if (!SD.exists("/apps")) {
        SD.mkdir("/apps");
        Serial.println("Created /apps directory");
    }
    
    // Create images directory if it doesn't exist
    if (!SD.exists("/images")) {
        SD.mkdir("/images");
        Serial.println("Created /images directory");
    }
    
    return true;
}

void loadAppsFromSD() {
    // Always start with built-in apps
    appCount = 0;
    
    // Add built-in apps first (these always work, even without SD card)
    appList[appCount++] = "clock";     // Built-in clock app
    appList[appCount++] = "notes";     // Built-in notes app
    appList[appCount++] = "Photo";   // Built-in Photo app (needs SD for images)
    appList[appCount++] = "apps";      // Built-in apps browser (for SD apps)
    appList[appCount++] = "settings";  // Built-in settings app (includes system info)
    
    Serial.printf("Added %d built-in apps\n", appCount);
    
    // If SD card is present, add additional apps from /apps/
    if (sdCardPresent) {
        File root = SD.open("/apps");
        if (root && root.isDirectory()) {
            File file = root.openNextFile();
            while (file && appCount < MAX_APPS) {
                String name = String(file.name());
                if (!file.isDirectory() && name.endsWith(".lua")) {
                    // Remove .lua extension for display
                    name = name.substring(0, name.length() - 4);
                    
                    // Don't add duplicates of built-in apps
                    bool isDuplicate = (name == "clock" || name == "notes" || 
                                       name == "Photo" || name == "apps" || 
                                       name == "settings");
                    
                    if (!isDuplicate) {
                        appList[appCount] = name;
                        appCount++;
                        Serial.println("Found SD app: " + name);
                    }
                }
                file = root.openNextFile();
            }
        }
    }
    
    Serial.printf("Total apps: %d (5 built-in + %d from SD)\n", appCount, appCount - 5);
    selectedAppIndex = 0;
}

String readAppScript(String appName) {
    String path = "/apps/" + appName + ".lua";
    File file = SD.open(path);
    if (!file) {
        Serial.println("Cannot open app: " + path);
        return "";
    }
    
    String content = file.readString();
    file.close();
    return content;
}

// ============================================================
// DISPLAY FUNCTIONS
// ============================================================

void initDisplay() {
    Serial.println("Initializing display...");
    SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
    display.init(115200, true, 2, false);
    display.setRotation(1);  // Landscape
    display.setTextColor(GxEPD_BLACK);
    Serial.println("Display initialized");
}

void displayClear() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
}

// Helper function to start display update - respects partial refresh setting
// Uses differential update technique to maintain contrast during partial refresh
void startDisplayUpdate() {
    static int partialUpdateCount = 0;
    
    if (partialRefreshEnabled) {
        partialUpdateCount++;
        
        // Every 15th partial refresh, force a full refresh (deep etch)
        if (partialUpdateCount >= 15) {
            display.setFullWindow();
            partialUpdateCount = 0;
        } else {
            // Use partial window for faster refresh
            display.setPartialWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        }
    } else {
        display.setFullWindow();
        partialUpdateCount = 0;
    }
    display.firstPage();
}

// Enhanced refresh that reinforces contrast after partial update
void endDisplayUpdate() {
    // For partial refresh, do a secondary refresh cycle to strengthen contrast
    // This uses refresh(true) which is faster than full refresh but reinforces pixels
    if (partialRefreshEnabled) {
        // Note: If contrast is still poor, this can be changed to refresh(false)
        // or removed entirely if it causes issues
        display.refresh(true);  // true = partial refresh mode
    }
}

// Get battery percentage (LiPo: 4.0V = 100%, 3.0V = 0%)
int getBatteryPercent() {
    int raw = analogRead(BATTERY_PIN);
    // LilyGo T5 has voltage divider (factor ~2), ADC is 12-bit (0-4095)
    // Reference voltage is 3.3V
    float voltage = (raw / 4095.0) * 3.3 * 2.0;  // Multiply by 2 for voltage divider
    
    // LiPo voltage range: 4.0V (full) to 3.0V (empty) - Adjusted per request
    float percent = ((voltage - 3.0) / (4.0 - 3.0)) * 100.0;
    if (percent > 100) percent = 100;
    if (percent < 0) percent = 0;
    return (int)percent;
}

// Draw battery icon with bars at position (x, y)
// Draw battery icon with bars at position (x, y)
void drawBatteryIcon(int x, int y) {
    int percent = getBatteryPercent();
    
    // Draw battery outline bitmap (20x10)
    display.drawBitmap(x, y, epd_bitmap_Battery, 20, 10, GxEPD_BLACK);
    
    // Fill bars based on percentage (4 bars max)
    // Bitmap is 20x10. Inner area roughly x+2 to x+16, y+2 to y+8
    // Bars: 4 bars, ~3px wide, 1px gap
    
    int bars = (percent + 12) / 25;  // 0-4 bars
    int barWidth = 3;
    int barGap = 1;
    int startBarX = x + 2;
    int startBarY = y + 2;
    int barHeight = 6; // 10px height - 2px top - 2px bottom border
    
    for (int i = 0; i < bars && i < 4; i++) {
        int bx = startBarX + i * (barWidth + barGap);
        display.fillRect(bx, startBarY, barWidth, barHeight, GxEPD_BLACK);
    }
}

void refreshDisplay() {
    if (inAppMenu) {
        drawAppMenu();
    }
}

void drawAppMenu() {
    startDisplayUpdate();
    
    do {
        // Draw the full background bitmap first
        display.drawBitmap(0, 0, epd_bitmap_home_menu, DISPLAY_WIDTH, DISPLAY_HEIGHT, GxEPD_BLACK);
        
        // Status indicators (Overlays)
        // Align with user's design: [BLE] [WIFI] [BATT] on right side
        
        int statusY = 5;
        // Start from right and move left
        
        // Battery: Far right
        drawBatteryIcon(DISPLAY_WIDTH - 25, statusY);
        
        // WiFi: Left of Battery (10x10)
        // Battery is at X = DISPLAY_WIDTH - 25. Width 20.
        // Gap of 5px.
        // WiFi X = DISPLAY_WIDTH - 25 - 5 - 10 = DISPLAY_WIDTH - 40
        if (wifiConnected) {
             display.drawBitmap(DISPLAY_WIDTH - 40, statusY, epd_bitmap_WIFI, 10, 10, GxEPD_BLACK);
        }
        
        // BLE: Left of WiFi (20x7)
        // WiFi At X = DISPLAY_WIDTH - 40
        // Gap of 5px.
        // BLE X = DISPLAY_WIDTH - 40 - 5 - 20 = DISPLAY_WIDTH - 65
        // Center BLE vertially relative to 10px height icons?
        // BLE is 7px high. statusY is top. statusY + (10-7)/2 = statusY + 1
        if (deviceConnected) {
             display.drawBitmap(DISPLAY_WIDTH - 65, statusY + 1, epd_bitmap_BLE, 20, 7, GxEPD_BLACK);
        }
        
        // Draw System Clock in footer right (User design: "23:45" bottom right)
        // Use timeout of 10ms to avoid blocking
         struct tm timeinfo;
         if (getLocalTime(&timeinfo, 10)) {
            char timeStr[6];
            strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
            display.setCursor(DISPLAY_WIDTH - 35, DISPLAY_HEIGHT - 10); 
            display.setTextSize(1);
            display.setTextColor(GxEPD_BLACK);
            display.print(timeStr);
         }

        // Draw App Icons
        // Aligning with the new bitmap outlines
        // Assuming the bitmap has 5 boxes for the apps
        
        int iconSize = 32;
        int iconSpacing = 17;
        int totalWidth = 5 * iconSize + 4 * iconSpacing;
        int startX = (DISPLAY_WIDTH - totalWidth) / 2;
        int startY = 42;
        
        // 5 built-in apps: Clock, Notes, Photo, Apps, Setup
        const unsigned char* icons[] = {icon_clock, icon_notes, icon_Photo, icon_apps, icon_settings};
        const char* labels[] = {"Clock", "Notes", "Photo", " Apps", "Setup"};
        
        // Ensure text size is reset to 1 (boot animation uses size 2)
        display.setTextSize(1);
        display.setTextColor(GxEPD_BLACK);
        
        for (int i = 0; i < 5; i++) {
            int x = startX + i * (iconSize + iconSpacing);
            int y = startY;
            
            if (i == selectedAppIndex) {
                 // Selected: Draw filled round rect behind to highlight
                 display.fillRoundRect(x - 2, y - 2, iconSize + 4, iconSize + 4, 3, GxEPD_BLACK);
                 display.drawBitmap(x, y, icons[i], iconSize, iconSize, GxEPD_WHITE);
            } else {
                display.drawBitmap(x, y, icons[i], iconSize, iconSize, GxEPD_BLACK);
            }
            
            // Draw label below icon
            display.setCursor(x, y + iconSize + 3);
            display.print(labels[i]);
        }
        
        // Note: Footer buttons text "[BTN]Next..." assumed to be in background bitmap
        
    } while (display.nextPage());
    
    // Reinforce contrast after partial refresh
    endDisplayUpdate();
}

void drawAppScreen(String title, String content) {
    startDisplayUpdate();
    
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Header with app name
        display.fillRect(0, 0, DISPLAY_WIDTH, 18, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
        display.setTextSize(1);
        display.setCursor(5, 5);
        display.print(title);
        
        display.setCursor(DISPLAY_WIDTH - 35, 5);
        display.print("[EXIT]");
        
        display.setTextColor(GxEPD_BLACK);
        
        // Content area
        display.setCursor(5, 25);
        display.setTextSize(1);
        
        // Simple word wrap
        int x = 5;
        int y = 25;
        int maxWidth = DISPLAY_WIDTH - 10;
        
        for (int i = 0; i < content.length(); i++) {
            char c = content.charAt(i);
            if (c == '\n') {
                x = 5;
                y += 12;
            } else {
                display.setCursor(x, y);
                display.print(c);
                x += 6;  // Approximate char width
                if (x > maxWidth) {
                    x = 5;
                    y += 12;
                }
            }
            if (y > DISPLAY_HEIGHT - 20) break;
        }
        
        // Footer
        display.drawLine(0, DISPLAY_HEIGHT - 15, DISPLAY_WIDTH, DISPLAY_HEIGHT - 15, GxEPD_BLACK);
        display.setCursor(5, DISPLAY_HEIGHT - 10);
        display.print("[BTN] Action  [LONG] Exit");
        
    } while (display.nextPage());
}

// ============================================================
// APP EXECUTION (Simplified - Command Based)
// ============================================================

void launchApp(String appName) {
    Serial.println("Launching app: " + appName);
    
    // Check if this is a built-in app (always available)
    bool isBuiltIn = (appName == "clock" || appName == "sysinfo" || 
                      appName == "notes" || appName == "Photo");
    
    // For non-built-in apps, verify they exist
    if (!isBuiltIn) {
        bool found = false;
        for (int i = 0; i < appCount; i++) {
            if (appList[i] == appName) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            // Try to load from SD if present
            if (sdCardPresent) {
                String script = readAppScript(appName);
                if (script.length() == 0) {
                    bleSend("ERROR:App not found: " + appName);
                    return;
                }
            } else {
                bleSend("ERROR:App not found: " + appName);
                return;
            }
        }
    }
    
    currentApp = appName;
    inAppMenu = false;
    appRunning = true;
    
    bleSend("OK:Launched " + appName);
    
    // Execute app
    executeApp(appName);
}

void executeApp(String appName) {
    // For now, we implement a simple command-based system
    // Full Lua support would require the Lua library
    
    if (appName == "clock") {
        runClockApp();
    }
    else if (appName == "notes") {
        runNotesApp();
    }
    else if (appName == "Photo") {
        runPhotoApp();
    }
    else if (appName == "apps") {
        runAppsApp();
    }
    else if (appName == "settings") {
        runSettingsApp();
    }
    else {
        // Try to load and parse Lua script
        String script = readAppScript(appName);
        if (script.length() > 0) {
            runGenericApp(appName, script);
        } else {
            drawAppScreen(appName, "App loaded.\n\nThis app requires\nLua interpreter.\n\n[BTN] to exit");
        }
    }
}

void exitApp() {
    Serial.println("Exiting app: " + currentApp);
    currentApp = "";
    inAppMenu = true;
    appRunning = false;
    bleSend("OK:Returned to menu");
    refreshDisplay();
}

void handleAppInput(String input) {
    if (!appRunning) {
        bleSend("ERROR:No app running");
        return;
    }
    
    Serial.println("App input: " + input);
    // App-specific input handling would go here
    bleSend("OK:Input received");
}

// ============================================================
// BUILT-IN APPS
// ============================================================

void runClockApp() {
    // Simple clock display
    while (appRunning) {
        struct tm timeinfo;
        if (getLocalTime(&timeinfo)) {
            startDisplayUpdate();
            
            do {
                display.fillScreen(GxEPD_WHITE);
                
                // Large digital time at top (like reference image)
                char timeStr[10];
                strftime(timeStr, sizeof(timeStr), "%H:%M", &timeinfo);
                
                // Draw time with large font (size 3 = ~24 pixels high)
                display.setTextColor(GxEPD_BLACK);
                display.setTextSize(3);
                display.setCursor(55, 15);
                display.print(timeStr);
                
                // Day name box (calendar style - inverted like reference)
                // Get day of week (0 = Sunday)
                int dayOfWeek = timeinfo.tm_wday;
                const char* dayName = dayNamesNO[dayOfWeek];  // Norwegian day names
                
                // Black box with white text for day name
                int boxX = 75;
                int boxY = 50;
                int boxW = 100;
                int boxH = 35;
                display.fillRect(boxX, boxY, boxW, boxH, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
                display.setTextSize(2);
                display.setCursor(boxX + 30, boxY + 10);
                display.print(dayName);
                
                // Date below the day box
                char dateStr[20];
                strftime(dateStr, sizeof(dateStr), "%d.%m.%Y", &timeinfo);
                display.setTextColor(GxEPD_BLACK);
                display.setTextSize(1);
                display.setCursor(90, 92);
                display.print(dateStr);
                
                // Footer
                display.drawLine(0, DISPLAY_HEIGHT - 18, DISPLAY_WIDTH, DISPLAY_HEIGHT - 18, GxEPD_BLACK);
                display.setCursor(5, DISPLAY_HEIGHT - 10);
                display.print("[LONG] Exit");
                
            } while (display.nextPage());
        } else {
            drawAppScreen("Clock", "Time not set\n\nConnect to WiFi\nto sync time");
        }
        
        // Check for exit - more frequent checks for better BLE responsiveness
        for (int i = 0; i < 3000; i++) {  // Check every 20ms for 60 seconds
            checkButton();
            handleWireless();
            if (!appRunning) return;
            delay(20);
        }
    }
}

void runSysInfoApp() {
    String info = "System Information\n";
    info += "==================\n\n";
    info += "SD Card: " + String(sdCardPresent ? "Yes" : "No") + "\n";
    if (sdCardPresent) {
        info += "SD Size: " + String(SD.cardSize() / (1024 * 1024)) + " MB\n";
    }
    info += "Apps: " + String(appCount) + "\n";
    info += "WiFi: " + String(wifiConnected ? wifiSSID : "Not connected") + "\n";
    if (wifiConnected) {
        info += "IP: " + WiFi.localIP().toString() + "\n";
    }
    info += "BLE: " + String(deviceConnected ? "Connected" : "Ready") + "\n";
    info += "Free Heap: " + String(ESP.getFreeHeap() / 1024) + " KB\n";
    
    drawAppScreen("System Info", info);
    
    while (appRunning) {
        checkButton();
        handleWireless();
        delay(100);
    }
}

void runNotesApp() {
    String notes = "Notes App\n";
    notes += "=========\n\n";
    notes += "Send notes via BLE:\n";
    notes += "CMD:TEXT:Your note\n\n";
    notes += "Notes will appear here.";
    
    drawAppScreen("Notes", notes);
    
    while (appRunning) {
        checkButton();
        handleWireless();
        
        // Check for new text input
        if (bleReceiveBuffer.startsWith("CMD:TEXT:")) {
            String note = bleReceiveBuffer.substring(9);
            notes = "Note received:\n\n" + note;
            drawAppScreen("Notes", notes);
            bleReceiveBuffer = "";
        }
        
        delay(100);
    }
}

void runPhotoApp() {
    if (!sdCardPresent) {
        drawAppScreen("Photo", "No SD card\n\nInsert SD card with\nimages in /images/");
        while (appRunning) {
            checkButton();
            handleWireless();
            delay(100);
        }
        return;
    }
    
    // List images
    File root = SD.open("/images");
    if (!root) {
        drawAppScreen("Photo", "Cannot open /images");
        while (appRunning) {
            checkButton();
            handleWireless();
            delay(100);
        }
        return;
    }
    
    String imageList[20];
    int imageCount = 0;
    
    File file = root.openNextFile();
    while (file && imageCount < 20) {
        String name = String(file.name());
        if (name.endsWith(".bin")) {
            imageList[imageCount] = name;
            imageCount++;
        }
        file = root.openNextFile();
    }
    
    if (imageCount == 0) {
        drawAppScreen("Photo", "No images found\n\nin /images/*.bin");
        while (appRunning) {
            checkButton();
            handleWireless();
            delay(100);
        }
        return;
    }
    
    int currentImage = 0;
    
    // Show Photo
    while (appRunning) {
        // Display current image info
        String info = "Image " + String(currentImage + 1) + "/" + String(imageCount);
        info += "\n\n" + imageList[currentImage];
        info += "\n\n[BTN] Next image\n[LONG] Exit";
        drawAppScreen("Photo", info);
        
        // Wait for input
        bool imageChanged = false;
        while (!imageChanged && appRunning) {
            checkButton();
            handleWireless();
            delay(100);
        }
        
        currentImage = (currentImage + 1) % imageCount;
    }
}

void runGenericApp(String name, String script) {
    Serial.println("Running generic app: " + name);
    
    // Simple line-by-line interpreter
    // Supports basic "Lua-like" commands:
    // print("text")
    // delay(ms)
    // screen.clear()
    // screen.print(x, y, "text", size)
    // screen.line(x1, y1, x2, y2)
    // screen.rect(x, y, w, h, fill)
    // screen.update()
    // wait_btn()
    
    int startIndex = 0;
    while (appRunning && startIndex < script.length()) {
        int endIndex = script.indexOf('\n', startIndex);
        if (endIndex == -1) endIndex = script.length();
        
        String line = script.substring(startIndex, endIndex);
        line.trim();
        startIndex = endIndex + 1;
        
        if (line.length() == 0 || line.startsWith("--")) continue; // Skip comments/empty
        
        // Check for App Exit
        checkButton();
        if (!appRunning) break;
        
        // --- Commands ---
        
        // print("text")
        if (line.startsWith("print(\"")) {
            int q2 = line.lastIndexOf('"');
            if (q2 > 7) {
                String text = line.substring(7, q2);
                Serial.println("App [" + name + "]: " + text);
            }
        }
        
        // delay(ms)
        else if (line.startsWith("delay(")) {
            int p2 = line.indexOf(')');
            if (p2 > 6) {
                int ms = line.substring(6, p2).toInt();
                // Break delay into chunks to check button
                unsigned long startNode = millis();
                while (millis() - startNode < ms && appRunning) {
                    checkButton();
                    handleWireless();
                    delay(10);
                }
            }
        }
        
        // wait_btn()
        else if (line.indexOf("wait_btn()") >= 0) {
            bool waited = false;
            while (!waited && appRunning) {
                checkButton();
                if (buttonPressed) { // Simple check, might need edge detection logic from checkButton
                     // Wait for release
                     while(digitalRead(BUTTON_PIN) == LOW) delay(10);
                     waited = true;
                }
                handleWireless();
                delay(50);
            }
        }
        
        // screen.clear()
        else if (line.indexOf("screen.clear()") >= 0) {
            display.fillScreen(GxEPD_WHITE);
        }
        
        // screen.update()
        else if (line.indexOf("screen.update()") >= 0) {
            // Check if partial refresh is allowed
            if (partialRefreshEnabled) {
                display.display(true); // Partial update
            } else {
                display.display(false); // Full update
            }
        }
        
        // screen.print(x, y, "text", size)
        else if (line.startsWith("screen.print(")) {
            // Parse args crudely (assuming correct format)
            // Example: screen.print(10, 20, "Hello", 2)
            int c1 = line.indexOf(',');
            int c2 = line.indexOf(',', c1 + 1);
            int q1 = line.indexOf('"', c2 + 1);
            int q2 = line.indexOf('"', q1 + 1);
            int c3 = line.indexOf(',', q2 + 1);
            int pEnd = line.lastIndexOf(')');
            
            if (c1 > 0 && c2 > c1 && q1 > c2 && q2 > q1) {
                int x = line.substring(13, c1).toInt();
                int y = line.substring(c1 + 1, c2).toInt();
                String text = line.substring(q1 + 1, q2);
                int size = 1;
                if (c3 > 0) size = line.substring(c3 + 1, pEnd).toInt();
                
                display.setCursor(x, y);
                display.setTextSize(size);
                display.setTextColor(GxEPD_BLACK);
                display.print(text);
            }
        }
        
        // screen.line(x1, y1, x2, y2)
        else if (line.startsWith("screen.line(")) {
            int c1 = line.indexOf(',');
            int c2 = line.indexOf(',', c1+1);
            int c3 = line.indexOf(',', c2+1);
            int pEnd = line.indexOf(')');
            
            if (c3 > 0) {
                int x1 = line.substring(12, c1).toInt();
                int y1 = line.substring(c1+1, c2).toInt();
                int x2 = line.substring(c2+1, c3).toInt();
                int y2 = line.substring(c3+1, pEnd).toInt();
                display.drawLine(x1, y1, x2, y2, GxEPD_BLACK);
            }
        }
        
        // screen.rect(x, y, w, h, fill)
        else if (line.startsWith("screen.rect(")) {
            int c1 = line.indexOf(',');
            int c2 = line.indexOf(',', c1+1);
            int c3 = line.indexOf(',', c2+1);
            int c4 = line.indexOf(',', c3+1);
            int pEnd = line.indexOf(')');
            
            if (c4 > 0) {
                int x = line.substring(12, c1).toInt();
                int y = line.substring(c1+1, c2).toInt();
                int w = line.substring(c2+1, c3).toInt();
                int h = line.substring(c3+1, c4).toInt();
                int fill = line.substring(c4+1, pEnd).toInt();
                
                if (fill) {
                    display.fillRect(x, y, w, h, GxEPD_BLACK);
                } else {
                    display.drawRect(x, y, w, h, GxEPD_BLACK);
                }
            }
        }
    }
    
    // If we reach the end and still running, wait for exit
    if (appRunning) {
        // Draw a "Finished" status line if screen wasn't handled
        // or just wait.
        while (appRunning) {
            checkButton();
            handleWireless();
            delay(100);
        }
    }
}

void runAppsApp() {
    // List SD card apps (beyond built-in ones)
    if (!sdCardPresent) {
        drawAppScreen("Apps", "No SD card\n\nInsert SD card with\n/apps/*.lua files");
        while (appRunning) {
            checkButton();
            handleWireless();
            delay(100);
        }
        return;
    }
    
    String info = "SD Card Apps\n============\n\n";
    int sdAppCount = 0;
    for (int i = 6; i < appCount; i++) {  // Skip first 6 built-in apps
        info += "• " + appList[i] + "\n";
        sdAppCount++;
    }
    
    if (sdAppCount == 0) {
        info += "No custom apps found.\n\nAdd .lua files to\n/apps/ on SD card.";
    } else {
        info += "\n" + String(sdAppCount) + " apps found";
    }
    
    drawAppScreen("Apps", info);
    
    while (appRunning) {
        checkButton();
        handleWireless();
        delay(100);
    }
}

void runSettingsApp() {
    int selectedSetting = 0;
    const int numSettings = 3;
    const unsigned long mediumPressTime = 2000;  // 2 seconds
    const unsigned long veryLongPressTime = 5000; // 5 seconds
    
    while (appRunning) {
        startDisplayUpdate();
        
        do {
            display.fillScreen(GxEPD_WHITE);
            
            // Header
            display.fillRect(0, 0, DISPLAY_WIDTH, 18, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
            display.setTextSize(1);
            display.setCursor(5, 5);
            display.print("Settings");
            display.setTextColor(GxEPD_BLACK);
            
            int y = 26;
            int lineHeight = 20;
            
            // Setting 1: Invert Display
            if (selectedSetting == 0) {
                display.fillRect(0, y - 2, DISPLAY_WIDTH, lineHeight, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            }
            display.setCursor(5, y);
            display.print("Invert Display: ");
            display.print(displayInverted ? "ON" : "OFF");
            display.setTextColor(GxEPD_BLACK);
            y += lineHeight;
            
            // Setting 2: Refresh Count
            if (selectedSetting == 1) {
                display.fillRect(0, y - 2, DISPLAY_WIDTH, lineHeight, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            }
            display.setCursor(5, y);
            display.print("Refresh Count: ");
            display.print(String(refreshCount));
            display.setTextColor(GxEPD_BLACK);
            y += lineHeight;
            
            // Setting 3: Partial Refresh
            if (selectedSetting == 2) {
                display.fillRect(0, y - 2, DISPLAY_WIDTH, lineHeight, GxEPD_BLACK);
                display.setTextColor(GxEPD_WHITE);
            }
            display.setCursor(5, y);
            display.print("Partial Refresh: ");
            display.print(partialRefreshEnabled ? "ON" : "OFF");
            display.setTextColor(GxEPD_BLACK);
            y += lineHeight + 8;
            
            // IP Address section (prominent)
            display.drawLine(0, y, DISPLAY_WIDTH, y, GxEPD_BLACK);
            y += 8;
            
            // Large IP Address box
            display.fillRect(0, y, DISPLAY_WIDTH, 24, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
            display.setCursor(5, y + 8);
            if (wifiConnected) {
                display.print("http://" + WiFi.localIP().toString());
            } else {
                display.print("WiFi not connected");
            }
            display.setTextColor(GxEPD_BLACK);
            
            // Footer with new instructions
            display.drawLine(0, DISPLAY_HEIGHT - 18, DISPLAY_WIDTH, DISPLAY_HEIGHT - 18, GxEPD_BLACK);
            display.setCursor(3, DISPLAY_HEIGHT - 12);
            display.print("<2s:Next 2-5s:Change >5s:Save");
            
        } while (display.nextPage());
        
        // Wait for button with 3-level detection
        bool actionTaken = false;
        while (!actionTaken && appRunning) {
            bool currentState = digitalRead(BUTTON_PIN);
            unsigned long pressDuration = 0;
            bool triggerAction = false;
            
            // Check for simulated press from BLE
            if (simulatedPressDuration > 0) {
                pressDuration = simulatedPressDuration;
                simulatedPressDuration = 0; // Reset
                triggerAction = true;
                bleSend("OK:Simulated press " + String(pressDuration) + "ms");
            }
            
            // Physical button pressed
            if (currentState == LOW && lastButtonState == HIGH) {
                buttonPressStart = millis();
                buttonPressed = true;
            }
            
            // Physical button released
            if (currentState == HIGH && lastButtonState == LOW && buttonPressed) {
                pressDuration = millis() - buttonPressStart;
                buttonPressed = false;
                triggerAction = true;
            }
            
            if (triggerAction) {
                if (pressDuration >= veryLongPressTime) {
                    // Very long press (>5s) - save and exit
                    saveDisplaySettings();
                    appRunning = false;
                    exitApp();
                    return;
                } else if (pressDuration >= mediumPressTime) {
                    // Medium press (2-5s) - change current setting value
                    if (selectedSetting == 0) {
                        displayInverted = !displayInverted;
                    } else if (selectedSetting == 1) {
                        refreshCount = (refreshCount % 3) + 1;
                    } else if (selectedSetting == 2) {
                        partialRefreshEnabled = !partialRefreshEnabled;
                    }
                    actionTaken = true;
                } else {
                    // Short press (<2s) - move to next setting
                    selectedSetting = (selectedSetting + 1) % numSettings;
                    actionTaken = true;
                }
            }
            
            lastButtonState = currentState;
            handleWireless();
            delay(20);
        }
    }
}

void saveDisplaySettings() {
    preferences.begin("RoseOS", false);
    preferences.putBool("inverted", displayInverted);
    preferences.putInt("refresh", refreshCount);
    preferences.putBool("partial", partialRefreshEnabled);
    preferences.end();
    Serial.println("Display settings saved");
}

void loadDisplaySettings() {
    preferences.begin("RoseOS", true);
    displayInverted = preferences.getBool("inverted", false);
    refreshCount = preferences.getInt("refresh", 1);
    partialRefreshEnabled = preferences.getBool("partial", true);
    preferences.end();
    Serial.println("Display settings loaded");
}

// ============================================================
// BUTTON HANDLING
// ============================================================

void checkButton() {
    bool currentState = digitalRead(BUTTON_PIN);
    
    // Button pressed (falling edge)
    if (currentState == LOW && lastButtonState == HIGH) {
        buttonPressStart = millis();
        buttonPressed = true;
    }
    
    // Button released (rising edge)
    if (currentState == HIGH && lastButtonState == LOW && buttonPressed) {
        unsigned long pressDuration = millis() - buttonPressStart;
        buttonPressed = false;
        
        if (millis() - lastButtonPress > debounceDelay) {
            lastButtonPress = millis();
            
            if (pressDuration >= longPressTime) {
                handleButtonPress(true);  // Long press
            } else {
                handleButtonPress(false); // Short press
            }
        }
    }
    
    lastButtonState = currentState;
}

void handleButtonPress(bool longPress) {
    Serial.println(longPress ? "Long press" : "Short press");
    
    if (inAppMenu) {
        if (longPress && appCount > 0) {
            // Launch selected app
            launchApp(appList[selectedAppIndex]);
        } else if (!longPress && appCount > 0) {
            // Navigate to next app
            selectedAppIndex = (selectedAppIndex + 1) % appCount;
            refreshDisplay();
        }
    } else {
        if (longPress) {
            // Exit current app
            exitApp();
        } else {
            // App-specific action (handled by app)
            // For built-in apps, this might cycle through content
        }
    }
}

// ============================================================
// BLE CONNECTION MANAGEMENT
// ============================================================

void handleBLEConnection() {
    // Allow BLE stack to process - critical for maintaining connection
    yield();
    delay(1);  // Small delay to let BLE process
    
    // Handle disconnection - restart advertising
    if (!deviceConnected && oldDeviceConnected) {
        delay(100);  // Reduced from 500ms
        pServer->startAdvertising();
        Serial.println("BLE: Restarted advertising");
        oldDeviceConnected = deviceConnected;
    }
    
    // Handle new connection
    if (deviceConnected && !oldDeviceConnected) {
        oldDeviceConnected = deviceConnected;
    }
}

// ============================================================
// WEB SERVER MANAGEMENT
// ============================================================

void initWebServer() {
    // Enable CORS for all routes (allows requests from any origin)
    server.enableCORS(true);
    
    server.on("/", []() {
        server.send(200, "text/html", html_controller);
    });
    
    server.on("/api/cmd", []() {
        if (server.hasArg("q")) {
            String cmd = server.arg("q");
            Serial.println("Web CMD: " + cmd);
            processBLECommand(cmd); // Reuse the command processor
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Missing q parameter");
        }
    });

    server.begin();
    Serial.println("HTTP server started on port 80");
}

void handleWireless() {
    // Check BLE
    handleBLEConnection();
    
    // Check WiFi Web Server
    if (wifiConnected) {
        server.handleClient();
    }
}

// ============================================================
// MAIN SETUP & LOOP
// ============================================================

// Flag for boot synchronization
volatile bool bootLoadingCompleted = false;

// Task for system initialization
void systemInitTask(void * parameter) {
    // Initialize BLE
    initBLE();
    
    // Load configurations
    loadWiFiConfig();
    loadDisplaySettings();
    
    // Try to connect to WiFi if configured
    if (wifiSSID.length() > 0) {
        connectWiFi();
    }
    
    // Configure time
    if (wifiConnected) {
        configTime(3600, 0, "pool.ntp.org");
        initWebServer(); // Initialize Web Server after WiFi
    }
    
    // Initialize SD card
    initSDCard();
    
    // Load apps
    loadAppsFromSD();
    
    // Signal completion
    bootLoadingCompleted = true;
    vTaskDelete(NULL);
}

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.println("\n\n====================================");
    Serial.println("        RoseOS for LilyGo T5       ");
    Serial.println("====================================");
    
    // Initialize button
    pinMode(BUTTON_PIN, INPUT);
    
    // Initialize display
    initDisplay();
    
    // Show boot screen with logo
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Draw centered boot logo (63x20px)
        int logoX = (DISPLAY_WIDTH - 63) / 2;
        int logoY = (DISPLAY_HEIGHT - 20) / 2 - 10;
        display.drawBitmap(logoX, logoY, epd_bitmap_boot, 63, 20, GxEPD_BLACK);
        
    } while (display.nextPage());
    
    // Start Initialization Task on Core 0 (Wireless/System core)
    xTaskCreatePinnedToCore(
      systemInitTask,   /* Function to implement the task */
      "SystemInit",     /* Name of the task */
      10000,            /* Stack size in words */
      NULL,             /* Task input parameter */
      1,                /* Priority of the task */
      NULL,             /* Task handle */
      0);               /* Core where the task should run */

    // 3-Dot Loading Animation Loop (Main Thread)
    display.setPartialWindow(0, 40, DISPLAY_WIDTH, 80);
    display.setTextSize(2);
    display.setTextColor(GxEPD_BLACK);
    
    int dotState = 0;
    while (!bootLoadingCompleted) {
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            
            // Redraw logo to keep it sharp
            int logoX = (DISPLAY_WIDTH - 63) / 2;
            int logoY = (DISPLAY_HEIGHT - 20) / 2 - 10;
            display.drawBitmap(logoX, logoY, epd_bitmap_boot, 63, 20, GxEPD_BLACK);
            
            // Draw Dots
            display.setCursor(110, 90); 
            if (dotState == 0) display.print("   ");
            if (dotState == 1) display.print(".  ");
            if (dotState == 2) display.print(".. ");
            if (dotState == 3) display.print("...");
        } while (display.nextPage());
        
        dotState = (dotState + 1) % 4;
        delay(300); // Animation speed
    }
    
    // Reset text size after boot animation (was size 2 for dots)
    display.setTextSize(1);
    display.setFullWindow();
    
    // Manual Refresh Sequence to clear ghosting and improve contrast
    bool savedRefreshSetting = partialRefreshEnabled;
    
    // 1. One Full Refresh (Clear artifacts)
    partialRefreshEnabled = false; 
    drawAppMenu(); 
    
    partialRefreshEnabled = savedRefreshSetting; // Restore setting
    
    Serial.println("====================================");
    Serial.println("RoseOS Ready!");
    Serial.println("BLE Name: RoseOS");
    Serial.println("SD Card: " + String(sdCardPresent ? "Yes" : "No"));
    Serial.println("Apps: " + String(appCount));
    Serial.println("====================================");
}

void loop() {
    if (inAppMenu) {
        checkButton();
        handleWireless();
        
        // Periodic clock update
        unsigned long now = millis();
        // Update every minute (60000ms) if partial refresh is ON
        // Update every 5 minutes (300000ms) if partial refresh is OFF (to save battery/flashing)
        unsigned long updateInterval = partialRefreshEnabled ? 60000 : 300000;
        
        if (now - lastClockUpdate >= updateInterval) {
            lastClockUpdate = now;
            refreshDisplay(); // This will redraw home menu with updated time
        }
        
        delay(10);
    }
    // When running an app, the app's loop handles everything
}

