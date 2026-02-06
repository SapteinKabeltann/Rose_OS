-- RoseBox Terminal – fullverdig terminal (kopi fra data/apps for SD-kort)
local Screen = require("hal.screen")
local Keyboard = require("hal.keyboard")
local WiFi = require("hal.wifi")

local Terminal = {}
local LINE_H = 10
local MAX_LINES = 10
local outputLines = {}
local cmdIndex = 1
local remoteMode = false
local defaultPort = 23

local commands = {
    "info", "wifi", "clear",
    "connect 192.168.1.1", "disconnect", "help"
}

local function addOutput(text)
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        if line ~= "" or #outputLines == 0 then
            table.insert(outputLines, line)
            if #outputLines > MAX_LINES then table.remove(outputLines, 1) end
        end
    end
end

local function redraw()
    Screen:clear()
    local y = 0
    for _, line in ipairs(outputLines) do
        Screen:drawText(0, y, line)
        y = y + LINE_H
    end
    local prompt = remoteMode and "[TCP] " or "> "
    Screen:drawText(0, y, prompt .. (commands[cmdIndex] or ""))
    Screen:update()
end

local function runInfo()
    addOutput("-- System --")
    addOutput(string.format("Heap: %d bytes", HAL.system_get_heap()))
    addOutput(string.format("Uptime: %d s", math.floor(HAL.system_uptime_ms() / 1000)))
    addOutput("")
end

local function runWifi()
    addOutput("-- WiFi --")
    addOutput("Status: " .. WiFi:getStatus())
    addOutput("IP: " .. (WiFi:getIP() ~= "" and WiFi:getIP() or "(ikke koblet)"))
    addOutput("")
end

local function runConnect(args)
    local ip, port = "192.168.1.1", defaultPort
    if args and args ~= "" then
        ip = args:match("^%s*([%d%.]+)") or ip
        local p = args:match("(%d+)%s*$")
        if p then port = tonumber(p) or port end
    end
    addOutput("Kobler til " .. ip .. ":" .. port .. "...")
    redraw()
    if HAL.tcp_connect(ip, port) then
        remoteMode = true
        addOutput("Koblet. Lang trykk = disconnect.")
    else
        addOutput("Kunne ikke koble til.")
    end
end

local function runDisconnect()
    HAL.tcp_stop()
    remoteMode = false
    addOutput("Frakoblet.")
end

local function runHelp()
    addOutput("Kort=bytt kommando, Lang=kjør")
    addOutput("info / wifi / clear / connect IP [port] / disconnect / help")
    addOutput("")
end

local function runCommand(cmd)
    cmd = (cmd:match("^%s*(.-)%s*$") or ""):gsub("^%s+", "")
    if cmd == "" then return end
    local name, args = cmd:match("^(%S+)%s*(.*)$")
    if name == "info" then runInfo()
    elseif name == "wifi" then runWifi()
    elseif name == "clear" then outputLines = {}
    elseif name == "connect" then runConnect(args)
    elseif name == "disconnect" then runDisconnect()
    elseif name == "help" then runHelp()
    else addOutput("Ukjent: " .. (name or cmd)) end
end

function Terminal:start()
    outputLines = {}
    cmdIndex = 1
    remoteMode = false
    addOutput("RoseBox Terminal v1.0")
    runHelp()
    redraw()
end

function Terminal:loop()
    if remoteMode and not HAL.tcp_connected() then
        remoteMode = false
        addOutput("Frakoblet (server lukket).")
        redraw()
        return
    end
    if remoteMode and HAL.tcp_connected() then
        local hadData = false
        while HAL.tcp_available() > 0 do
            local s = HAL.tcp_read(64)
            if s and s ~= "" then addOutput(s:gsub("\r", "")); hadData = true end
        end
        if hadData then redraw() end
    end
    local key = Keyboard:getKey()
    if not key then return end
    if remoteMode then
        if key == "LONG_ENTER" then runDisconnect() else HAL.tcp_send("\n") end
        redraw()
        return
    end
    if key == "LONG_ENTER" then runCommand(commands[cmdIndex] or "")
    else cmdIndex = (cmdIndex % #commands) + 1 end
    redraw()
end

return Terminal
