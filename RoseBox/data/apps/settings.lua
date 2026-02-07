-- RoseBox Settings: Invertering, Refresh count, Partial refresh. Kort = neste, Lang = endre, Lang >5s = lagre og hjem.
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
-- WiFi lazy: lastes ved første bruk, ensureWiFi() kobler til
local _wifi = nil
local function getWiFi()
    if _wifi == false then return nil end
    if _wifi then return _wifi end
    ensureWiFi()
    local ok, w = pcall(require, "hal.wifi")
    if not ok or not w then _wifi = false; return nil end
    _wifi = w
    return _wifi
end

local Settings = {}
local LINE_H = 12
local selectedIndex = 0  -- 0=Invert, 1=RefreshCount, 2=Partial, 3=Lagre og lukk
local SCREEN_H = 122
local ROW_YS = { 6, 18, 30, 76 }  -- y for hver valgbar rad (Invert, Refresh, Partial, Lagre)
local CONTENT_H = 92

function Settings:start()
    selectedIndex = 0
    HAL.screen_register_draw(function()
        self:_drawContent()
    end)
    self:redraw()
end

function Settings:_scrollOffset()
    local rowY = ROW_YS[selectedIndex + 1] or ROW_YS[4]
    local targetY = 28
    local maxOffset = math.max(0, CONTENT_H - SCREEN_H)
    return math.max(0, math.min(rowY - targetY, maxOffset))
end

function Settings:_drawContent()
    -- Forgrunn = false, bakgrunn (hvit på valgt rad) = true. I Lua er 0 truthy, derfor bruk false/true.
    HAL.screen_setTextColor(false)
    local off = self:_scrollOffset()
    local y = 6
    local inv = HAL.display_get_inverted() and 1 or 0
    local invStr = inv == 1 and "ON" or "OFF"
    self:_drawRow(y - off, "Inverter: " .. invStr, selectedIndex == 0)
    y = y + LINE_H
    local rc = HAL.display_get_refresh_count()
    self:_drawRow(y - off, "Refresh (1-3, lagres): " .. tostring(rc), selectedIndex == 1)
    y = y + LINE_H
    local part = HAL.display_get_partial() and 1 or 0
    local partStr = part == 1 and "ON" or "OFF"
    self:_drawRow(y - off, "Partial: " .. partStr, selectedIndex == 2)
    y = y + LINE_H + 4
    HAL.screen_drawLine(0, y - off, HAL.screen_getWidth(), y - off)
    y = y + 6
    HAL.screen_setTextColor(false)
    local w = getWiFi()
    local ip = (w and w:getIP()) or ""
    Screen:drawText(5, y - off, "IP: " .. (ip ~= "" and ip or "(ikke koblet)"))
    y = y + LINE_H + 6
    HAL.screen_drawLine(0, y - off, HAL.screen_getWidth(), y - off)
    y = y + 6
    self:_drawRow(y - off, ">>> Lagre og lukk <<<", selectedIndex == 3)
    y = y + LINE_H + 4
    HAL.screen_setTextColor(false)
    Screen:drawText(5, y - off, "Lang >5s=lagre+hjem")
end

function Settings:redraw()
    Screen:clear()
    Screen:update()
end

function Settings:_drawRow(y, text, selected)
    if selected then
        HAL.screen_fillRect(0, y - 2, HAL.screen_getWidth(), LINE_H + 2)
        HAL.screen_setTextColor(true)   -- hvit tekst på valgt rad
        HAL.screen_drawText(5, y, text)
        HAL.screen_setTextColor(false)  -- tilbake til forgrunn
    else
        HAL.screen_setTextColor(false)
        HAL.screen_drawText(5, y, text)
    end
end

function Settings:loop()
    local key = Keyboard:getKey()
    if not key then return end

    if key == "LONG_ENTER_5SEC" then
        HAL.display_save_settings()
        return "exit"
    end

    if key == "LONG_ENTER" then
        if selectedIndex == 3 then
            HAL.display_save_settings()
            return "exit"
        elseif selectedIndex == 0 then
            local inv = HAL.display_get_inverted() and 1 or 0
            HAL.display_set_inverted(inv == 0)
        elseif selectedIndex == 1 then
            local rc = HAL.display_get_refresh_count()
            rc = (rc % 3) + 1
            HAL.display_set_refresh_count(rc)
        elseif selectedIndex == 2 then
            local part = HAL.display_get_partial() and 1 or 0
            HAL.display_set_partial(part == 0)
        end
        self:redraw()
        return
    end

    -- Kort trykk: neste innstilling
    selectedIndex = (selectedIndex + 1) % 4
    self:redraw()
end

return Settings
