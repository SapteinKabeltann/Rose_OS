local config = require("config")
local Keyboard = {}

function Keyboard:init()
    if config.keyboard_type == "matrix_4x4" then
        print("HAL: Initializing 4x4 Matrix Keyboard (Native implementation needed for scan)...")
    end
end

function Keyboard:getKey()
    -- Uses the native C++ HAL binding
    return HAL.keyboard_getKey()
end

-- Blocking wait for key (with delay to avoid busy-loop; best practice)
function Keyboard:waitForKey()
    while true do
        local key = self:getKey()
        if key then return key end
        HAL.delay_ms(10)
    end
end

return Keyboard
