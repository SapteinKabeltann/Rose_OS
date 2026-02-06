local config = require("config")

local Screen = {}

function Screen:init()
    if config.screen_type == "monochrome_spi" then
        print("HAL: Initializing Monochrome SPI Screen via Native C++...")
        HAL.screen_init()
    elseif config.screen_type == "oled_i2c" then
        print("HAL: Initializing OLED I2C Screen (Not yet implemented in native)...")
    else
        print("HAL: Unknown screen type: " .. tostring(config.screen_type))
    end
end

function Screen:register_draw(f)
    HAL.screen_register_draw(f)
end

function Screen:unregister_draw()
    HAL.screen_unregister_draw()
end

function Screen:clear()
    HAL.screen_clear()
end

function Screen:drawText(x, y, text)
    HAL.screen_drawText(x, y, text)
end

function Screen:update()
    HAL.screen_update()
end

-- Hjem-skjerm / dashboard (home.h). Kall for å vise systemets hjem-skjerm igjen.
function Screen:drawHome()
    HAL.screen_drawHome()
end

return Screen
