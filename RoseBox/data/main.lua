-- RoseBox Main Entry Point
-- Lazy loading: hal.storage og hal.wifi lastes IKKE ved oppstart. De lastes når en app trenger dem
-- (Terminal/Settings krever hal.wifi). Vi krasjer ikke – ved lav minne feiler require i appen og vi viser feil.
local config = require("config")

print("RoseBox Booting...")

-- Kun nødvendige HAL-moduler ved oppstart (screen, keyboard, gpio).
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local GPIO = require("hal.gpio")

-- Initialize Hardware
print("Initializing HAL...")
Screen:init()
Keyboard:init()
GPIO:setup()

print("System Ready.")

-- Start Launcher: hjem-skjerm med app-ikoner (Terminal, Clock, Settings). Kort trykk = bytt app, Lang trykk = åpne app.
local Launcher = require("apps.launcher")
Launcher:start()

-- Global: kall fra apper (f.eks. Terminal/Settings) for å koble til WiFi ved første bruk.
function ensureWiFi()
    if _G._wifi_ensured then return end
    _G._wifi_ensured = true
    local ok, WiFi = pcall(require, "hal.wifi")
    if ok and config.wifi_ssid and #config.wifi_ssid > 0 then
        WiFi:connect(config.wifi_ssid, config.wifi_pass or "")
    end
end

function loop()
    Launcher:loop()
end
