return {
    -- Hardware definitions
    screen_type = "monochrome_spi",      -- Options: "monochrome_spi", "oled_i2c", "tft_touch"
    keyboard_type = "matrix_4x4",        -- Options: "matrix_4x4", "custom_spi", "uart_serial"
    
    -- Pin Definitions (LilyGo T5: knapp er IO39)
    gpio_pins = {
        led = 2,
        button = 39,
        sensor1 = 12,
        sensor2 = 13
    },
    
    -- Peripherals (TF/SD: CS 13, MOSI 15, SCK 14, MISO 2)
    sd_cs_pin = 13,
    
    -- Network
    wifi_ssid = "RoseNet",
    wifi_pass = "SecretPassword",
    
    -- System Settings
    debug_mode = true
}
