/**
 * @file main.c
 * @brief Example application demonstrating the HD44780_LCD_I2C component.
 *
 * This example walks through every public API exposed by `lcd.h`:
 *   - lcd_init()        : bring up the I2C bus and the LCD
 *   - lcd_printf()      : PRINT_MSG-style formatted output
 *   - lcd_set_cursor()  : move cursor to a specific (row, col)
 *   - lcd_row_pin()     : protect a row from being overwritten
 *   - lcd_row_unpin()   : release a previously pinned row
 *   - lcd_clear_row()   : clear only unpinned rows
 *   - lcd_clear()       : nuke everything and reset state
 *
 * It also serves as a smoke test: if you see all the labeled stages roll
 * past on the LCD, the wiring, contrast, pin mapping, and I2C bus are all
 * healthy.
 *
 * Build: standard ESP-IDF component. Add HD44780_LCD_I2C to your
 * project's `components/` directory and `REQUIRES HD44780_LCD_I2C`
 * in your main CMakeLists.txt.
 */

#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_err.h"
#include "esp_timer.h"

#include "lcd.h"
#include "logging.h"

/* ---- timing knobs --------------------------------------------------- */
#define STAGE_DELAY_MS      3000        // how long each stage stays on screen
#define SHORT_DELAY_MS      1000        // shorter pause between related ops
#define COUNTER_TICK_MS     500         // counter update interval
#define COUNTER_MAX         10          // counter runs 0..COUNTER_MAX-1


/* ---- helpers -------------------------------------------------------- */
static void print_chip_info(void)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    PRINT_MSG("\n*** ESP32 boot OK ***\n");
    PRINT_MSG("Chip      : %s, %d core(s), revision %d\n",
           CONFIG_IDF_TARGET, chip_info.cores, chip_info.revision);
    PRINT_MSG("Flash     : %" PRIu32 " MB %s\n",
           flash_size / (1024 * 1024),
           (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    PRINT_MSG("Free heap : %" PRIu32 " bytes\n\n", esp_get_free_heap_size());
}

static void stage_banner(const char *title)
{
    PRINT_MSG("\n=== %s ===\n", title);
}


/* ---- main ----------------------------------------------------------- */
void app_main(void)
{
    print_chip_info();

    /* ---------------------------------------------------------------
     * Stage 0 : bring up the LCD
     * --------------------------------------------------------------- */
    stage_banner("init");
    esp_err_t ret = lcd_init(&lcd_dev);
    if (ret != LCD_OK) {
        PRINT_MSG("lcd_init() failed: 0x%x\n", ret);
        PRINT_MSG("Check wiring, I2C address, contrast pot, and power.\n");
        return;
    }
    PRINT_MSG("lcd_init() OK\n");

    /* ---------------------------------------------------------------
     * Stage 1 : simple greeting
     * Shows: basic lcd_printf to row 0 (default cursor).
     * --------------------------------------------------------------- */
    stage_banner("greeting");
    lcd_printf(&lcd_dev, " Hello, ESP32! ");
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    /* ---------------------------------------------------------------
     * Stage 2 : auto-wrap from row 0 to row 1
     * Shows: lcd_printf wraps onto row 1 when the string exceeds
     *        the column count. Anything past 32 chars is dropped.
     * --------------------------------------------------------------- */
    stage_banner("auto-wrap");
    lcd_printf(&lcd_dev,
               "1234567890ABCDEFGHIJabcdefghij...");  /* 32+ chars */
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    /* ---------------------------------------------------------------
     * Stage 3 : explicit cursor placement
     * Shows: lcd_set_cursor + lcd_printf together place text on a
     *        specific row.
     * --------------------------------------------------------------- */
    stage_banner("explicit cursor");
    lcd_clear(&lcd_dev);
    lcd_set_cursor(&lcd_dev, 0, 0);
    lcd_printf(&lcd_dev, "Row 0 top");
    vTaskDelay(pdMS_TO_TICKS(SHORT_DELAY_MS));

    lcd_set_cursor(&lcd_dev, 1, 0);
    lcd_printf(&lcd_dev, "Row 1 bottom");
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    /* ---------------------------------------------------------------
     * Stage 4 : row pinning
     * Shows: row 0 is pinned with a static label. Subsequent
     *        lcd_printf calls cannot touch it -- they only refresh
     *        row 1. A counter ticks in row 1 while the label stays
     *        put.
     * --------------------------------------------------------------- */
    stage_banner("row pinning");
    lcd_clear(&lcd_dev);
    lcd_set_cursor(&lcd_dev, 0, 0);
    lcd_printf(&lcd_dev, "  Status: OK  ");

    if (lcd_row_pin(&lcd_dev, 0) != LCD_OK) {
        PRINT_MSG("row pin failed\n");
    } else {
        PRINT_MSG("row 0 pinned\n");
    }

    for (int i = 0; i < COUNTER_MAX; i++) {
        lcd_printf(&lcd_dev, "Counter: %d", i);
        vTaskDelay(pdMS_TO_TICKS(COUNTER_TICK_MS));
    }

    /* ---------------------------------------------------------------
     * Stage 5 : unpin and clear
     * Shows: row 0 becomes writable again after lcd_row_unpin().
     * --------------------------------------------------------------- */
    stage_banner("unpin");
    if (lcd_row_unpin(&lcd_dev, 0) != LCD_OK) {
        PRINT_MSG("row unpin failed\n");
    }
    lcd_printf(&lcd_dev, " Row 0 free again ");
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    /* ---------------------------------------------------------------
     * Stage 6 : pin row 1, write to row 0 only
     * Shows: pinning the bottom row instead of the top.
     * --------------------------------------------------------------- */
    stage_banner("pin row 1");
    lcd_clear(&lcd_dev);
    lcd_set_cursor(&lcd_dev, 1, 0);
    lcd_printf(&lcd_dev, "  -- footer --  ");
    lcd_row_pin(&lcd_dev, 1);

    lcd_printf(&lcd_dev, "Heap: %" PRIu32, esp_get_free_heap_size());
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    lcd_printf(&lcd_dev, "Time: %lld ms",
               (long long)(esp_timer_get_time() / 1000));
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    /* ---------------------------------------------------------------
     * Stage 7 : selective clear via lcd_clear_row()
     * Shows: with row 1 pinned, lcd_clear_row() wipes only row 0.
     * --------------------------------------------------------------- */
    stage_banner("selective clear");
    lcd_clear_row(&lcd_dev);   /* row 1 still pinned, so only row 0 wiped */
    vTaskDelay(pdMS_TO_TICKS(SHORT_DELAY_MS));
    lcd_printf(&lcd_dev, "Row 0 cleared");
    vTaskDelay(pdMS_TO_TICKS(STAGE_DELAY_MS));

    /* ---------------------------------------------------------------
     * Stage 8 : final cleanup
     * --------------------------------------------------------------- */
    stage_banner("done");
    lcd_row_unpin(&lcd_dev, 1);
    lcd_clear(&lcd_dev);
    lcd_printf(&lcd_dev, "  demo complete  ");

    PRINT_MSG("All stages finished. Idle loop entering.\n");

    /* Idle forever -- LCD keeps last message. */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
