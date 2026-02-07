-- Lastes ved første Lang trykk (åpne app). Definerer launchApp, closeApp, ensureWiFi.
local function redraw()
  HAL.screen_drawHomeWithMenu(_G.appList, _G.selectedIndex)
end

local function launchApp(name)
  HAL.screen_unregister_draw()
  local mod = nil
  local ok, err = pcall(function()
    mod = require("apps." .. name)  -- apps/terminal.lua, apps/clock.lua, apps/apps.lua osv.
  end)
  if not ok or not mod or not mod.start then
    print("Bootstrap: kunne ikke starte '" .. tostring(name) .. "': " .. tostring(err))
    redraw()
    return
  end
  _G.currentApp = mod
  _G.currentAppName = name
  mod:start()
  print("Bootstrap: åpnet " .. name)
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
  print("Bootstrap: tilbake til hjem")
end

function ensureWiFi()
  if _G._wifi_ensured then return end
  _G._wifi_ensured = true
  local ok, config = pcall(require, "config")
  if not ok or not config then return end
  local ok2, WiFi = pcall(require, "hal.wifi")
  if ok2 and WiFi and config.wifi_ssid and #config.wifi_ssid > 0 then
    WiFi:connect(config.wifi_ssid, config.wifi_pass or "")
  end
end

return { launchApp = launchApp, closeApp = closeApp, ensureWiFi = ensureWiFi }
