#include <lapi.h>
#include <lauxlib.h>
#include <lcode.h>
#include <ldebug.h>
#include <ldo.h>
#include <lfunc.h>
#include <lgc.h>
#include <llex.h>
#include <llimits.h>
#include <lmem.h>
#include <lobject.h>
#include <lopcodes.h>
#include <lparser.h>
#include <lstate.h>
#include <lstring.h>
#include <ltable.h>
#include <ltm.h>
#include <lua.h>
#include <luaconf.h>
#include <lualib.h>
#include <lundump.h>
#include <lvm.h>
#include <lzio.h>

/* Lua's io/posix code can define getline macro; undef to avoid conflict with C++ std::getline */
#ifdef getline
#undef getline
#endif

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <LittleFS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <GxEPD2_BW.h>
#include "icons.h"
#include "home.h"
#include "embedded_lua.h"

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

// Core Lua Header
extern "C" {
  #include <lua.h>
  #include <lualib.h>
  #include <lauxlib.h>
}

// --- Status ---
lua_State *L = nullptr;
bool luaReady = false;
bool sdReady = false;
bool littlefsReady = false;
bool spiffsReady = false;  // Sketch Data Upload bruker ofte SPIFFS-partisjon
WiFiClient tcpClient;

// WiFi lagret for BLE-kommandoer (CMD:WIFI:SSID/PASS:CONNECT)
static String wifiStoredSSID = "";
static String wifiStoredPass = "";

// BLE: buffer for linjedelte kommandoer (én kommando per linje)
static String bleRxBuffer = "";

// Forward for BLE
static void processBLECommand(String cmd);
static void initBLE();
static void bleSend(String msg);
static void handleBLEConnection();

// --- GPIO Bindings ---
static int l_gpio_mode(lua_State *L) {
    int pin = luaL_checkinteger(L, 1);
    int mode = luaL_checkinteger(L, 2); 
    pinMode(pin, mode ? OUTPUT : INPUT);
    return 0;
}

static int l_gpio_write(lua_State *L) {
    int pin = luaL_checkinteger(L, 1);
    int val = luaL_checkinteger(L, 2);
    digitalWrite(pin, val);
    return 0;
}

static int l_gpio_read(lua_State *L) {
    int pin = luaL_checkinteger(L, 1);
    lua_pushinteger(L, digitalRead(pin));
    return 1;
}

// --- Screen Bindings ---
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

class RoseBoxBLERxCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* pCharacteristic) override {
        std::string rx = pCharacteristic->getValue();
        if (rx.length() == 0) return;
        bleRxBuffer += String(rx.c_str());
        // Prosesser kommandoer: splitt på linjeskift eller på neste "CMD:" (flere kommandoer i én pakke)
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
                if (bleRxBuffer.length() > 0 && (bleRxBuffer[0] == '\n' || bleRxBuffer[0] == '\r')) bleRxBuffer = bleRxBuffer.substring(1);
            } else {
                bleRxBuffer = bleRxBuffer.substring(idx);
            }
            line.trim();
            if (line.length() > 0) {
                Serial.println("BLE RX: " + line);
                processBLECommand(line);
            }
        }
        // Enkelt kommando uten linjeskift (f.eks. bare CMD:WIFI:PASS:xxx)
        bleRxBuffer.trim();
        if (bleRxBuffer.length() > 0 && bleRxBuffer.startsWith("CMD:")) {
            Serial.println("BLE RX: " + bleRxBuffer);
            processBLECommand(bleRxBuffer);
            bleRxBuffer = "";
        }
        if (bleRxBuffer.length() > 512) bleRxBuffer = bleRxBuffer.substring(bleRxBuffer.length() - 256);
    }
};

static void initBLE() {
    Serial.println("Initializing BLE...");
    BLEDevice::setMTU(185);
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

#define LUA_DRAW_REF_NONE (-2)
static int drawCallbackRef = LUA_DRAW_REF_NONE;

static int l_screen_init(lua_State *L) {
    display.init(115200, true, 2, false);
    display.setRotation(1);
    display.setTextColor(EPD_FG());
    return 0;
}

static int l_screen_register_draw(lua_State *L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    lua_pushvalue(L, 1);
    if (drawCallbackRef != LUA_DRAW_REF_NONE) {
        luaL_unref(L, LUA_REGISTRYINDEX, drawCallbackRef);
    }
    drawCallbackRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

static int l_screen_unregister_draw(lua_State *L) {
    if (drawCallbackRef != LUA_DRAW_REF_NONE) {
        luaL_unref(L, LUA_REGISTRYINDEX, drawCallbackRef);
        drawCallbackRef = LUA_DRAW_REF_NONE;
    }
    return 0;
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

static int l_screen_clear(lua_State *L) {
    applyDisplayWindow(true);
    display.fillScreen(EPD_BG());
    return 0;
}

static int l_screen_drawText(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    const char* text = luaL_checkstring(L, 3);
    
    display.setCursor(x, y);
    display.print(text);
    return 0;
}

static int l_screen_setTextColor(lua_State *L) {
    int white = lua_toboolean(L, 1);
    display.setTextColor(white ? EPD_BG() : EPD_FG());
    return 0;
}

static int l_screen_update(lua_State *L) {
    applyDisplayWindow(false);
    if (drawCallbackRef != LUA_DRAW_REF_NONE) {
        // Callback-modus: firstPage/nextPage tegner og refresher (én gang)
        display.firstPage();
        do {
            yield();
            display.fillScreen(EPD_BG());
            display.setTextColor(EPD_FG());
            lua_rawgeti(L, LUA_REGISTRYINDEX, drawCallbackRef);
            if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
                Serial.println(lua_tostring(L, -1));
                lua_pop(L, 1);
            }
        } while (display.nextPage());
        // IKKE kall display.display() – nextPage() har allerede refreshet
    } else {
        // Direkte-modus: buffer er fylt av clear+draw-kall, send med display()
        display.display(partialRefreshEnabled);
    }
    return 0;
}

// Home screen / dashboard (home.h – 250x122, full screen på 2.13")
static int l_screen_drawHome(lua_State *L) {
    display.setFullWindow();
    display.firstPage();
    do {
        yield();
        display.fillScreen(EPD_BG());
        display.drawBitmap(0, 0, epd_bitmap_home_menu, 250, 122, EPD_FG());
    } while (display.nextPage());
    return 0;
}

// Home + app menu: grid med opptil 5 app-celler (containere). Lua: screen_drawHomeWithMenu({ "terminal", "clock", "settings", "apps" }, selectedIndex)
static const unsigned char* getAppIcon(const char* name) {
    if (strcmp(name, "terminal") == 0) return epd_bitmap_terminal_icon;
    if (strcmp(name, "clock") == 0) return icon_clock;
    if (strcmp(name, "notes") == 0) return icon_notes;
    if (strcmp(name, "photo") == 0) return icon_Photo;
    if (strcmp(name, "apps") == 0) return icon_apps;
    if (strcmp(name, "settings") == 0 || strcmp(name, "setup") == 0) return icon_settings;
    return icon_apps;  // ukjente apper
}

static int l_screen_drawHomeWithMenu(lua_State *L) {
    luaL_checktype(L, 1, LUA_TTABLE);
    int selected = (int)luaL_checkinteger(L, 2);  // 1-based from Lua
    const int gridSlots = 5;  // fast antall ruter i gridet
    const char* names[gridSlots];
    int n = 0;
    for (int i = 1; i <= gridSlots; i++) {
        lua_rawgeti(L, 1, i);
        if (lua_isnil(L, -1)) { lua_pop(L, 1); break; }
        names[n++] = lua_tostring(L, -1);
        lua_pop(L, 1);
    }

    const int iconSize = 32;
    const int gridCols = 5;
    const int cellW = 250 / gridCols;   // 50 px per celle
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
    // nextPage() har allerede refreshet – ingen display() nødvendig
    return 0;
}

static int l_screen_drawLine(lua_State *L) {
    int x0 = luaL_checkinteger(L, 1);
    int y0 = luaL_checkinteger(L, 2);
    int x1 = luaL_checkinteger(L, 3);
    int y1 = luaL_checkinteger(L, 4);
    display.drawLine(x0, y0, x1, y1, EPD_FG());
    return 0;
}

static int l_screen_fillCircle(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int r = luaL_checkinteger(L, 3);
    display.fillCircle(x, y, r, EPD_FG());
    return 0;
}

static int l_screen_drawCircle(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int r = luaL_checkinteger(L, 3);
    display.drawCircle(x, y, r, EPD_FG());
    return 0;
}

static int l_screen_fillRect(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    display.fillRect(x, y, w, h, EPD_FG());
    return 0;
}

static int l_screen_drawRect(lua_State *L) {
    int x = luaL_checkinteger(L, 1);
    int y = luaL_checkinteger(L, 2);
    int w = luaL_checkinteger(L, 3);
    int h = luaL_checkinteger(L, 4);
    display.drawRect(x, y, w, h, EPD_FG());
    return 0;
}

static int l_screen_fillTriangle(lua_State *L) {
    int x0 = luaL_checkinteger(L, 1);
    int y0 = luaL_checkinteger(L, 2);
    int x1 = luaL_checkinteger(L, 3);
    int y1 = luaL_checkinteger(L, 4);
    int x2 = luaL_checkinteger(L, 5);
    int y2 = luaL_checkinteger(L, 6);
    display.fillTriangle(x0, y0, x1, y1, x2, y2, EPD_FG());
    return 0;
}

static int l_screen_getWidth(lua_State *L) {
    lua_pushinteger(L, display.width());
    return 1;
}

static int l_screen_getHeight(lua_State *L) {
    lua_pushinteger(L, display.height());
    return 1;
}

static int l_screen_setTextSize(lua_State *L) {
    int s = luaL_checkinteger(L, 1);
    display.setTextSize(s);
    return 0;
}

static int l_screen_getTextWidth(lua_State *L) {
    const char* text = luaL_checkstring(L, 1);
    if (lua_gettop(L) >= 2) {
        int s = luaL_checkinteger(L, 2);
        display.setTextSize(s);
    }
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
    lua_pushinteger(L, (int)w);
    return 1;
}

// --- Delay (for Lua loop / waitForKey) ---
static int l_delay_ms(lua_State *L) {
    int ms = luaL_checkinteger(L, 1);
    if (ms > 0 && ms <= 10000) delay(ms);
    return 0;
}

// --- File read/write (LittleFS + SD) ---
static int l_file_read(lua_State *L) {
    const char* path = luaL_checkstring(L, 1);
    if (path[0] == '\0') {
        lua_pushnil(L);
        return 1;
    }
    String p = (path[0] == '/') ? String(path) : String("/") + path;
    if (LittleFS.exists(p.c_str())) {
        File f = LittleFS.open(p.c_str(), "r");
        if (f) {
            String content = f.readString();
            f.close();
            lua_pushstring(L, content.c_str());
            return 1;
        }
    }
    if (sdReady && SD.exists(p.c_str())) {
        File f = SD.open(p.c_str(), "r");
        if (f) {
            String content = f.readString();
            f.close();
            lua_pushstring(L, content.c_str());
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int l_file_write(lua_State *L) {
    const char* path = luaL_checkstring(L, 1);
    const char* content = luaL_checkstring(L, 2);
    if (path[0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    String p = (path[0] == '/') ? String(path) : String("/") + path;
    File f = LittleFS.open(p.c_str(), "w");
    if (f) {
        f.print(content);
        f.close();
        lua_pushboolean(L, 1);
        return 1;
    }
    if (sdReady) {
        f = SD.open(p.c_str(), "w");
        if (f) {
            f.print(content);
            f.close();
            lua_pushboolean(L, 1);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// --- List files in directory (for Apps-app: Intern / SD) ---
static int l_file_list_flash(lua_State *L) {
    const char* path = luaL_checkstring(L, 1);
    String p = (path[0] == '/') ? String(path) : String("/") + path;
    lua_newtable(L);
    int idx = 1;
    File root = LittleFS.open(p.c_str());
    if (!root || !root.isDirectory()) return 1;
    File file = root.openNextFile();
    while (file) {
        String n = file.name();
        int lastSlash = n.lastIndexOf('/');
        String base = (lastSlash >= 0) ? n.substring(lastSlash + 1) : n;
        if (base.endsWith(".lua")) {
            lua_pushstring(L, base.c_str());
            lua_rawseti(L, -2, idx++);
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();
    return 1;
}

static int l_file_list_sd(lua_State *L) {
    const char* path = luaL_checkstring(L, 1);
    lua_newtable(L);
    int idx = 1;
    if (!sdReady) return 1;
    String p = (path[0] == '/') ? String(path) : String("/") + path;
    File root = SD.open(p.c_str());
    if (!root || !root.isDirectory()) return 1;
    File file = root.openNextFile();
    while (file) {
        String n = file.name();
        file.close();
        int lastSlash = n.lastIndexOf('/');
        String base = (lastSlash >= 0) ? n.substring(lastSlash + 1) : n;
        if (base.endsWith(".lua")) {
            lua_pushstring(L, base.c_str());
            lua_rawseti(L, -2, idx++);
        }
        file = root.openNextFile();
    }
    root.close();
    return 1;
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

static int l_display_get_inverted(lua_State *L) {
    lua_pushboolean(L, displayInverted ? 1 : 0);
    return 1;
}
static int l_display_set_inverted(lua_State *L) {
    displayInverted = lua_toboolean(L, 1);
    // Software-inversjon: EPD_FG/EPD_BG oppdateres automatisk, redraw trengs
    return 0;
}
static int l_display_get_refresh_count(lua_State *L) {
    lua_pushinteger(L, refreshCount);
    return 1;
}
static int l_display_set_refresh_count(lua_State *L) {
    int n = (int)luaL_checkinteger(L, 1);
    if (n >= 1 && n <= 3) refreshCount = n;
    return 0;
}
static int l_display_get_partial(lua_State *L) {
    lua_pushboolean(L, partialRefreshEnabled ? 1 : 0);
    return 1;
}
static int l_display_set_partial(lua_State *L) {
    partialRefreshEnabled = lua_toboolean(L, 1);
    return 0;
}
static int l_display_save_settings(lua_State *L) {
    saveDisplaySettings();
    return 0;
}

// --- WiFi Bindings ---
static int l_wifi_connect(lua_State *L) {
    const char* ssid = luaL_checkstring(L, 1);
    const char* pass = luaL_checkstring(L, 2);
    WiFi.begin(ssid, pass);
    return 0;
}

static int l_wifi_status(lua_State *L) {
    lua_pushstring(L, (WiFi.status() == WL_CONNECTED) ? "CONNECTED" : "DISCONNECTED");
    return 1;
}

static int l_wifi_get_ip(lua_State *L) {
    if (WiFi.status() == WL_CONNECTED) {
        lua_pushstring(L, WiFi.localIP().toString().c_str());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

// --- System (for terminal / sysinfo) ---
static int l_system_get_heap(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)ESP.getFreeHeap());
    return 1;
}

static int l_system_uptime_ms(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)millis());
    return 1;
}

// --- TCP client (for connect/telnet) ---
static int l_tcp_connect(lua_State *L) {
    const char* host = luaL_checkstring(L, 1);
    int port = (int)luaL_optinteger(L, 2, 23);
    if (tcpClient.connected()) tcpClient.stop();
    bool ok = tcpClient.connect(host, (uint16_t)port, 5000);
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

static int l_tcp_send(lua_State *L) {
    const char* str = luaL_checkstring(L, 1);
    if (tcpClient.connected()) {
        tcpClient.print(str);
        lua_pushboolean(L, 1);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

static int l_tcp_available(lua_State *L) {
    lua_pushinteger(L, (lua_Integer)tcpClient.available());
    return 1;
}

static int l_tcp_read(lua_State *L) {
    int n = (int)luaL_optinteger(L, 1, 1);
    if (n <= 0 || !tcpClient.connected()) {
        lua_pushstring(L, "");
        return 1;
    }
    String s;
    while (n-- > 0 && tcpClient.available()) {
        char c = (char)tcpClient.read();
        s += c;
    }
    lua_pushstring(L, s.c_str());
    return 1;
}

static int l_tcp_connected(lua_State *L) {
    lua_pushboolean(L, tcpClient.connected() ? 1 : 0);
    return 1;
}

static int l_tcp_stop(lua_State *L) {
    tcpClient.stop();
    return 0;
}

// --- Keyboard Binding (langtrykk = LONG_ENTER for å kjøre kommando) ---
static int l_keyboard_getKey(lua_State *L) {
    static uint8_t lastBtn = HIGH;
    static uint32_t pressStart = 0;
    int btn = digitalRead(39);
    if (btn == LOW && lastBtn == HIGH) {
        pressStart = millis();
    }
    if (btn == HIGH && lastBtn == LOW) {
        lastBtn = HIGH;
        uint32_t duration = millis() - pressStart;
        if (duration >= 5000) {
            lua_pushstring(L, "LONG_ENTER_5SEC");
        } else if (duration >= 800) {
            lua_pushstring(L, "LONG_ENTER");
        } else {
            lua_pushstring(L, "ENTER");
        }
        return 1;
    }
    lastBtn = btn;
    lua_pushnil(L);
    return 1;
}

// Laster og kjører bootstrap_core.lua fra C (mindre Lua-stack enn require fra Lua). Setter _G._core = retur-tabell.
static int l_load_app_core(lua_State *L) {
    File f;
    if (littlefsReady) f = LittleFS.open("/bootstrap_core.lua", "r");
    if (!f && spiffsReady) f = SPIFFS.open("/bootstrap_core.lua", "r");
    if (!f || f.size() == 0 || (size_t)f.size() >= LUA_LOADER_BUF_SIZE) {
        if (f) f.close();
        lua_pushboolean(L, 0);
        return 1;
    }
    size_t n = f.read((uint8_t*)lua_loader_buf, LUA_LOADER_BUF_SIZE - 1);
    f.close();
    if (n == 0) { lua_pushboolean(L, 0); return 1; }
    lua_loader_buf[n] = '\0';
    if (luaL_loadbuffer(L, lua_loader_buf, n, "@bootstrap_core.lua") != LUA_OK) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_gc(L, LUA_GCCOLLECT, 0);
    if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
        lua_pop(L, 1);
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_setglobal(L, "_core");
    lua_pushboolean(L, 1);
    return 1;
}

// --- Custom Lua Loader (Searcher) ---
// This allows 'require' to work with LittleFS and SD card
// Sett til 1 for loader-debug, 1 for heap-tall ved hver require (Serial)
#define LUA_LOADER_DEBUG 0
#define LUA_HEAP_DEBUG 1

#define LUA_LOADER_BUF_SIZE 4096
static char lua_loader_buf[LUA_LOADER_BUF_SIZE];

static int l_vfs_loader(lua_State *L) {
    const char* name = luaL_checkstring(L, 1);
    char path[64];
    
    // Frigjør Lua-garbage før vi laster (config brukte minne – gi heap til neste modul)
    lua_gc(L, LUA_GCCOLLECT, 0);
    
    // Convert '.' to '/' for Lua module paths (e.g. hal.screen -> hal/screen)
    String modulePath = String(name);
    modulePath.replace(".", "/");
    
#if LUA_LOADER_DEBUG
    Serial.printf("[Loader] require('%s') -> path base '%s'\n", name, modulePath.c_str());
    Serial.printf("[Loader] littlefsReady=%d heap=%u\n", (int)littlefsReady, (unsigned)ESP.getFreeHeap());
#endif
#if defined(LUA_HEAP_DEBUG) && LUA_HEAP_DEBUG
    Serial.printf("[Heap] require('%s'): %u bytes ledig\n", name, (unsigned)ESP.getFreeHeap());
#endif
    
    auto tryLoad = [L](FS& fs, const char* p) -> bool {
        File f = fs.open(p, "r");
#if LUA_LOADER_DEBUG
        Serial.printf("[Loader] open('%s') = %s, size = %d\n", p, f ? "OK" : "FAIL", f ? (int)f.size() : 0);
#endif
        if (!f || f.size() == 0) return false;
        size_t sz = (size_t)f.size();
        int loadOk = LUA_ERRERR;
        if (sz < LUA_LOADER_BUF_SIZE) {
            size_t n = f.read((uint8_t*)lua_loader_buf, sz);
            f.close();
            if (n > 0) {
                lua_loader_buf[n] = '\0';
                loadOk = luaL_loadbuffer(L, lua_loader_buf, n, p);
            }
        } else {
            String content = f.readString();
            f.close();
            loadOk = luaL_loadstring(L, content.c_str());
        }
#if LUA_LOADER_DEBUG
        if (loadOk != LUA_OK) Serial.printf("[Loader] load failed: %s\n", lua_tostring(L, -1));
#endif
        if (loadOk == LUA_OK) return true;
        lua_pop(L, 1);
        return false;
    };
    
    // Try LittleFS: /hal/screen.lua (åpne direkte – unngår exists() som noen ganger feiler på underkataloger)
    if (littlefsReady) {
        snprintf(path, sizeof(path), "/%s.lua", modulePath.c_str());
        if (tryLoad(LittleFS, path)) return 1;
        snprintf(path, sizeof(path), "%s.lua", modulePath.c_str());
        if (tryLoad(LittleFS, path)) return 1;
    }
    if (spiffsReady) {
        snprintf(path, sizeof(path), "/%s.lua", modulePath.c_str());
        if (tryLoad(SPIFFS, path)) return 1;
        snprintf(path, sizeof(path), "%s.lua", modulePath.c_str());
        if (tryLoad(SPIFFS, path)) return 1;
    }
    if (sdReady) {
        snprintf(path, sizeof(path), "/%s.lua", modulePath.c_str());
        if (tryLoad(SD, path)) return 1;
    }

    lua_pushfstring(L, "\n\tno file '%s' on Flash or SD", name);
    return 1;
}

void register_hal() {
    lua_newtable(L);
    
    // GPIO Bindings
    lua_pushcfunction(L, l_gpio_mode); lua_setfield(L, -2, "gpio_mode");
    lua_pushcfunction(L, l_gpio_write); lua_setfield(L, -2, "gpio_write");
    lua_pushcfunction(L, l_gpio_read); lua_setfield(L, -2, "gpio_read");
    
    // Screen Bindings
    lua_pushcfunction(L, l_screen_init); lua_setfield(L, -2, "screen_init");
    lua_pushcfunction(L, l_screen_register_draw); lua_setfield(L, -2, "screen_register_draw");
    lua_pushcfunction(L, l_screen_unregister_draw); lua_setfield(L, -2, "screen_unregister_draw");
    lua_pushcfunction(L, l_screen_clear); lua_setfield(L, -2, "screen_clear");
    lua_pushcfunction(L, l_screen_drawText); lua_setfield(L, -2, "screen_drawText");
    lua_pushcfunction(L, l_screen_setTextColor); lua_setfield(L, -2, "screen_setTextColor");
    lua_pushcfunction(L, l_screen_update); lua_setfield(L, -2, "screen_update");
    lua_pushcfunction(L, l_screen_drawHome); lua_setfield(L, -2, "screen_drawHome");
    lua_pushcfunction(L, l_screen_drawHomeWithMenu); lua_setfield(L, -2, "screen_drawHomeWithMenu");
    lua_pushcfunction(L, l_screen_drawLine); lua_setfield(L, -2, "screen_drawLine");
    lua_pushcfunction(L, l_screen_fillCircle); lua_setfield(L, -2, "screen_fillCircle");
    lua_pushcfunction(L, l_screen_drawCircle); lua_setfield(L, -2, "screen_drawCircle");
    lua_pushcfunction(L, l_screen_fillRect); lua_setfield(L, -2, "screen_fillRect");
    lua_pushcfunction(L, l_screen_drawRect); lua_setfield(L, -2, "screen_drawRect");
    lua_pushcfunction(L, l_screen_fillTriangle); lua_setfield(L, -2, "screen_fillTriangle");
    lua_pushcfunction(L, l_screen_getWidth); lua_setfield(L, -2, "screen_getWidth");
    lua_pushcfunction(L, l_screen_getHeight); lua_setfield(L, -2, "screen_getHeight");
    lua_pushcfunction(L, l_screen_setTextSize); lua_setfield(L, -2, "screen_setTextSize");
    lua_pushcfunction(L, l_screen_getTextWidth); lua_setfield(L, -2, "screen_getTextWidth");

    // Display settings (invert, partial refresh, refresh count)
    lua_pushcfunction(L, l_display_get_inverted); lua_setfield(L, -2, "display_get_inverted");
    lua_pushcfunction(L, l_display_set_inverted); lua_setfield(L, -2, "display_set_inverted");
    lua_pushcfunction(L, l_display_get_refresh_count); lua_setfield(L, -2, "display_get_refresh_count");
    lua_pushcfunction(L, l_display_set_refresh_count); lua_setfield(L, -2, "display_set_refresh_count");
    lua_pushcfunction(L, l_display_get_partial); lua_setfield(L, -2, "display_get_partial");
    lua_pushcfunction(L, l_display_set_partial); lua_setfield(L, -2, "display_set_partial");
    lua_pushcfunction(L, l_display_save_settings); lua_setfield(L, -2, "display_save_settings");

    // Delay
    lua_pushcfunction(L, l_delay_ms); lua_setfield(L, -2, "delay_ms");

    // File read/write
    lua_pushcfunction(L, l_file_read); lua_setfield(L, -2, "file_read");
    lua_pushcfunction(L, l_file_write); lua_setfield(L, -2, "file_write");
    lua_pushcfunction(L, l_file_list_flash); lua_setfield(L, -2, "file_list_flash");
    lua_pushcfunction(L, l_file_list_sd); lua_setfield(L, -2, "file_list_sd");

    // WiFi Bindings
    lua_pushcfunction(L, l_wifi_connect); lua_setfield(L, -2, "wifi_connect");
    lua_pushcfunction(L, l_wifi_status); lua_setfield(L, -2, "wifi_status");
    lua_pushcfunction(L, l_wifi_get_ip); lua_setfield(L, -2, "wifi_get_ip");

    // System
    lua_pushcfunction(L, l_system_get_heap); lua_setfield(L, -2, "system_get_heap");
    lua_pushcfunction(L, l_system_uptime_ms); lua_setfield(L, -2, "system_uptime_ms");

    // TCP client
    lua_pushcfunction(L, l_tcp_connect); lua_setfield(L, -2, "tcp_connect");
    lua_pushcfunction(L, l_tcp_send); lua_setfield(L, -2, "tcp_send");
    lua_pushcfunction(L, l_tcp_available); lua_setfield(L, -2, "tcp_available");
    lua_pushcfunction(L, l_tcp_read); lua_setfield(L, -2, "tcp_read");
    lua_pushcfunction(L, l_tcp_connected); lua_setfield(L, -2, "tcp_connected");
    lua_pushcfunction(L, l_tcp_stop); lua_setfield(L, -2, "tcp_stop");

    // Keyboard Binding
    lua_pushcfunction(L, l_keyboard_getKey); lua_setfield(L, -2, "keyboard_getKey");

    // Laster bootstrap_core.lua fra C (unngår dybde/krasj ved require fra Lua)
    lua_pushcfunction(L, l_load_app_core); lua_setfield(L, -2, "load_app_core");
    
    lua_setglobal(L, "HAL");

    // Register custom file loader: Lua 5.2+ uses package.searchers, Lua 5.1 uses package.loaders
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "searchers");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_getfield(L, -1, "loaders");
    }
    if (!lua_isnil(L, -1)) {
        lua_pushcfunction(L, l_vfs_loader);
        lua_rawseti(L, -2, 2);
    }
    lua_pop(L, 2);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n--- RoseBox Boot ---");

  // Start WiFi-driver tidlig (unngår "create wifi task: failed to create queue" ved senere WiFi.begin fra BLE)
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);

  // false = aldri formatér – ellers kan opplastede filer (Sketch Data Upload) slettes ved mount-feil
  littlefsReady = LittleFS.begin(false);
  if (littlefsReady) {
    Serial.println("LittleFS: mounted");
    Serial.println("  /bootstrap.lua: " + String(LittleFS.exists("/bootstrap.lua") ? "finnes" : "MANGLER"));
    Serial.println("  /main.lua: " + String(LittleFS.exists("/main.lua") ? "finnes" : "MANGLER"));
    Serial.println("  /config.lua: " + String(LittleFS.exists("/config.lua") ? "finnes" : "MANGLER"));
    Serial.println("  /hal/screen.lua: " + String(LittleFS.exists("/hal/screen.lua") ? "finnes" : "MANGLER"));
  } else {
    Serial.println("LittleFS: mount failed (velg samme Partition Scheme som ved LittleFS-opplasting, f.eks. Huge APP)");
  }
  spiffsReady = SPIFFS.begin(false);
  if (spiffsReady) Serial.println("SPIFFS mounted");
  if (littlefsReady && writeEmbeddedLuaIfMissing()) {
    Serial.println("LittleFS: wrote default Lua scripts (first boot)");
  }

  sdReady = SD.begin(SD_CS);
  if (!sdReady) {
    Serial.println("SD card fail (CS 13)");
  }

  // Init display: boot med logo + prikker (*, **, ***). Full skjerm 0,0 så ingen åpning på venstre.
  display.init(115200, true, 2, false);
  display.setRotation(1);
  loadDisplaySettings();
  loadWiFiConfig();
  initBLE();  // må startes tidlig – ESP32 BLE-biblioteket krasjer (StoreProhibited) hvis det kalles etter Lua
  pinMode(39, INPUT);  // Knapp (LilyGo T5) – må settes her så bootstrap får tastetrykk uten Lua GPIO:setup()
  display.setTextColor(EPD_FG());

  const int screenW = display.width();
  const int screenH = display.height();
  const int bootW = 80, bootH = 50;
  const int bootX = (screenW - bootW) / 2;
  const int bootY = (screenH - bootH) / 2 - 8;
  const int dotY = bootY + bootH + 8;
  const int dotSpacing = 16;
  const int dotRadius = 3;
  const int dotLeftX = bootX + (bootW - (2 * dotSpacing)) / 2;
  const int bootDotDelayMs = 150;

  // Hjelpe-lambda: tegne hele boot-skjermen (logo + n fylte prikker: 1=*, 2=**, 3=***)
  auto drawBootScreen = [&](int numDots) {
    display.fillRect(0, 0, screenW, screenH, EPD_BG());
    display.drawBitmap(bootX, bootY, epd_bitmap_boot_logo, bootW, bootH, EPD_FG());
    for (int i = 0; i < 3; i++) {
      int cx = dotLeftX + i * dotSpacing;
      if (i < numDots) {
        display.fillCircle(cx, dotY, dotRadius, EPD_FG());
      } else {
        display.drawCircle(cx, dotY, dotRadius, EPD_FG());
      }
    }
  };

  // Init Lua i små steg mens vi animerer, så animasjonen går til alt er klart
  int initStep = 0;
  const int totalFrames = 18;  // lengre animasjon før main.lua kjører
  bool mainExists = false;

  for (int frame = 0; frame < totalFrames; frame++) {
    int numDots = (frame % 3) + 1;  // *, **, ***, *, **, ***

    if (frame == 0) {
      display.setFullWindow();
    } else {
      display.setPartialWindow(0, 0, screenW, screenH);
    }
    display.firstPage();
    do {
        yield();
        drawBootScreen(numDots);
    } while (display.nextPage());
    // nextPage() gjør refresh – ingen ekstra display() nødvendig

    if (initStep == 0) {
      L = luaL_newstate();
      initStep = 1;
    } else if (initStep == 1) {
      luaL_openlibs(L);
      initStep = 2;
    } else if (initStep == 2) {
      register_hal();
      initStep = 3;
    } else if (initStep == 3) {
      luaL_dostring(L, "package.path = '/?.lua;/hal/?.lua;/sd/apps/?.lua;/sd/?.lua'");
      initStep = 4;
    } else if (initStep == 4) {
      mainExists = (littlefsReady && (LittleFS.exists("/bootstrap.lua") || LittleFS.exists("/main.lua")))
                   || (spiffsReady && (SPIFFS.exists("/bootstrap.lua") || SPIFFS.exists("/main.lua")));
      initStep = 5;
    }

    delay(bootDotDelayMs);
  }

  // Minimal boot: last og kjør bootstrap manuelt slik at vi kan kjøre GC mellom lasting og kjøring (sparer RAM).
  if (L != nullptr && mainExists) {
#if defined(LUA_HEAP_DEBUG) && LUA_HEAP_DEBUG
    Serial.printf("[Heap] før bootstrap: %u bytes ledig\n", (unsigned)ESP.getFreeHeap());
#endif
    bool bootstrapOk = false;
    File f;
    if (littlefsReady) f = LittleFS.open("/bootstrap.lua", "r");
    if (!f && spiffsReady) f = SPIFFS.open("/bootstrap.lua", "r");
    if (f && f.size() > 0 && (size_t)f.size() < LUA_LOADER_BUF_SIZE) {
      size_t n = f.read((uint8_t*)lua_loader_buf, LUA_LOADER_BUF_SIZE - 1);
      f.close();
      if (n > 0) {
        lua_loader_buf[n] = '\0';
        if (luaL_loadbuffer(L, lua_loader_buf, n, "@bootstrap.lua") == LUA_OK) {
          lua_gc(L, LUA_GCCOLLECT, 0);  // frigjør minne fra loadbuffer før vi kjører chunken
#if defined(LUA_HEAP_DEBUG) && LUA_HEAP_DEBUG
          Serial.printf("[Heap] etter load, før run: %u bytes ledig\n", (unsigned)ESP.getFreeHeap());
#endif
          if (lua_pcall(L, 0, 0, 0) == LUA_OK) {
            bootstrapOk = true;
          }
        }
        if (!bootstrapOk && lua_gettop(L) > 0) lua_pop(L, 1);
      }
    }
    if (!bootstrapOk) {
      lua_gc(L, LUA_GCCOLLECT, 0);
      lua_getglobal(L, "require");
      lua_pushstring(L, "bootstrap");
      if (lua_pcall(L, 1, 0, 0) == LUA_OK) {
        bootstrapOk = true;
      } else {
        Serial.print("Bootstrap failed: ");
        Serial.println(lua_tostring(L, -1));
        lua_pop(L, 1);
      }
    }
    if (bootstrapOk) {
      luaReady = true;
    } else {
      lua_getglobal(L, "require");
      lua_pushstring(L, "main");
      if (lua_pcall(L, 1, 0, 0) == LUA_OK) {
        luaReady = true;
      } else {
        Serial.print("Lua Error (main): ");
        Serial.println(lua_tostring(L, -1));
        lua_pop(L, 1);
      }
    }
  } else if (L != nullptr && !mainExists) {
    Serial.println("bootstrap.lua / main.lua not found on LittleFS");
  }
}

void loop() {
  if (!luaReady || L == nullptr) {
    delay(100);
    return;
  }
  handleBLEConnection();
  lua_getglobal(L, "loop");
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK) {
      Serial.println(lua_tostring(L, -1));
      lua_pop(L, 1);
    }
  } else {
    lua_pop(L, 1);
  }
  delay(10);
}
