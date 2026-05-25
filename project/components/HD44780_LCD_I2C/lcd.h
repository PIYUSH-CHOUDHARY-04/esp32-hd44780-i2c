#ifndef __LCD_H__
#define __LCD_H__

/**
 * @file lcd.h
 * @brief High-level HD44780 LCD driver wrapper for ESP-IDF using PCF8574 I2C backpack.
 *
 * This module provides:
 * - Formatted LCD printing
 * - Cursor state management
 * - Persistent row pinning
 * - Partial row clearing
 * - ESP-IDF I2C integration
 *
 * Built on top of the HD44780 low-level driver.
 */

#include <stdint.h>
#include <esp_err.h>
#include "hd44780.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ESP_ERR_LCD_BASE                 0x5000
typedef enum {

    // Generic
    LCD_OK                              = ESP_OK,

    // I2C operation failures
    LCD_ERR_NEW_MASTER_BUS              = ESP_ERR_LCD_BASE + 1,
    LCD_ERR_MASTER_ADD_DEVICE           = ESP_ERR_LCD_BASE + 2,

    // Invalid usage
    LCD_ERR_INVALID_ARG                 = ESP_ERR_INVALID_ARG,

    // State errors
    LCD_WARN_NOTHING_TO_CLEAR           = ESP_ERR_LCD_BASE + 3,

    // HD44780 operation failures
    LCD_ERR_PUTC_FAILED                 = ESP_ERR_LCD_BASE + 4,
    LCD_ERR_GOTOXY_FAILED               = ESP_ERR_LCD_BASE + 5,
    LCD_ERR_CLEAR_FAILED                = ESP_ERR_LCD_BASE + 6,
    LCD_ERR_INIT_FAILED                 = ESP_ERR_LCD_BASE + 7

} lcd_err_t;

/**
 * @brief LCD device descriptor for HD44780 + PCF8574 configuration.
 *
 * This structure stores:
 * - HD44780 descriptor
 * - I2C bus/device configuration
 * - Cursor state
 * - Row pinning state
 * - ESP-IDF I2C handles
 */
struct lcd_i2c {
   
    /**
     * @brief Underlying HD44780 descriptor.
     */
    hd44780_t lcd;
    
    /**
     * @brief Number of LCD columns.
     */
    const uint8_t lcd_cols;

    /**
     * @brief I2C device address of PCF8574.
     */
    const uint8_t dev_i2c_addr;

    /**
     * @brief SDA GPIO pin number.
     */
    const uint8_t dev_i2c_sda_pin;

    /**
     * @brief SCL GPIO pin number.
     */
    const uint8_t dev_i2c_scl_pin;

    /**
     * @brief I2C clock frequency in Hz.
     */
    const uint32_t dev_i2c_freq;

    /**
     * @brief ESP-IDF I2C controller port.
     */
    const uint8_t esp_i2c_port;

    /**
     * @brief LCD row pinning bitmap.
     *
     * Bit layout:
     * - bit0 -> row 0 pinned
     * - bit1 -> row 1 pinned
     *
     * Pinned rows are protected from:
     * - lcd_printf()
     * - lcd_clear_row()
     */
    uint8_t lcd_row_pinning     : 2;    // b0 for row0, b1 for row1, if set, chars can't be put into that row

    /**
     * @brief Current cursor row tracked by wrapper layer.
     */
    uint8_t lcd_cursor_pos_row;

    /**
     * @brief Current cursor column tracked by wrapper layer.
     */
    uint8_t lcd_cursor_pos_col;

    /**
     * @brief ESP-IDF I2C master bus handle.
     */
    i2c_master_bus_handle_t bus_handle;

    /**
     * @brief ESP-IDF I2C device handle.
     */
    i2c_master_dev_handle_t dev_handle;
};

/**
 * @brief Global LCD device instance.
 */
extern struct lcd_i2c lcd_dev;

//=============================================== internals (exposed for hd44780)==========================================

/**
 * @brief Low-level HD44780 write callback.
 *
 * This callback is used internally by the HD44780 driver
 * to transmit one byte through the PCF8574 I2C expander.
 *
 * @param lcd Pointer to HD44780 descriptor.
 * @param data Data byte to transmit.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_FAIL or ESP-IDF I2C error code on failure
 */
esp_err_t write_cb(const hd44780_t *lcd, uint8_t data);

//=============================================== API ================================================

/**
 * @brief Initialize LCD driver and underlying I2C bus/device.
 *
 * This function:
 * - Creates I2C master bus
 * - Adds PCF8574 device
 * - Initializes HD44780 LCD
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 *
 * @return
 *      - LCD_OK on success
 *      - LCD_ERR_** on failure
 */
esp_err_t lcd_init(struct lcd_i2c* lcd_i2c_dev);

/**
 * @brief Clear entire LCD and reset internal state.
 *
 * This function:
 * - Clears LCD display
 * - Resets cursor position
 * - Clears row pinning state
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 *
 * @return
 *      - LCD_OK on success
 *      - LCD_ERR_** code on failure
 */
esp_err_t lcd_clear(struct lcd_i2c* lcd_i2c_dev);

/**
 * @brief Clear only unpinned LCD rows.
 *
 * Behavior:
 * - No pinned rows:
 *      clears entire display
 * - One pinned row:
 *      clears only the unpinned row
 * - Both rows pinned:
 *      nothing is cleared
 *
 * @note
 * Cursor positioning assumptions are maintained internally
 * by row pinning and cursor management APIs.
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 *
 * @return
 *      - LCD_OK on success
 *      - Positive value if nothing cleared (LCD_WARN_**)
 *      - Negative value on internal failure (LCD_ERR_**)
 */
esp_err_t lcd_clear_row(struct lcd_i2c* lcd_i2c_dev);

/**
 * @brief Print formatted text to LCD.
 *
 * Supports standard printf-style formatting using variadic arguments.
 *
 * Behavior depends on current row pinning state.
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 * @param fmt Format string.
 * @param ... Variable arguments.
 *
 * @return
 *      - Number of characters written
 *      - Negative value on failure
 */
int lcd_printf(struct lcd_i2c* lcd_i2c_dev, const char *fmt, ...);

/**
 * @brief Pin a row to make its contents persistent.
 *
 * Pinned rows are protected from:
 * - lcd_printf()
 * - lcd_clear_row()
 *
 * Cursor position is automatically updated to the next
 * writable region.
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 * @param row LCD row number.
 *
 * @return
 *      - LCD_OK on success
 *      - LCD_ERR_** on failure
 */
esp_err_t lcd_row_pin(struct lcd_i2c* lcd_i2c_dev, uint8_t row);

/**
 * @brief Remove row pinning.
 *
 * After unpinning, the row becomes writable again.
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 * @param row LCD row number.
 *
 * @return
 *      - LCD_OK on success
 *      - LCD_ERR_** on failure
 */
esp_err_t lcd_row_unpin(struct lcd_i2c* lcd_i2c_dev, uint8_t row);

/**
 * @brief Set LCD cursor position.
 *
 * Updates both:
 * - LCD hardware cursor
 * - Internal wrapper cursor state
 *
 * @param lcd_i2c_dev Pointer to LCD descriptor.
 * @param row Target row.
 * @param col Target column.
 *
 * @return
 *      - LCD_OK on success
 *      - LCD_ERR_** on failure
 */
esp_err_t lcd_set_cursor(struct lcd_i2c* lcd_i2c_dev, uint8_t row, uint8_t col);

#ifdef __cplusplus
}
#endif

#endif  
