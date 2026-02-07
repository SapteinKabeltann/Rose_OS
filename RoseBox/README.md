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

1. Open **RoseBox.ino** i Arduino IDE
2. Installer nødvendige biblioteker (se under)
3. Velg kort: **ESP32 Dev Module**
4. Velg riktig COM-port
5. Gå til **Tools → Partition Scheme → Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)** eller **Big App (No OTA)** (avhengig av board package – LittleFS må ha plass)
6. **Upload** (kompiler og last opp sketch)

### 3. Last opp Lua-filer til Flash – viktig

Uten dette får du feilmeldingen **«module 'hal.screen' not found»**.

1. I Arduino IDE: **Tools → ESP32 Sketch Data Upload** (eller **LittleFS Data Upload** / **SPIFFS Data Upload** avhengig av board/plugin).
2. Dette laster opp mappen **data/** til flash: **bootstrap.lua** (én fil: hjem + åpne/lukke app), **main.lua**, **hal/*.lua**, **apps/*.lua**, **config.lua**.
3. RoseBox leser fra **både LittleFS og SPIFFS**. Ved **LittleFS Filesystem Upload** (f.eks. earlephilhower som bruker `huge_app`):
   - **Viktig:** Velg **samme Partition Scheme** når du bygger sketch som når du kjører LittleFS-opplasting. I Arduino IDE: **Tools → Partition Scheme** – velg det som tilsvarer `huge_app` (f.eks. **«Huge APP (3MB No OTA/1MB SPIFFS)»** eller liknende). Bygg og last opp **sketch** med dette valget, deretter **Tools → ESP32 LittleFS Data Upload**.
   - Ved oppstart skriver RoseBox til Serial om LittleFS er montert og om `/bootstrap.lua`, `/main.lua`, `/config.lua`, `/hal/screen.lua` **finnes** eller **MANGLER**. Ser du «LittleFS: mount failed» eller «MANGLER», bygg sketch på nytt med riktig Partition Scheme og last opp sketch + LittleFS igjen.
   - Koden bruker `LittleFS.begin(false)` så partisjonen aldri formateres ved oppstart.

**Minimal boot:** Ved oppstart kjører RoseBox **bootstrap.lua** (én fil med hjem-meny, åpne/lukke app). Ved **Lang trykk** lastes kun den valgte appen (f.eks. `require("apps.terminal")`) – ingen ekstra bootstrap_core. BLE og WiFi (C++) er aktive fra setup(). Hvis bootstrap feiler, faller firmware tilbake til **main.lua**.  

**Hvis du får «not enough memory»:** Det er **RAM (heap)** som er tom. Med splittet bootstrap lastes minimalt ved boot; bootstrap_core og apper lastes on demand. For å se ledig heap: i `RoseBox.ino` sett `LUA_HEAP_DEBUG 1`.

**Legge til nye Lua-apper:** Du trenger ikke endre C++ eller bootstrap_core. 1) Lag `data/apps/minapp.lua` som returnerer en tabell med `:start()` og `:loop()` (se f.eks. `clock.lua`). 2) Legg `"minapp"` inn i listen i `data/bootstrap.lua`: `_G.appList = { "terminal", "clock", "settings", "apps", "minapp" }`. 3) Last opp data på nytt. Appen lastes først når brukeren åpner den; når de lukker, frigjøres minnet. Ny app = ingen ekstra minne ved oppstart.

### 4. SD-kort (valgfritt)

De innebygde appene (Terminal, Clock, Settings, Apps) fungerer **uten SD-kort** når Lua-filene er lastet opp til LittleFS (steg 3).

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
1. Open app and connect to device **RoseBox** (BLE-navn)
2. Send commands (see list below)

**Hvis du får GATT status 147:** Prøv «Forget device» / «Glem enhet» på telefonen og koble til på nytt. Enheten krever ikke paring.

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