-- RoseBox Clock app (enkel). Lang trykk = tilbake til hjem.
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")

local Clock = {}
local lastDraw = 0

function Clock:start()
    self:redraw()
end

function Clock:redraw()
    Screen:clear()
    Screen:drawText(10, 20, "Clock")
    Screen:drawText(10, 40, "Tid kommer ved WiFi/NTP.")
    Screen:drawText(10, 70, "Lang trykk = tilbake")
    Screen:update()
end

function Clock:loop()
    local key = Keyboard:getKey()
    if key == "LONG_ENTER" then
        return "exit"
    end
    if key then
        self:redraw()
    end
end

return Clock
