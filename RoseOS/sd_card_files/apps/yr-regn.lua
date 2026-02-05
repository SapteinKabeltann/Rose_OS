-- YR Nedbørsvarsel App for RoseOS
-- Viser nedbørsvarsel for neste 90 minutter fra YR.no
-- 
-- Basert på rein_app/yr-regn-display
-- 
-- MERK: Denne appen krever full Lua-støtte i RoseOS som ikke er implementert ennå.
-- Appen vil fungere når RoseOS får Lua-interpreter implementert.

app = {
    name = "YR Nedbør",
    version = "1.0",
    author = "RoseOS"
}

-- Konfigurasjon
local YR_API_URL = "https://www.yr.no/api/v0/locations/%s/forecast/now"
local UPDATE_INTERVAL = 5 * 60 * 1000  -- 5 minutter i millisekunder
local MAX_PRECIPITATION = 17.0
local SHOW_GRAPH_ON_NO_PRECIPITATION = true
local SHOW_BATTERY_INDICATOR = true

-- User Agent for YR.no API
-- VIKTIG: Endre dette til ditt eget GitHub-brukernavn for å unngå å bli blokkert
-- YR.no krever en identifiserbar user agent
local GITHUB_USERNAME = "dittBrukernavn"  -- ENDRE DETTE!
local USER_AGENT = "epaper/1.0 github.com/" .. GITHUB_USERNAME

-- App state
local yrLocation = ""
local precipitationData = {}  -- Array med 18 verdier (90 minutter, 5-min intervall)
local createdTime = ""
local radarIsDown = false
local lastUpdate = 0
local errorMessage = ""

-- Hjelpefunksjoner for graf-tegning
function coordinateFromSquaredPrecipitation(value, maxHeight)
    if value >= MAX_PRECIPITATION then
        return maxHeight - 20
    end
    
    local maxSquared = math.sqrt(MAX_PRECIPITATION)
    local valueSquared = math.sqrt(value)
    
    return math.floor((valueSquared / maxSquared) * maxHeight)
end

function drawTitle(x, y, title)
    display.text(x, y - 8, title, 2)
    -- Tegn ø manuelt (hack for manglende ø-støtte)
    display.line(x + 51, y - 18, x + 43, y - 7)
    display.line(x + 52, y - 18, x + 44, y - 8)
end

function drawTime(x, y, timeStr)
    display.text(x, y - 2, timeStr, 1)
end

function drawAxes(x, y, w, h)
    display.line(x, y + h, x + w, y + h)  -- X-akse
end

function drawGridLines(x, y, w, h)
    local lineY1 = y + h * 1/4
    local lineY2 = y + h * 2/4
    local lineY3 = y + h * 3/4
    display.line(x, lineY1, x + w, lineY1)
    display.line(x, lineY2, x + w, lineY2)
    display.line(x, lineY3, x + w, lineY3)
end

function drawRaindrop(x, y, fillLevel)
    local size = 6
    local radius = size / 2
    
    -- Tegn regndråpe-form (sirkel + trekant)
    display.circle(x, y, radius, false)
    display.triangle(x - radius, y, x + radius, y, x, y - size, false)
    
    -- Fyll basert på fillLevel
    if fillLevel == 3 then
        display.circle(x, y, radius, true)
        display.triangle(x - radius, y, x + radius, y, x, y - size, true)
    elseif fillLevel == 2 then
        display.circle(x, y, radius, true)
        -- Tegn hvite linjer for delvis fylling
        display.line(x - radius + 3, y - radius, x + radius - 3, y - radius, true)  -- Hvit
    elseif fillLevel == 1 then
        display.circle(x, y, radius, true)
        display.line(x - radius + 3, y - radius, x + radius - 3, y - radius, true)  -- Hvit
    end
end

function drawRaindrops(x, y, h)
    local lineY1 = y + h * 1/4
    local lineY2 = y + h * 2/4
    local lineY3 = y + h * 3/4
    drawRaindrop(x - 9, lineY1, 3)
    drawRaindrop(x - 9, lineY2, 2)
    drawRaindrop(x - 9, lineY3, 1)
end

function drawGraphData(x, y, w, h, data)
    local coordinates = {}
    
    -- Beregn koordinater
    for i = 1, #data do
        local minute = (i - 1) * 5
        local coordX = x + (minute * w / 90)
        local coordY = y + h - coordinateFromSquaredPrecipitation(data[i], h)
        table.insert(coordinates, {x = coordX, y = coordY})
    end
    
    -- Legg til siste punkt
    table.insert(coordinates, {x = x + w, y = coordinates[#coordinates].y})
    
    -- Tegn og fyll graf
    if #coordinates > 0 then
        -- Start trekant
        display.triangle(x, y + h, coordinates[1].x, coordinates[1].y, x, coordinates[1].y, true)
        
        -- Tegn resten av grafen
        for i = 1, #coordinates - 1 do
            -- Fyll trekant
            display.triangle(
                coordinates[i].x, coordinates[i].y,
                coordinates[i+1].x, coordinates[i+1].y,
                coordinates[i].x, y + h,
                true
            )
            display.triangle(
                coordinates[i+1].x, coordinates[i+1].y,
                coordinates[i+1].x, y + h,
                coordinates[i].x, y + h,
                true
            )
            -- Tegn linje
            display.line(coordinates[i].x, coordinates[i].y, coordinates[i+1].x, coordinates[i+1].y)
        end
    end
end

function drawXAxisLabels(x, y, w, h)
    local labels = {"No", "15", "30", "45", "60", "75", "90"}
    for i = 1, 7 do
        local labelX = x + ((i - 1) * w / 6)
        display.line(labelX, y + h, labelX, y + h + 5)
        if i == 1 then
            display.text(labelX, y + h + 7, labels[i], 1)
        elseif i < 7 then
            display.text(labelX - 5, y + h + 7, labels[i], 1)
        else
            display.text(labelX - 10, y + h + 7, labels[i], 1)
        end
    end
end

-- Hovedvisningsfunksjoner
function drawPrecipitationGraph(x, y, w, h)
    display.clear()
    
    drawTitle(x, y, "Nedbor neste 90 minutt")
    drawTime(x, y, createdTime)
    drawAxes(x, y, w, h)
    drawGridLines(x, y, w, h)
    drawRaindrops(x, y, h)
    drawGraphData(x, y, w, h, precipitationData)
    drawXAxisLabels(x, y, w, h)
    
    if SHOW_BATTERY_INDICATOR then
        drawBatteryIndicator(110, 23)
    end
    
    display.refresh()
end

function drawNoPrecipitationView()
    display.clear()
    display.text(10, 30, "Det blir opphald", 2)
    display.text(10, 50, "dei neste 90 minutta", 2)
    display.text(10, 70, "Oppdatert: " .. createdTime, 1)
    display.refresh()
end

function drawNoRadarView()
    display.clear()
    display.text(10, 30, "Ingen radardata", 2)
    display.text(10, 50, "tilgjengelig", 2)
    
    -- Tegn radar-ikon i øvre høyre hjørne
    local size = 40
    local x = display.width() - size - 10
    local y = 10
    local centerX = x + size / 2
    local centerY = y + size / 2
    
    -- Tegn konsentriske sirkler
    display.circle(centerX, centerY, size / 2, false)
    display.circle(centerX, centerY, size / 3, false)
    display.circle(centerX, centerY, size / 5, false)
    display.circle(centerX, centerY, 2, true)  -- Senterpunkt
    
    -- Tegn vertikal linje
    display.line(centerX, centerY, centerX, y + size)
    
    -- Tegn små prikker
    display.circle(centerX - size / 4, centerY - size / 6, 1, true)
    display.circle(centerX + size / 5, centerY - size / 5, 1, true)
    display.circle(centerX + size / 6, centerY + size / 4, 1, true)
    
    display.text(10, 70, "Oppdatert: " .. createdTime, 1)
    display.refresh()
end

function drawBatteryIndicator(x, y)
    local voltage = system.batteryVoltage()
    local percentage = system.batteryPercent()
    
    -- Batteri-omriss
    display.rect(x, y, 19, 8, false)
    display.rect(x + 19, y + 2, 2, 4, true)  -- Batteri-pinne
    
    -- Tekst for prosent og spenning
    display.text(x + 24, y, percentage .. "% (" .. string.format("%.1f", voltage) .. "V)", 1)
    
    -- Beregn batterinivå
    local level = math.floor((percentage / 100) * 18)
    display.rect(x + 1, y + 1, level, 6, true)
    
    -- Batterinivå-indikatorlinjer
    for i = 1, 3 do
        display.line(x + 5 * i, y + 1, x + 5 * i, y + 7)
    end
end

function drawErrorView(message)
    display.clear()
    display.text(10, 30, "Error:", 2)
    
    -- Del opp melding i flere linjer
    local yPos = 60
    local words = {}
    for word in message:gmatch("%S+") do
        table.insert(words, word)
    end
    
    local line = ""
    local x = 10
    for _, word in ipairs(words) do
        local testLine = line .. (line == "" and "" or " ") .. word
        if display.textWidth(testLine, 1) > display.width() - 20 then
            if line ~= "" then
                display.text(x, yPos, line, 1)
                yPos = yPos + 25
                line = word
            else
                display.text(x, yPos, word, 1)
                yPos = yPos + 25
            end
        else
            line = testLine
        end
    end
    if line ~= "" then
        display.text(x, yPos, line, 1)
    end
    
    display.refresh()
end

-- API og datahåndtering
function parsePrecipitationData(jsonData)
    -- Parse JSON (krever JSON-bibliotek i Lua)
    -- Dette er en forenklet versjon - full implementasjon krever JSON-parser
    local doc = json.parse(jsonData)
    
    if not doc then
        return false, "Kunne ikke parse JSON"
    end
    
    -- Hent created time
    createdTime = doc.created or ""
    
    -- Sjekk om radar er nede
    radarIsDown = doc.radarIsDown or false
    
    if radarIsDown then
        return true, nil
    end
    
    -- Hent nedbørsdata
    local points = doc.points
    if not points or #points == 0 then
        return false, "Ingen nedbørsdata funnet"
    end
    
    precipitationData = {}
    for i = 1, math.min(18, #points) do
        local intensity = points[i].precipitation.intensity or 0
        table.insert(precipitationData, intensity)
    end
    
    return true, nil
end

function fetchPrecipitationData()
    if yrLocation == "" then
        return false, "YR location ikke satt. Konfigurer i innstillinger."
    end
    
    if not wifi.isConnected() then
        return false, "WiFi ikke tilkoblet"
    end
    
    local url = string.format(YR_API_URL, yrLocation)
    -- Send med user agent header
    local headers = {
        ["User-Agent"] = USER_AGENT
    }
    local response, status = http.get(url, headers)
    
    if status ~= 200 then
        return false, "HTTP feil: " .. tostring(status)
    end
    
    if not response or response == "" then
        return false, "Tom respons fra YR API"
    end
    
    local success, err = parsePrecipitationData(response)
    return success, err
end

function updateDisplay()
    if radarIsDown then
        drawNoRadarView()
        return
    end
    
    -- Sjekk om det er nedbør
    local hasPrecipitation = false
    for i = 1, #precipitationData do
        if precipitationData[i] > 0 then
            hasPrecipitation = true
            break
        end
    end
    
    if hasPrecipitation or SHOW_GRAPH_ON_NO_PRECIPITATION then
        drawPrecipitationGraph(16, 25, 225, 78)
    else
        drawNoPrecipitationView()
    end
end

-- App lifecycle
function setup()
    display.clear()
    display.text(10, 10, "YR Nedbørsvarsel", 2)
    display.text(10, 40, "Laster konfigurasjon...", 1)
    display.refresh()
    
    -- Last GitHub-brukernavn fra lagring (eller bruk default)
    local savedUsername = storage.get("github_username")
    if savedUsername and savedUsername ~= "" and savedUsername ~= "dittBrukernavn" then
        GITHUB_USERNAME = savedUsername
        USER_AGENT = "epaper/1.0 github.com/" .. GITHUB_USERNAME
    else
        -- Advarsel hvis ikke konfigurert
        if GITHUB_USERNAME == "dittBrukernavn" then
            errorMessage = "GitHub-brukernavn ikke satt!\n\nEndre GITHUB_USERNAME i\nkoden eller send via BLE:\nCMD:CONFIG:GITHUB_USER:<navn>\n\nYR.no krever dette for API."
            drawErrorView(errorMessage)
            return
        end
    end
    
    -- Last YR location fra lagring
    yrLocation = storage.get("yr_location") or ""
    
    if yrLocation == "" then
        errorMessage = "YR location ikke konfigurert.\n\nSend via BLE:\nCMD:CONFIG:YR_LOCATION:<location-id>\n\nFinn location ID på yr.no"
        drawErrorView(errorMessage)
        return
    end
    
    -- Prøv å hente data
    local success, err = fetchPrecipitationData()
    if not success then
        errorMessage = err or "Kunne ikke hente nedbørsdata"
        drawErrorView(errorMessage)
    else
        updateDisplay()
    end
    
    lastUpdate = system.time()
end

function loop()
    local currentTime = system.time()
    
    -- Oppdater hvert 5. minutt
    if currentTime - lastUpdate >= UPDATE_INTERVAL then
        local success, err = fetchPrecipitationData()
        if success then
            updateDisplay()
            errorMessage = ""
        else
            errorMessage = err or "Kunne ikke oppdatere data"
            drawErrorView(errorMessage)
        end
        lastUpdate = currentTime
    end
    
    -- Sjekk for konfigurasjonsendringer via BLE
    -- (håndteres i onBleData)
    
    system.sleep(1000)  -- Sjekk hvert sekund
end

function onButton()
    -- Kort trykk: Oppdater data
    local success, err = fetchPrecipitationData()
    if success then
        updateDisplay()
        errorMessage = ""
    else
        errorMessage = err or "Kunne ikke oppdatere"
        drawErrorView(errorMessage)
    end
end

function onBleData(data)
    -- Håndter BLE-kommandoer for konfigurasjon
    if data:match("^CMD:CONFIG:YR_LOCATION:") then
        yrLocation = data:match("^CMD:CONFIG:YR_LOCATION:(.+)$")
        storage.set("yr_location", yrLocation)
        ble.send("OK:YR location satt: " .. yrLocation)
        
        -- Prøv å hente data med ny location
        local success, err = fetchPrecipitationData()
        if success then
            updateDisplay()
            errorMessage = ""
            ble.send("OK:Data hentet og visning oppdatert")
        else
            errorMessage = err or "Kunne ikke hente data"
            drawErrorView(errorMessage)
            ble.send("ERROR:" .. errorMessage)
        end
    elseif data:match("^CMD:CONFIG:GITHUB_USER:") then
        -- Sett GitHub-brukernavn for user agent
        GITHUB_USERNAME = data:match("^CMD:CONFIG:GITHUB_USER:(.+)$")
        USER_AGENT = "epaper/1.0 github.com/" .. GITHUB_USERNAME
        storage.set("github_username", GITHUB_USERNAME)
        ble.send("OK:GitHub-brukernavn satt: " .. GITHUB_USERNAME)
        ble.send("OK:User agent: " .. USER_AGENT)
    elseif data == "CMD:CONFIG:STATUS" then
        -- Vis nåværende konfigurasjon
        local status = "CONFIG:YR Location: " .. (yrLocation ~= "" and yrLocation or "(ikke satt)")
        status = status .. "|GitHub User: " .. (GITHUB_USERNAME ~= "dittBrukernavn" and GITHUB_USERNAME or "(ikke satt)")
        status = status .. "|User Agent: " .. USER_AGENT
        ble.send(status)
    end
end
