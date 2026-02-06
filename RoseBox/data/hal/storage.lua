local config = require("config")
local Storage = {}

function Storage:init()
    if config.sd_cs_pin then
        print(string.format("HAL: Initializing SD Card on CS Pin %d...", config.sd_cs_pin))
        -- C.sd_init(config.sd_cs_pin)
    else
        print("HAL: No SD CS pin defined, skipping storage init.")
    end
end

function Storage:readFile(path)
    local content = HAL.file_read(path)
    return content
end

function Storage:writeFile(path, content)
    local ok = HAL.file_write(path, content)
    return ok
end

return Storage
