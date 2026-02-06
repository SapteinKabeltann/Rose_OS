local config = require("config")
local GPIO = {}

function GPIO:setup()
    print("HAL: Setting up GPIO via Native C++...")
    for name, pin in pairs(config.gpio_pins) do
        -- Knapp og sensorer må være INPUT, ellers leses ikke knappen (IO39) riktig
        local isInput = (name == "button") or (type(name) == "string" and name:match("^sensor"))
        local mode = isInput and 0 or 1  -- 0 = INPUT, 1 = OUTPUT
        HAL.gpio_mode(pin, mode)
        print(string.format("  - Pin %d configured as %s (Logic ID: %s)", pin, isInput and "INPUT" or "OUTPUT", name))
    end
end

function GPIO:write(name, value)
    local pin = config.gpio_pins[name]
    if pin then
        HAL.gpio_write(pin, value)
    else
        print("HAL: Error - Unknown GPIO name: " .. tostring(name))
    end
end

function GPIO:read(name)
    local pin = config.gpio_pins[name]
    if pin then
        return HAL.gpio_read(pin)
    end
    return nil
end

return GPIO
