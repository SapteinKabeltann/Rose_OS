-- RoseBox Apps: liste over apper på flash (Intern) og SD. Venstre = Intern, høyre = SD. Lang trykk = åpne valgt app.
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")

local Apps = {}
local intern = {}
local sd = {}
local selectedSide = 1   -- 1 = Intern, 2 = SD
local selectedIndex = 1
local LINE_H = 12
local DIVIDER_X = 125

local function loadLists()
    intern = {}
    sd = {}
    local t = HAL.file_list_flash("/apps")
    if t and type(t) == "table" then
        for i = 1, #t do
            local name = t[i]
            if name and type(name) == "string" and name:match("%.lua$") then
                table.insert(intern, name:gsub("%.lua$", ""))
            end
        end
    end
    t = HAL.file_list_sd("/apps")
    if t and type(t) == "table" then
        for i = 1, #t do
            local name = t[i]
            if name and type(name) == "string" and name:match("%.lua$") then
                table.insert(sd, name:gsub("%.lua$", ""))
            end
        end
    end
end

function Apps:start()
    loadLists()
    selectedSide = 1
    selectedIndex = 1
    if #intern == 0 and #sd > 0 then
        selectedSide = 2
    end
    self:redraw()
end

function Apps:redraw()
    Screen:clear()
    local w = HAL.screen_getWidth()
    local h = HAL.screen_getHeight()
    -- Midtstrek
    HAL.screen_drawLine(DIVIDER_X, 0, DIVIDER_X, h)
    -- Headers
    Screen:drawText(5, 2, "Intern")
    Screen:drawText(DIVIDER_X + 5, 2, "SD")
    -- Horisontal linje under header
    HAL.screen_drawLine(0, 14, w, 14)

    local yLeft = 18
    local yRight = 18
    for i, name in ipairs(intern) do
        local sel = (selectedSide == 1 and selectedIndex == i)
        local txt = (sel and "> " or "  ") .. name
        Screen:drawText(5, yLeft, txt)
        yLeft = yLeft + LINE_H
    end
    if #intern == 0 then
        Screen:drawText(5, yLeft, "  (ingen)")
    end

    for i, name in ipairs(sd) do
        local sel = (selectedSide == 2 and selectedIndex == i)
        local txt = (sel and "> " or "  ") .. name
        Screen:drawText(DIVIDER_X + 5, yRight, txt)
        yRight = yRight + LINE_H
    end
    if #sd == 0 then
        Screen:drawText(DIVIDER_X + 5, yRight, "  (ingen)")
    end

    Screen:drawText(5, h - 12, "Kort=velg  Lang=apne")
    Screen:update()
end

function Apps:loop()
    local key = Keyboard:getKey()
    if not key then return end

    if key == "LONG_ENTER_5SEC" then
        return "exit"
    end

    if key == "LONG_ENTER" then
        local name
        if selectedSide == 1 and intern[selectedIndex] then
            name = intern[selectedIndex]
        elseif selectedSide == 2 and sd[selectedIndex] then
            name = sd[selectedIndex]
        end
        if name then
            return "launch", name
        end
        return
    end

    -- Kort trykk: neste element (venstre liste først, deretter høyre)
    if selectedSide == 1 then
        if selectedIndex < #intern then
            selectedIndex = selectedIndex + 1
        else
            if #sd > 0 then
                selectedSide = 2
                selectedIndex = 1
            else
                selectedIndex = 1
            end
        end
    else
        if selectedIndex < #sd then
            selectedIndex = selectedIndex + 1
        else
            selectedSide = 1
            selectedIndex = 1
        end
    end
    self:redraw()
end

return Apps
