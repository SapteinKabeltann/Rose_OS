-- Minimal: hjem + taster. Ved Lang trykk kalles HAL.load_app_core() (C) så _G._core lastes uten dybde/krasj.
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
