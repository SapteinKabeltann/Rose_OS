local config = require("config")
local WiFi = {}

function WiFi:connect(ssid, password)
    print(string.format("HAL: Connecting to WiFi '%s' via Native C++...", ssid))
    HAL.wifi_connect(ssid, password)
end

function WiFi:getStatus()
    return HAL.wifi_status()
end

function WiFi:getIP()
    return HAL.wifi_get_ip()
end

return WiFi
