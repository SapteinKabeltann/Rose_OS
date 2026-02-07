# RoseOS for ESP32 LilyGo T5 E-Paper

A lightweight operating system for LilyGo T5 2.13" e-paper display.

**Native C++-apper (v1.0.1+):** Alle apper er hardkodet i flash som C++. Hjemskjerm, Terminal, Clock, Settings og Apps kjører uten Lua – minimal heap, ingen `dofile` eller Lua-kompilering. BLE, skjerm, SD og LittleFS initialiseres én gang i `setup()`.

## 📁 Folder Structure

```
RoseBox/
├── RoseBox.ino         # Firmware: display, BLE, SD, LittleFS + native app-loop
├── App.h               # App-struktur (setup/loop/name), launchApp(), apps[]
├── data/               # Valgfritt: Lua-filer brukes ikke lenger av kjernen
│   └── apps/           # (SD .lua vises i Apps-app som liste, kjøres ikke)
└── web/                # Valgfritt WiFi-kontrollpanel
```

**Apper (C++):** Home (launcher), Terminal, Clock, Settings, Apps. Bytt med kort trykk på hjemskjerm, lang trykk for å åpne. `launchApp(0)` = tilbake til hjemskjerm.

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
2. Dette laster opp **data/** til flash: **core.lua**, **launcher.lua**, **config.lua**, **hal/*.lua**, **apps/*.lua**. (bootstrap.lua og main.lua brukes som fallback.)
3. RoseBox leser fra **LittleFS og SPIFFS**. Ved LittleFS-opplasting:
   - Velg **samme Partition Scheme** for sketch og data (f.eks. Huge APP).
   - Ved oppstart vises om `/core.lua`, `/launcher.lua`, `/config.lua`, `/hal/screen.lua` **finnes** eller **MANGLER**.
   - `LittleFS.begin(false)` – partisjonen formateres ikke ved oppstart.

**Modulær boot:**  
- **Core** (`core.lua`): lastes først, forblir i RAM. Filystem/skjerm/input/BLE er satt opp i C++ (setup).  
- **Launcher** (`launcher.lua`): lastes fra C i første `loop()` (HAL.load_launcher), viser app-ikoner, starter app ved Lang trykk. Core + launcher er alltid i RAM.  
- **Apper**: lastes kun når brukeren åpner dem; ved avslutning fjernes de fra `package.loaded` og `collectgarbage()` kjøres.  

Fallback: hvis `core.lua` mangler, prøves `bootstrap.lua`, deretter `main.lua`.

**Hvis du får «not enough memory» ved lasting av launcher:** Launcher kompileres fra kildekode og bruker mye heap. Løsning: last opp **forhåndskompilert bytecode** i stedet. På PC (samme Lua-versjon som firmware, typisk 5.1):
```bash
luac -o launcher.luac launcher.lua
```
Legg **launcher.luac** i **data/** og kjør **Tools → ESP32 Sketch Data Upload**. Firmware prøver `.luac` først og bruker da mye mindre minne.  
For generell heap-debug: sett `LUA_HEAP_DEBUG 1` i RoseBox.ino for å se ledig heap.

### Optimalisering (heap, BLE, .luac) – anbefalt workflow

| Mål | Hvordan |
|-----|--------|
| **Mindre heap** | Kompiler Lua til `.luac` (bytecode). `require("modul")` prøver automatisk `.luac` før `.lua`. |
| **Kompilere alt** | Kjør `scripts\compile_lua.bat` (Windows). Krever Lua 5.1 `luac` i PATH. Last deretter opp **data/** med Sketch Data Upload. |
| **Store moduler** | Legg store `.luac` på **SD-kort**, små config på **LittleFS**. Last fra Lua: `HAL.dofile("/sd/scripts/stor_modul.luac")`. |
| **Modulær lasting** | Last kun det som trengs: `local display = require("hal.screen")`. Avlast med `HAL.collect_garbage()` etter store operasjoner. |
| **BLE stabil** | Firmware bruker allerede små BLE-pakker og prosesserer i hovedloop (ikke i BLE-callback). Unngå tung jobb i BLE-callback fra Lua. |
| **E-paper** | Partiell oppdatering er på. Unngå store bitmap-tabeller i Lua – bruk C-side eller buffer på SD. |
| **Debugging** | Logg til SD i stedet for Serial: `HAL.debug_log("melding")` skriver til `/debug.log` (SD eller flash). |

**Legge til nye Lua-apper:** 1) Lag `data/apps/minapp.lua` med `:start()` og `:loop()` (se `clock.lua`). 2) Legg `"minapp"` inn i **`data/launcher.lua`**: `_G.appList = { "terminal", "clock", "settings", "apps", "minapp" }`. 3) Last opp data. Appen lastes kun ved åpning og frigjøres ved lukking.

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