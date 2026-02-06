-- Display-adapter for apper som forventer display.* (f.eks. yr-regn.lua).
-- Mapper til HAL.screen_*.

local Display = {}

function Display.clear()
    HAL.screen_clear()
end

function Display.refresh()
    HAL.screen_update()
end

function Display.text(x, y, text, size)
    size = size or 1
    HAL.screen_setTextSize(size)
    HAL.screen_drawText(x, y, text)
end

function Display.line(x1, y1, x2, y2)
    HAL.screen_drawLine(x1, y1, x2, y2)
end

function Display.circle(x, y, r, fill)
    if fill then
        HAL.screen_fillCircle(x, y, r)
    else
        HAL.screen_drawCircle(x, y, r)
    end
end

function Display.rect(x, y, w, h, fill)
    if fill then
        HAL.screen_fillRect(x, y, w, h)
    else
        HAL.screen_drawRect(x, y, w, h)
    end
end

function Display.triangle(x1, y1, x2, y2, x3, y3, fill)
    if fill then
        HAL.screen_fillTriangle(x1, y1, x2, y2, x3, y3)
    else
        HAL.screen_drawLine(x1, y1, x2, y2)
        HAL.screen_drawLine(x2, y2, x3, y3)
        HAL.screen_drawLine(x3, y3, x1, y1)
    end
end

function Display.width()
    return HAL.screen_getWidth()
end

function Display.height()
    return HAL.screen_getHeight()
end

function Display.textWidth(text, size)
    size = size or 1
    HAL.screen_setTextSize(size)
    return HAL.screen_getTextWidth(text)
end

-- Sett global display slik at apper som yr-regn kan bruke display.* uten å endre kode.
display = Display
return Display
