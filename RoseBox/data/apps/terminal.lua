-- RoseBox Terminal – fullverdig terminal med systeminfo, WiFi, TCP/telnet
-- Kort trykk = bytt kommando, Lang trykk (>0.8s) = kjør kommando / send / disconnect

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
    "info",
    "wifi",
    "clear",
    "connect 192.168.1.1",
    "disconnect",
    "help",
    "exit"
}

local function addOutput(text)
    for line in (text .. "\n"):gmatch("([^\n]*)\n") do
        if line ~= "" or #outputLines == 0 then
            table.insert(outputLines, line)
            if #outputLines > MAX_LINES then
                table.remove(outputLines, 1)
            end
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
    local prompt = "> "
    if remoteMode then
        prompt = "[TCP] "
    end
    local curCmd = commands[cmdIndex] or ""
    Screen:drawText(0, y, prompt .. curCmd)
    Screen:update()
end

local function runInfo()
    local heap = HAL.system_get_heap()
    local uptime = HAL.system_uptime_ms()
    local uptimeSec = math.floor(uptime / 1000)
    addOutput("-- System --")
    addOutput(string.format("Heap: %d bytes", heap))
    addOutput(string.format("Uptime: %d s", uptimeSec))
    addOutput("")
end

local function runWifi()
    local status = WiFi:getStatus()
    local ip = WiFi:getIP()
    addOutput("-- WiFi --")
    addOutput("Status: " .. status)
    addOutput("IP: " .. (ip ~= "" and ip or "(ikke koblet)"))
    addOutput("")
end

local function runConnect(args)
    local ip = "192.168.1.1"
    local port = defaultPort
    if args and args ~= "" then
        ip = args:match("^%s*([%d%.]+)") or ip
        local p = args:match("(%d+)%s*$")
        if p then port = tonumber(p) or port end
    end
    addOutput("Kobler til " .. ip .. ":" .. port .. "...")
    redraw()
    local ok = HAL.tcp_connect(ip, port)
    if ok then
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
    addOutput("-- Kommandoer --")
    addOutput("Kort trykk = bytt, Lang = kjør")
    addOutput("info   = systeminfo")
    addOutput("wifi   = WiFi status/IP")
    addOutput("clear  = tøm skjerm")
    addOutput("connect IP [port] = TCP (telnet)")
    addOutput("disconnect = frakoble TCP")
    addOutput("help   = denne hjelpen")
    addOutput("exit   = lukk terminal (tilbake til hjem)")
    addOutput("")
end

local function runCommand(cmd)
    cmd = cmd:match("^%s*(.-)%s*$") or ""
    if cmd == "" then return end
    local name = cmd:match("^(%S+)")
    local args = cmd:match("^%S+%s+(.+)$")
    if name == "info" then
        runInfo()
    elseif name == "wifi" then
        runWifi()
    elseif name == "clear" then
        outputLines = {}
    elseif name == "connect" then
        runConnect(args)
    elseif name == "disconnect" then
        runDisconnect()
    elseif name == "help" then
        runHelp()
    else
        addOutput("Ukjent: " .. name .. " (prøv help)")
    end
end

function Terminal:start()
    outputLines = {}
    cmdIndex = 1
    remoteMode = false
    addOutput("RoseBox Terminal v1.0")
    addOutput("Kort=bytt kommando, Lang=kjør")
    runHelp()
    -- Ikke redraw() her: behold home-skjermen (home.h) til bruker trykker. Første tastetrykk viser terminalen.
    print("App: Terminal started")
end

function Terminal:loop()
    if remoteMode and not HAL.tcp_connected() then
        remoteMode = false
        addOutput("Frakoblet (server lukket).")
        redraw()
        return
    end

    -- TCP-modus: les mottatt data og vis på skjerm
    if remoteMode and HAL.tcp_connected() then
        local hadData = false
        while HAL.tcp_available() > 0 do
            local s = HAL.tcp_read(64)
            if s and s ~= "" then
                addOutput(s:gsub("\r", ""))
                hadData = true
            end
        end
        if hadData then redraw() end
    end

    local key = Keyboard:getKey()
    if not key then return end

    if remoteMode then
        if key == "LONG_ENTER" then
            runDisconnect()
        else
            HAL.tcp_send("\n")
        end
        redraw()
        return
    end

    if key == "LONG_ENTER" then
        local cmd = commands[cmdIndex] or ""
        if cmd == "exit" then
            return "exit"
        end
        runCommand(cmd)
    else
        cmdIndex = (cmdIndex % #commands) + 1
    end
    redraw()
end

return Terminal
