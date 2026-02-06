-- RoseBox Main Entry Point
local config = require("config")

print("RoseBox Booting...")

-- Load HAL Modules
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local GPIO = require("hal.gpio")
local Storage = require("hal.storage")
local WiFi = require("hal.wifi")

-- Initialize Hardware
print("Initializing HAL...")
Screen:init()
Keyboard:init()
GPIO:setup()
Storage:init()

if config.wifi_ssid then
    WiFi:connect(config.wifi_ssid, config.wifi_pass or "")
end

print("System Ready.")

-- Start Launcher: hjem-skjerm med app-ikoner (Terminal, Clock, Settings). Kort trykk = bytt app, Lang trykk = åpne app.
local Launcher = require("apps.launcher")
Launcher:start()

-- Global loop(): kalles fra C++ loop(). Launcher håndterer tastatur og åpner/lukker apper.
function loop()
    Launcher:loop()
end
