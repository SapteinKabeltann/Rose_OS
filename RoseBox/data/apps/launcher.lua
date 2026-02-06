-- RoseBox Launcher: hjem-skjerm med app-ikoner. Kort trykk = bytt app, Lang trykk = åpne app.
-- Som RoseOS: appene rendres på hjem-skjermen (home.h + ikoner fra C++).

local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")

local Launcher = {}
Launcher.appList = { "terminal", "clock", "settings", "apps" }
Launcher.selectedIndex = 1  -- 1-based
Launcher.currentApp = nil   -- modul som kjører (har :loop()), eller nil

function Launcher:start()
    self.selectedIndex = 1
    self.currentApp = nil
    self:redraw()
    print("Launcher: home menu ready")
end

function Launcher:redraw()
    HAL.screen_drawHomeWithMenu(self.appList, self.selectedIndex)
end

function Launcher:launchApp(name)
    HAL.screen_unregister_draw()
    local mod = nil
    local ok, err = pcall(function()
        if name == "terminal" then
            mod = require("apps.terminal")
        elseif name == "clock" then
            mod = require("apps.clock")
        elseif name == "settings" then
            mod = require("apps.settings")
        elseif name == "apps" then
            mod = require("apps.apps")
        else
            mod = require("apps." .. name)
        end
    end)
    if not ok or not mod or not mod.start then
        print("Launcher: could not start app '" .. tostring(name) .. "': " .. tostring(err))
        return
    end
    self.currentApp = mod
    mod:start()
    print("Launcher: opened " .. name)
end

function Launcher:closeApp()
    if self.currentApp and self.currentApp.onExit then
        self.currentApp:onExit()
    end
    self.currentApp = nil
    self:redraw()
    print("Launcher: returned to home")
end

function Launcher:loop()
    if self.currentApp then
        local a, b = self.currentApp:loop()
        if a == "exit" then
            self:closeApp()
        elseif a == "launch" and b then
            self:closeApp()
            self:launchApp(b)
        end
        return
    end

    local key = Keyboard:getKey()
    if not key then return end

    if key == "LONG_ENTER" then
        local name = self.appList[self.selectedIndex]
        if name then
            self:launchApp(name)
        end
    else
        -- Kort trykk: neste app
        self.selectedIndex = (self.selectedIndex % #self.appList) + 1
        self:redraw()
    end
end

return Launcher
