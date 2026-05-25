idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES HD44780_LCD_I2C
    PRIV_REQUIRES spi_flash esp_timer
)
