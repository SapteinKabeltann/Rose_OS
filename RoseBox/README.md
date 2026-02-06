# RoseOS for ESP32 LilyGo T5 E-Paper

A lightweight operating system for LilyGo T5 2.13" e-paper display.

## 📁 Folder Structure

```
Rose_OS/                 # Project folder
├── RoseOS/             # Sketch folder
│   ├── RoseOS.ino      # Main firmware
│   ├── home.h          # Bitmap data for home screen
│   ├── icons.h         # Icons
│   └── web/            # Web interface for WiFi control
    └── apps/
        ├── Photo.lua       # Image gallery
        ├── clock.lua       # Clock app
        ├── notes.lua       # Notes
        └── settings.lua    # Settings
└── sd_card_files/      # Copy contents to SD card
```

## 🚀 Installation

### 1. Prepare folder names
1. Name the folder `RoseOS`
2. Name the main file `RoseOS.ino`

### 2. Flash firmware to ESP32

1. Open `RoseOS.ino` in Arduino IDE
2. Install required libraries (see below)
3. Select board: **ESP32 Dev Module**
4. Select correct COM port
5. Go to Tools > Partition Scheme > Big App (No OTA) !!!IMPORTANT!!!
6. Upload

### 3. Prepare SD card (optional)

**Note:** The 5 built-in apps work **without SD card**!

SD card is only needed for:
- Photo app (to display images from `/images/`)
- Running custom Lua scripts via the Apps app

## 📚 Required Arduino Libraries

- **GxEPD2** by Jean-Marc Zingg  
  *(Sketch → Include Library → Manage Libraries → søk "GxEPD2")*

- **Lua for ESP32** – tolker som kjører .lua-filene fra flash (LittleFS) og SD.  
  **Lua finnes ikke i Library Manager.** Du må bruke en av metodene under.

  **⚠️ Nicholas3388/LuaNode fungerer IKKE** som bibliotek til RoseBox. LuaNode er en **helt egen firmware** (bygget med ESP-IDF, egen partisjon og oppstart). Den er ikke et Arduino-bibliotek du kan legge til i en sketch – du bygger enten LuaNode-firmware eller Arduino-sketch, ikke begge.

  **Anbefalt: lua511-esp32** (Lua 5.1 for ESP32)
  Installer biblioteket slik at Arduino IDE finner det (én gang):
  - **Fra GitHub:** Clone eller last ned [lua511-esp32](https://github.com/sapteinkabeltann/lua511-esp32) og legg mappen i `Arduino/libraries/`, eller bruk **Sketch → Include Library → Add .ZIP Library** og velg den nedlastede ZIP-en.
  - Lua-kilden er inkludert i repoet, så du trenger ikke kjøre noe script. Når biblioteket står i `libraries/`, bygger RoseBox mot det.

  **Alternativ:** [inajob/lua-in-arduino-esp32](https://github.com/inajob/lua-in-arduino-esp32) kjører Lua på arduino-esp32, men prosjektet er bygget for **PlatformIO** (ikke Arduino IDE med "Add .ZIP Library"). Du kan bruke det i PlatformIO, eller kopiere `lib/`-innholdet fra det repoet inn i et eget Arduino-bibliotek som over.
- **ESP32 BLE Arduino** (built into ESP32 core)
- **WebServer** (built into ESP32 core)

## 📖 Hardware / videre utvikling

- **DFRobot 2.13" e-ink (DFR0676)** – wiki med spec, pinout og eksempler:  
  [wiki.dfrobot.com/e-Ink_Display_Module_for_ESP32_SKU_DFR0676](https://wiki.dfrobot.com/e-Ink_Display_Module_for_ESP32_SKU_DFR0676)  
  Matcher RoseBox/LilyGo T5: 250×122, E-Paper CS=5, SD CS=13, knapp IO39, GDEH0213B72.

## 📱 Features

- **Threaded Boot:** Fast startup with animated logo
- **WiFi Web Controller:** Control the device via browser on the same network
- **BLE Controller:** Control via Web Bluetooth
- **Smart Partial Refresh:** Fast updates with contrast reinforcement
- **Lua Interpreter:** Run custom scripts from SD card
- **Battery Optimized:** For LiPo (3.0V-4.0V)

## 🎮 Navigation

| Action | Result |
|--------|--------|
| **Short press** | Next app / App action |
| **Long press** | Launch app / Exit app |
| **Press over 5 seconds** | Save settings and exit settings |

## 📱 BLE Controller (Recommended App)

To send commands via Bluetooth (e.g. for WiFi setup), I recommend **Serial Bluetooth Terminal** by Kai Morich.

- [Download for Android (Google Play)](https://play.google.com/store/apps/details?id=de.kai_morich.serial_bluetooth_terminal)

**Usage:**
1. Open app and connect to device **RoseOS**
2. Send commands (see list below)

## 🌐 WiFi Web Controller

1. Connect ESP32 to WiFi via BLE (send `CMD:WIFI:SSID:name` and `CMD:WIFI:PASS:password` and send `CMD:WIFI:CONNECT` to connect to WiFi)
2. View the IP address in the **Settings** app
3. **Option A:** Open the IP address directly in browser (e.g. `http://192.168.1.100`)
4. **Option B:** Run local web controller:
   ```bash
   cd RoseOS/web
   npm install
   npm run dev
   ```
   Open `http://localhost:5050` and enter the ESP32's IP

## 📡 Commands (BLE & WiFi)

| Command | Description |
|---------|-------------|
| `CMD:WIFI:SSID:name` | Set WiFi name |
| `CMD:WIFI:PASS:password` | Set WiFi password |
| `CMD:BUTTON` | Simulate short press |
| `CMD:BUTTON:LONG` | Simulate long press |
| `CMD:APP:EXIT` | Exit app |
| `CMD:SYSTEM:INFO` | Get system information |

## ⚙️ Settings

The Settings app shows 3 adjustable settings + IP address:

| Setting | Description |
|---------|-------------|
| **Invert Display** | Swap black/white |
| **Refresh Count** | Number of refresh cycles (1-3) |
| **Partial Refresh** | Fast update (ON/OFF) |
| **IP Address** | For web control |

**Navigation in Settings:**
- Short press (<2s): Next setting
- Medium press (2-5s): Change value
- Long press (>5s): Save and exit

## 🔧 Troubleshooting

Open **Tools > Serial Monitor** (115200 baud) for debug info.

### Common problems:

| Problem | Solution |
|---------|----------|
| WiFi won't connect | Check SSID/password via BLE |
| Web controller can't connect | Check that ESP32 and PC are on the same network |
| Screen becomes gray | Partial refresh is normal, full refresh occurs every 15th update |
| It may also need a restart after WiFi is connected for the clock to work |

### SD card pins (LilyGo T5)

```cpp
#define SD_CS     13
#define SD_MOSI   15
#define SD_MISO   2
#define SD_SCK    14
```

## 📄 License

**Proprietary License / All Rights Reserved**

All rights reserved by Alexander Tornøe aka https://github.com/SapteinKabeltann.

The following terms apply:
- The program is for **personal use only**.
- It may **not** be sold or distributed in any way.
- It may **not** be used in commercial projects.
- It may **not** be modified or used in any form of projects without explicit permission from Alexander Tornøe aka https://github.com/SapteinKabeltann.

Contact for license or commercial use inquiries: alex@tornoee.com