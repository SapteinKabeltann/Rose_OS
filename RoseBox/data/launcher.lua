-- Launcher: viser app-ikoner, starter app ved brukerinteraksjon.
-- Lastes etter core (via HAL.load_launcher fra C). Core + Launcher forblir i RAM.
-- Apper lastes kun ved start og frigjør RAM ved avslutning.

_G.appList = { "terminal", "clock", "settings", "apps" }
_G.selectedIndex = 1
_G.currentApp = nil
_G.currentAppName = nil
local initialRedrawDone = false

local function redraw()
  HAL.screen_drawHomeWithMenu(_G.appList, _G.selectedIndex)
end

local function launchApp(appName)
  HAL.screen_unregister_draw()
  -- Frigjør eventuell tidligere instans og RAM før innlasting
  package.loaded["apps." .. appName] = nil
  collectgarbage("collect")

  local mod = nil
  local ok, err = pcall(function()
    mod = require("apps." .. appName)
  end)
  if not ok or not mod or not mod.start then
    print("Kunne ikke starte app: " .. tostring(err))
    redraw()
    return
  end
  _G.currentApp = mod
  _G.currentAppName = appName
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

local function launcher_loop()
  if _G.currentApp then
    local a, b = _G.currentApp:loop()
    if a == "exit" then closeApp() return end
    if a == "launch" and b then closeApp() launchApp(b) return end
    return
  end
  if not initialRedrawDone then
    initialRedrawDone = true
    redraw()
    print("Launcher: hjem klar")
  end
  local key = HAL.keyboard_getKey()
  if not key then return end
  if key == "LONG_ENTER" then
    local n = _G.appList[_G.selectedIndex]
    if n then launchApp(n) end
  else
    _G.selectedIndex = (_G.selectedIndex % #_G.appList) + 1
    redraw()
  end
end

return { loop = launcher_loop }
