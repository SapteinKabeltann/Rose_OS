// Embedded Lua scripts – skrives til LittleFS ved første oppstart hvis filer mangler.
// Unngår at bruker må kjøre "Sketch Data Upload" for at RoseBox skal starte.

#ifndef EMBEDDED_LUA_H
#define EMBEDDED_LUA_H

#include <LittleFS.h>
#include <Arduino.h>

// Skriver innebygde Lua-filer til LittleFS hvis /config.lua mangler (første oppstart).
// Bruk: include denne filen og kall writeEmbeddedLuaIfMissing() etter LittleFS.begin().

static bool writeEmbeddedLuaIfMissing() {
    bool wroteAny = false;
    auto writeIfMissing = [&wroteAny](const char* path, const char* content) {
        if (!LittleFS.exists(path)) {
            File f = LittleFS.open(path, "w");
            if (f) {
                f.print((const __FlashStringHelper*)content);
                f.close();
                wroteAny = true;
            }
        }
    };

    // Vi sjekker bare config – hvis den finnes, antas resten å være lastet opp
    if (LittleFS.exists("/config.lua")) return false;

    static const char c_config[] PROGMEM = R"EMBED(
return {
    screen_type = "monochrome_spi",
    keyboard_type = "matrix_4x4",
    gpio_pins = { led = 2, button = 39, sensor1 = 12, sensor2 = 13 },
    sd_cs_pin = 13,
    wifi_ssid = "RoseNet",
    wifi_pass = "SecretPassword",
    debug_mode = true
}
)EMBED";
    writeIfMissing("/config.lua", c_config);

    static const char c_bootstrap[] PROGMEM = R"EMBED(
_G.appList = { "terminal", "clock", "settings", "apps" }
_G.selectedIndex = 1
_G.currentApp = nil
_G.currentAppName = nil
local done = false
local function redraw()
  HAL.screen_drawHomeWithMenu(_G.appList, _G.selectedIndex)
end
function loop()
  if _G.currentApp then
    local c = _G._core
    if c then
      local a, b = _G.currentApp:loop()
      if a == "exit" then c.closeApp(); return end
      if a == "launch" and b then c.closeApp(); c.launchApp(b); return end
    end
    return
  end
  if not done then done = true; redraw(); print("Bootstrap: hjem klar") end
  local key = HAL.keyboard_getKey()
  if not key then return end
  if key == "LONG_ENTER" then
    if not _G._core then
      local ok = HAL.load_app_core()
      if not ok then print("load_app_core failed") return end
    end
    local n = _G.appList[_G.selectedIndex]
    if n then _G._core.launchApp(n) end
  else
    _G.selectedIndex = (_G.selectedIndex % #_G.appList) + 1
    redraw()
  end
end
)EMBED";
    writeIfMissing("/bootstrap.lua", c_bootstrap);

    static const char c_bootstrap_core[] PROGMEM = R"EMBED(
local function redraw()
  HAL.screen_drawHomeWithMenu(_G.appList, _G.selectedIndex)
end
local function launchApp(name)
  HAL.screen_unregister_draw()
  local mod = nil
  local ok, err = pcall(function() mod = require("apps." .. name) end)
  if not ok or not mod or not mod.start then
    print("Kunne ikke starte: " .. tostring(err))
    redraw()
    return
  end
  _G.currentApp = mod
  _G.currentAppName = name
  mod:start()
end
local function closeApp()
  if _G.currentApp and _G.currentApp.onExit then _G.currentApp:onExit() end
  if _G.currentAppName then
    package.loaded["apps." .. _G.currentAppName] = nil
    package.loaded["hal.screen"] = nil
    package.loaded["hal.keyboard"] = nil
    _G.currentAppName = nil
  end
  _G.currentApp = nil
  collectgarbage("collect")
  redraw()
end
function ensureWiFi()
  if _G._wifi_ensured then return end
  _G._wifi_ensured = true
  local ok, cfg = pcall(require, "config")
  if not ok or not cfg then return end
  local ok2, w = pcall(require, "hal.wifi")
  if ok2 and w and cfg.wifi_ssid and #cfg.wifi_ssid > 0 then
    w:connect(cfg.wifi_ssid, cfg.wifi_pass or "")
  end
end
return { launchApp = launchApp, closeApp = closeApp, ensureWiFi = ensureWiFi }
)EMBED";
    writeIfMissing("/bootstrap_core.lua", c_bootstrap_core);

    static const char c_main[] PROGMEM = R"EMBED(
-- RoseBox Main Entry Point
local config = require("config")
print("RoseBox Booting...")
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local GPIO = require("hal.gpio")
local Storage = require("hal.storage")
local WiFi = require("hal.wifi")
print("Initializing HAL...")
Screen:init()
Keyboard:init()
GPIO:setup()
Storage:init()
if config.wifi_ssid then WiFi:connect(config.wifi_ssid, config.wifi_pass or "") end
print("System Ready.")
local Launcher = require("apps.launcher")
Launcher:start()
function loop()
    Launcher:loop()
end
)EMBED";
    writeIfMissing("/main.lua", c_main);

    static const char c_hal_screen[] PROGMEM = R"EMBED(
local config = require("config")
local Screen = {}
function Screen:init()
    if config.screen_type == "monochrome_spi" then HAL.screen_init()
    else print("HAL: Unknown screen type: " .. tostring(config.screen_type)) end
end
function Screen:register_draw(f) HAL.screen_register_draw(f) end
function Screen:unregister_draw() HAL.screen_unregister_draw() end
function Screen:clear() HAL.screen_clear() end
function Screen:drawText(x, y, text) HAL.screen_drawText(x, y, text) end
function Screen:update() HAL.screen_update() end
function Screen:drawHome() HAL.screen_drawHome() end
return Screen
)EMBED";
    writeIfMissing("/hal/screen.lua", c_hal_screen);

    static const char c_hal_keyboard[] PROGMEM = R"EMBED(
local Keyboard = {}
function Keyboard:init() end
function Keyboard:getKey() return HAL.keyboard_getKey() end
function Keyboard:waitForKey()
    while true do local key = self:getKey() if key then return key end HAL.delay_ms(10) end
end
return Keyboard
)EMBED";
    writeIfMissing("/hal/keyboard.lua", c_hal_keyboard);

    static const char c_hal_gpio[] PROGMEM = R"EMBED(
local config = require("config")
local GPIO = {}
function GPIO:setup()
    for name, pin in pairs(config.gpio_pins) do
        local isInput = (name == "button") or (type(name) == "string" and name:match("^sensor"))
        HAL.gpio_mode(pin, isInput and 0 or 1)
    end
end
function GPIO:write(name, value) local pin = config.gpio_pins[name] if pin then HAL.gpio_write(pin, value) end end
function GPIO:read(name) local pin = config.gpio_pins[name] if pin then return HAL.gpio_read(pin) end return nil end
return GPIO
)EMBED";
    writeIfMissing("/hal/gpio.lua", c_hal_gpio);

    static const char c_hal_storage[] PROGMEM = R"EMBED(
local Storage = {}
function Storage:init() end
function Storage:readFile(path) return HAL.file_read(path) end
function Storage:writeFile(path, content) return HAL.file_write(path, content) end
return Storage
)EMBED";
    writeIfMissing("/hal/storage.lua", c_hal_storage);

    static const char c_hal_wifi[] PROGMEM = R"EMBED(
local WiFi = {}
function WiFi:connect(ssid, password) HAL.wifi_connect(ssid, password) end
function WiFi:getStatus() return HAL.wifi_status() end
function WiFi:getIP() return HAL.wifi_get_ip() end
return WiFi
)EMBED";
    writeIfMissing("/hal/wifi.lua", c_hal_wifi);

    static const char c_hal_display[] PROGMEM = R"EMBED(
local Display = {}
function Display.clear() HAL.screen_clear() end
function Display.refresh() HAL.screen_update() end
function Display.text(x, y, text, size) HAL.screen_setTextSize(size or 1) HAL.screen_drawText(x, y, text) end
function Display.line(x1, y1, x2, y2) HAL.screen_drawLine(x1, y1, x2, y2) end
function Display.circle(x, y, r, fill) if fill then HAL.screen_fillCircle(x, y, r) else HAL.screen_drawCircle(x, y, r) end end
function Display.rect(x, y, w, h, fill) if fill then HAL.screen_fillRect(x, y, w, h) else HAL.screen_drawRect(x, y, w, h) end end
function Display.triangle(x1,y1,x2,y2,x3,y3,fill) if fill then HAL.screen_fillTriangle(x1,y1,x2,y2,x3,y3) end end
function Display.width() return HAL.screen_getWidth() end
function Display.height() return HAL.screen_getHeight() end
function Display.textWidth(text, size) HAL.screen_setTextSize(size or 1) return HAL.screen_getTextWidth(text) end
display = Display
return Display
)EMBED";
    writeIfMissing("/hal/display.lua", c_hal_display);

    static const char c_launcher[] PROGMEM = R"EMBED(
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local Launcher = {}
Launcher.appList = { "terminal", "clock", "settings", "apps" }
Launcher.selectedIndex = 1
Launcher.currentApp = nil
function Launcher:start() self.selectedIndex = 1 self.currentApp = nil self:redraw() end
function Launcher:redraw() HAL.screen_drawHomeWithMenu(self.appList, self.selectedIndex) end
function Launcher:launchApp(name)
    HAL.screen_unregister_draw()
    local mod
    local ok, err = pcall(function()
        if name == "terminal" then mod = require("apps.terminal")
        elseif name == "clock" then mod = require("apps.clock")
        elseif name == "settings" then mod = require("apps.settings")
        elseif name == "apps" then mod = require("apps.apps")
        else mod = require("apps." .. name) end
    end)
    if not ok or not mod or not mod.start then return end
    self.currentApp = mod
    mod:start()
end
function Launcher:closeApp() self.currentApp = nil self:redraw() end
function Launcher:loop()
    if self.currentApp then
        local a, b = self.currentApp:loop()
        if a == "exit" then self:closeApp()
        elseif a == "launch" and b then self:closeApp() self:launchApp(b) end
        return
    end
    local key = Keyboard:getKey()
    if not key then return end
    if key == "LONG_ENTER" then local name = self.appList[self.selectedIndex] if name then self:launchApp(name) end
    else self.selectedIndex = (self.selectedIndex % #self.appList) + 1 self:redraw() end
end
return Launcher
)EMBED";
    writeIfMissing("/apps/launcher.lua", c_launcher);

    static const char c_clock[] PROGMEM = R"EMBED(
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local Clock = {}
function Clock:start() self:redraw() end
function Clock:redraw() Screen:clear() Screen:drawText(10,20,"Clock") Screen:drawText(10,40,"Tid ved WiFi/NTP.") Screen:update() end
function Clock:loop() local key = Keyboard:getKey() if not key then return end if key == "LONG_ENTER_5SEC" or key == "LONG_ENTER" then return "exit" end self:redraw() end
return Clock
)EMBED";
    writeIfMissing("/apps/clock.lua", c_clock);

    static const char c_settings[] PROGMEM = R"EMBED(
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local WiFi = require("hal.wifi")
local Settings = {}
local LINE_H = 12
local selectedIndex = 0
local ROW_YS = { 6, 18, 30, 76 }
local SCREEN_H = 122
local CONTENT_H = 92
function Settings:start()
    selectedIndex = 0
    HAL.screen_register_draw(function() self:_drawContent() end)
    self:redraw()
end
function Settings:_scrollOffset()
    local rowY = ROW_YS[selectedIndex + 1] or ROW_YS[4]
    return math.max(0, math.min(rowY - 28, math.max(0, CONTENT_H - SCREEN_H)))
end
function Settings:_drawContent()
    HAL.screen_setTextColor(false)
    local off = self:_scrollOffset()
    local y = 6
    local invStr = HAL.display_get_inverted() and "ON" or "OFF"
    self:_drawRow(y - off, "Inverter: " .. invStr, selectedIndex == 0)
    y = y + LINE_H
    self:_drawRow(y - off, "Refresh: " .. tostring(HAL.display_get_refresh_count()), selectedIndex == 1)
    y = y + LINE_H
    self:_drawRow(y - off, "Partial: " .. (HAL.display_get_partial() and "ON" or "OFF"), selectedIndex == 2)
    y = y + LINE_H + 4
    HAL.screen_drawLine(0, y - off, HAL.screen_getWidth(), y - off)
    y = y + 6
    HAL.screen_setTextColor(false)
    Screen:drawText(5, y - off, "IP: " .. (WiFi:getIP() ~= "" and WiFi:getIP() or "(ikke koblet)"))
    y = y + LINE_H + 6
    HAL.screen_drawLine(0, y - off, HAL.screen_getWidth(), y - off)
    y = y + 6
    self:_drawRow(y - off, ">>> Lagre og lukk <<<", selectedIndex == 3)
    y = y + LINE_H + 4
    HAL.screen_setTextColor(false)
    Screen:drawText(5, y - off, "Lang >5s=lagre+hjem")
end
function Settings:redraw() Screen:clear() Screen:update() end
function Settings:_drawRow(y, text, selected)
    if selected then HAL.screen_fillRect(0, y-2, HAL.screen_getWidth(), LINE_H+2) HAL.screen_setTextColor(true) HAL.screen_drawText(5,y,text) HAL.screen_setTextColor(false)
    else HAL.screen_setTextColor(false) HAL.screen_drawText(5,y,text) end
end
function Settings:loop()
    local key = Keyboard:getKey()
    if not key then return end
    if key == "LONG_ENTER_5SEC" then HAL.display_save_settings() return "exit" end
    if key == "LONG_ENTER" then
        if selectedIndex == 3 then HAL.display_save_settings() return "exit"
        elseif selectedIndex == 0 then HAL.display_set_inverted(not HAL.display_get_inverted())
        elseif selectedIndex == 1 then local rc = HAL.display_get_refresh_count() HAL.display_set_refresh_count((rc % 3) + 1)
        elseif selectedIndex == 2 then local p = HAL.display_get_partial() HAL.display_set_partial(not p) end
        self:redraw() return
    end
    selectedIndex = (selectedIndex + 1) % 4
    self:redraw()
end
return Settings
)EMBED";
    writeIfMissing("/apps/settings.lua", c_settings);

    static const char c_apps[] PROGMEM = R"EMBED(
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local Apps = {}
local intern, sd = {}, {}
local selectedSide, selectedIndex = 1, 1
local LINE_H, DIVIDER_X = 12, 125
local function loadLists()
    intern = {} sd = {}
    local t = HAL.file_list_flash("/apps") or {}
    for i = 1, #t do local n = t[i] if n and type(n)=="string" and n:match("%.lua$") then table.insert(intern, n:gsub("%.lua$","")) end end
    t = HAL.file_list_sd("/apps") or {}
    for i = 1, #t do local n = t[i] if n and type(n)=="string" and n:match("%.lua$") then table.insert(sd, n:gsub("%.lua$","")) end end
end
function Apps:start() loadLists() selectedSide = 1 selectedIndex = 1 if #intern==0 and #sd>0 then selectedSide = 2 end self:redraw() end
function Apps:redraw()
    Screen:clear()
    local w, h = HAL.screen_getWidth(), HAL.screen_getHeight()
    HAL.screen_drawLine(DIVIDER_X,0,DIVIDER_X,h) HAL.screen_drawLine(0,14,w,14)
    Screen:drawText(5,2,"Intern") Screen:drawText(DIVIDER_X+5,2,"SD")
    local yL, yR = 18, 18
    for i, name in ipairs(intern) do local sel = (selectedSide==1 and selectedIndex==i) Screen:drawText(5, yL, (sel and "> " or "  ")..name) yL = yL + LINE_H end
    if #intern==0 then Screen:drawText(5,yL,"  (ingen)") end
    for i, name in ipairs(sd) do local sel = (selectedSide==2 and selectedIndex==i) Screen:drawText(DIVIDER_X+5, yR, (sel and "> " or "  ")..name) yR = yR + LINE_H end
    if #sd==0 then Screen:drawText(DIVIDER_X+5,yR,"  (ingen)") end
    Screen:drawText(5, h-12, "Kort=velg  Lang=apne") Screen:update()
end
function Apps:loop()
    local key = Keyboard:getKey() if not key then return end
    if key == "LONG_ENTER_5SEC" then return "exit" end
    if key == "LONG_ENTER" then local name = (selectedSide==1 and intern[selectedIndex]) or (selectedSide==2 and sd[selectedIndex]) if name then return "launch", name end return end
    if selectedSide == 1 then if selectedIndex < #intern then selectedIndex = selectedIndex + 1 else if #sd>0 then selectedSide=2 selectedIndex=1 else selectedIndex=1 end end
    else if selectedIndex < #sd then selectedIndex = selectedIndex + 1 else selectedSide=1 selectedIndex=1 end end
    self:redraw()
end
return Apps
)EMBED";
    writeIfMissing("/apps/apps.lua", c_apps);

    static const char c_terminal[] PROGMEM = R"EMBED(
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local WiFi = require("hal.wifi")
local Terminal = {}
local LINE_H, MAX_LINES = 10, 10
local outputLines, cmdIndex, remoteMode, defaultPort = {}, 1, false, 23
local commands = { "info", "wifi", "clear", "connect 192.168.1.1", "disconnect", "help", "exit" }
local function addOutput(text)
    for line in (text.."\n"):gmatch("([^\n]*)\n") do table.insert(outputLines, line) if #outputLines > MAX_LINES then table.remove(outputLines, 1) end end
end
local function redraw()
    Screen:clear() local y = 0
    for _, line in ipairs(outputLines) do Screen:drawText(0, y, line) y = y + LINE_H end
    Screen:drawText(0, y, (remoteMode and "[TCP] " or "> ") .. (commands[cmdIndex] or "")) Screen:update()
end
local function runInfo() addOutput("Heap: "..HAL.system_get_heap().."  Uptime: "..math.floor(HAL.system_uptime_ms()/1000).."s") end
local function runWifi() addOutput("WiFi: "..WiFi:getStatus().."  IP: "..(WiFi:getIP()~="" and WiFi:getIP() or "-")) end
local function runCommand(cmd)
    cmd = cmd:match("^%s*(.-)%s*$") or "" if cmd=="" then return end
    local name, args = cmd:match("^(%S+)"), cmd:match("^%S+%s+(.+)$")
    if name=="info" then runInfo() elseif name=="wifi" then runWifi() elseif name=="clear" then outputLines={}
    elseif name=="connect" then local ip = args and args:match("^%s*([%d%.]+)") or "192.168.1.1" local port = args and tonumber(args:match("(%d+)%s*$")) or defaultPort remoteMode = HAL.tcp_connect(ip, port) addOutput(remoteMode and "Koblet." or "Kunne ikke koble til.")
    elseif name=="disconnect" then HAL.tcp_stop() remoteMode = false addOutput("Frakoblet.")
    elseif name=="help" then addOutput("info|wifi|clear|connect IP [port]|disconnect|help|exit") else addOutput("Ukjent: "..name) end
end
function Terminal:start() outputLines={} cmdIndex=1 remoteMode=false addOutput("RoseBox Terminal") addOutput("Kort=bytt  Lang=kjør") end
function Terminal:loop()
    if remoteMode and not HAL.tcp_connected() then remoteMode = false addOutput("Frakoblet.") redraw() return end
    if remoteMode and HAL.tcp_connected() then while HAL.tcp_available()>0 do local s = HAL.tcp_read(64) if s and s~="" then addOutput(s:gsub("\r","")) redraw() end end end
    local key = Keyboard:getKey() if not key then return end
    if key == "LONG_ENTER_5SEC" then return "exit" end
    if remoteMode then if key == "LONG_ENTER" then HAL.tcp_stop() remoteMode = false else HAL.tcp_send("\n") end redraw() return end
    if key == "LONG_ENTER" then local cmd = commands[cmdIndex] or "" if cmd == "exit" then return "exit" end runCommand(cmd) else cmdIndex = (cmdIndex % #commands) + 1 end
    redraw()
end
return Terminal
)EMBED";
    writeIfMissing("/apps/terminal.lua", c_terminal);

    return wroteAny;
}

#endif
