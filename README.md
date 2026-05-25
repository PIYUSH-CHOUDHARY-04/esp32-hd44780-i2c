# HD44780_LCD_I2C

ESP-IDF component for driving HD44780-compatible character LCDs (e.g. 16x2, 20x4) over I²C through a PCF8574 backpack.

Provides a clean high-level wrapper around the well-known HD44780 driver by Ruslan V. Uss (`hd44780.c` / `hd44780.h`), adding:

- `printf`-style formatted printing to the LCD
- Internal cursor state tracking
- **Row pinning** — protect rows from being overwritten or cleared
- Selective row clearing
- ESP-IDF v5.x new I²C master driver integration (`driver/i2c_master.h`)
- Structured error codes via `lcd_err_t`

Target platform: **ESP32** (tested), should work on any ESP-IDF target that supports the new I²C master driver.

---

## Credits

The low-level HD44780 driver files `hd44780.c` and `hd44780.h` are taken from:

> **Ruslan V. Uss — esp-idf-lib**
> https://github.com/UncleRus/esp-idf-lib/tree/master/components/hd44780
> Originally ported from `esp-open-rtos`. BSD-licensed.

These files are largely unmodified. The only change made was adding a `void* user_ctx` field to the `hd44780_t` struct, and accepting it in `hd44780_init()`, so the I²C write callback can recover the owning device pointer (used to look up the I²C device handle).

The high-level wrapper (`lcd.c` / `lcd.h`) is written from scratch on top of the HD44780 driver.

---

## Hardware

| Component | Purpose |
|---|---|
| ESP32 dev board | Host MCU |
| HD44780-compatible LCD (16x2, 20x4, etc.) | Display |
| PCF8574 I²C backpack | Converts I²C → 4-bit parallel for the LCD |
| 5 V supply for LCD module | HD44780 needs 5 V on VDD; do NOT power it from 3.3 V |

### Wiring (default config)

| ESP32 | PCF8574 backpack |
|---|---|
| GPIO21 (SDA) | SDA |
| GPIO22 (SCL) | SCL |
| GND | GND |
| 5 V (VIN) | VCC |

Most PCF8574 backpacks have onboard 4.7 kΩ pull-ups on SDA/SCL to VCC. The ESP32's internal pull-ups are enabled in this driver as well, which is fine but not a substitute for proper external pull-ups on long wires.

A trim pot on the back of the LCD module sets the contrast (V0). **If the display shows nothing, adjust this first** before suspecting software.

### Default PCF8574 → HD44780 pin mapping

The backpack maps the 8 PCF8574 output pins to LCD control/data lines. The default mapping used here is the most common one found on cheap PCF8574 backpacks:

| PCF8574 bit | LCD signal |
|---|---|
| P0 | RS |
| P1 | RW |
| P2 | E (enable) |
| P3 | BL (backlight transistor) |
| P4 | D4 |
| P5 | D5 |
| P6 | D6 |
| P7 | D7 |

Some clone backpacks use the swapped mapping (RS/RW/E/BL on P4-P7, data on P0-P3). If nothing displays despite correct I²C communication, edit `PIN_*_BIT` defines in `lcd.c`.

### Default I²C address

`0x27` — the most common default for PCF8574 backpacks with all address jumpers floating.

Some variants (notably the PCF8574**A** chip) use `0x3F`. If `lcd_init()` reports the device isn't found, change `HD44780_I2C_ADDR` in `lcd.c`. The driver also performs an I²C scan-and-probe at init, which prints discovered addresses to the console.

---

## Repository layout

```
HD44780_LCD_I2C/
├── CMakeLists.txt        # component build config
├── README.md             # this file
├── hd44780.c             # third-party low-level HD44780 driver
├── hd44780.h
├── lcd.c                 # high-level wrapper (this project)
├── lcd.h
├── ets_sys.h             # multi-target shim for ets_delay_us
└── esp_idf_lib_helpers.h # multi-target helpers (from esp-idf-lib)
```

---

## Build / integration

This is a standard ESP-IDF component. Drop it into your project's `components/` directory:

```
my_project/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main.c
└── components/
    └── HD44780_LCD_I2C/
        ├── CMakeLists.txt
        ├── lcd.c
        ├── lcd.h
        ├── hd44780.c
        └── hd44780.h
```

In your `main/CMakeLists.txt`, add the component as a requirement:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES HD44780_LCD_I2C
    PRIV_REQUIRES spi_flash
)
```

The component's own `CMakeLists.txt` declares its dependencies on ESP-IDF I²C and GPIO drivers:

```cmake
idf_component_register(
    SRCS
        "lcd.c"
        "hd44780.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_driver_i2c
        esp_driver_gpio
)
```

Then in your application code:

```c
#include "lcd.h"
```

---

## Quick start

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"

void app_main(void) {
    if (lcd_init(&lcd_dev) != LCD_OK) {
        printf("LCD init failed!\n");
        return;
    }

    lcd_printf(&lcd_dev, "Hello, ESP32!");
    vTaskDelay(pdMS_TO_TICKS(2000));

    lcd_set_cursor(&lcd_dev, 1, 0);          // row 1, col 0
    lcd_printf(&lcd_dev, "Counter: %d", 42);
    vTaskDelay(pdMS_TO_TICKS(2000));

    lcd_clear(&lcd_dev);
}
```

A pre-configured `lcd_dev` global instance is provided in `lcd.c`. You can either use that directly (simple case) or define your own `struct lcd_i2c` instance if you need different pins/address/etc.

---

## Configuration

All hardware-specific defines live at the top of `lcd.c`:

```c
// I2C bus config
#define ESP_I2C_PORT          I2C_NUM_0
#define ESP_SDA_GPIO          GPIO_NUM_21
#define ESP_SCL_GPIO          GPIO_NUM_22
#define HD44780_I2C_CLK_FREQ  100000        // 100 kHz, safe for HD44780

// I2C device
#define HD44780_I2C_ADDR      0x27          // change to 0x3F if needed

// LCD geometry
#define LCD_COL_CNT           16
#define LCD_ROW_CNT           2

// PCF8574 → LCD pin mapping (swap to 4-7 / 0-3 if backpack is different)
#define PIN_RS_BIT  0
#define PIN_RW_BIT  1
#define PIN_E_BIT   2
#define PIN_BL_BIT  3
#define PIN_D4_BIT  4
#define PIN_D5_BIT  5
#define PIN_D6_BIT  6
#define PIN_D7_BIT  7
```

Important: `TQUEUE_DEPTH` is set to `0` to force the new I²C master driver into synchronous mode. Do **not** set this to a non-zero value — the driver switches into async mode (per ESP-IDF docs and known issues) which causes errors to be silently dropped, and gives confusing "device found at every address" results during scans.

---

## API reference

All wrapper functions return `lcd_err_t` (a superset of `esp_err_t`) except `lcd_printf()`, which returns the number of characters written (or a negative error code).

Error codes defined in `lcd.h`:

```c
LCD_OK                       // == ESP_OK
LCD_ERR_NEW_MASTER_BUS       // i2c_new_master_bus() failed
LCD_ERR_MASTER_ADD_DEVICE    // i2c_master_bus_add_device() failed
LCD_ERR_INVALID_ARG          // bad argument (e.g. row > LCD_ROW_CNT)
LCD_WARN_NOTHING_TO_CLEAR    // both rows pinned, lcd_clear_row() is a no-op
LCD_ERR_PUTC_FAILED          // a hd44780_putc() call failed
LCD_ERR_GOTOXY_FAILED        // a hd44780_gotoxy() call failed
LCD_ERR_CLEAR_FAILED         // a hd44780_clear() call failed
LCD_ERR_INIT_FAILED          // hd44780_init() failed
```

### `esp_err_t lcd_init(struct lcd_i2c *dev)`

Initialize the I²C bus, register the PCF8574 as an I²C device, and run the HD44780 init sequence (4-bit mode setup, function set, display on, clear).

Must be called once before any other function. Returns `LCD_OK` on success.

### `esp_err_t lcd_clear(struct lcd_i2c *dev)`

Clears the entire display. Also resets internal state:

- Cursor moves to (0, 0)
- Row pinning is reset to "no rows pinned"

This is the "nuclear" clear — for clearing only unpinned rows, use `lcd_clear_row()` instead.

### `esp_err_t lcd_clear_row(struct lcd_i2c *dev)`

Clears rows depending on pinning state:

| Pinning state | Behavior |
|---|---|
| No rows pinned | Same as `lcd_clear()` — clears whole display |
| Row 0 pinned | Clears row 1 only |
| Row 1 pinned | Clears row 0 only |
| Both rows pinned | No-op, returns `LCD_WARN_NOTHING_TO_CLEAR` |

After clearing, the cursor is positioned at the start of the cleared row.

### `int lcd_printf(struct lcd_i2c *dev, const char *fmt, ...)`

`printf`-style formatted print to the LCD. Returns the number of characters written, or a negative error code.

Behavior:

1. Saves the current cursor position as `old_pos`.
2. If any row is pinned, calls `lcd_clear_row()` to clear only the unpinned row(s).
3. If no rows are pinned, calls `lcd_clear()` to clear everything, then restores cursor to `old_pos`.
4. Formats `fmt`/`args` into an internal buffer of size `LCD_ROW_CNT * LCD_COL_CNT + 1`.
5. Writes characters to the LCD, automatically wrapping from row 0 to row 1 at column 16.
6. Restores the cursor position to `old_pos` at the end.

So each `lcd_printf` call effectively **replaces** the visible content of unpinned rows.

```c
lcd_printf(&lcd_dev, "Temp: %d C", 23);     // unpinned: rewrites everything
lcd_row_pin(&lcd_dev, 0);                    // protect row 0
lcd_printf(&lcd_dev, "Counter: %lu", n);    // only row 1 gets rewritten
```

### `esp_err_t lcd_row_pin(struct lcd_i2c *dev, uint8_t row)`

Pin a row. Pinned rows:

- Are **not cleared** by `lcd_printf()` or `lcd_clear_row()`
- Are **not written to** by subsequent `lcd_printf()` calls

After pinning, the cursor is moved to the next writable region:

- Pin row 0 → cursor moves to (1, 0)
- Pin row 1 → cursor moves to (0, 0)
- Both rows pinned → cursor moves to (1, 15) — but at this point any `lcd_printf()` call will simply return -1 since there is nowhere to write

Pinning is implemented via a 2-bit field `lcd_row_pinning` in the device struct (bit 0 = row 0 pinned, bit 1 = row 1 pinned).

### `esp_err_t lcd_row_unpin(struct lcd_i2c *dev, uint8_t row)`

Unpin a row, restoring it to a writable state. Resets the cursor to (0, 0).

### `esp_err_t lcd_set_cursor(struct lcd_i2c *dev, uint8_t row, uint8_t col)`

Move the LCD hardware cursor and update the internal cursor state. Does not check whether the destination row is pinned (use with care if you have pinning in effect).

---

## Internals

### Device struct (`struct lcd_i2c`)

Holds everything needed to operate the LCD:

```c
struct lcd_i2c {
    hd44780_t lcd;                      // HD44780 driver descriptor
    const uint8_t lcd_cols;             // column count
    const uint8_t dev_i2c_addr;         // PCF8574 I²C address
    const uint8_t dev_i2c_sda_pin;
    const uint8_t dev_i2c_scl_pin;
    const uint32_t dev_i2c_freq;
    const uint8_t esp_i2c_port;
    uint8_t lcd_row_pinning : 2;        // bit 0 = row 0 pinned, bit 1 = row 1 pinned
    uint8_t lcd_cursor_pos_row;         // wrapper-tracked cursor row
    uint8_t lcd_cursor_pos_col;         // wrapper-tracked cursor column
    i2c_master_bus_handle_t bus_handle;
    i2c_master_dev_handle_t dev_handle;
};
```

### How writes reach the LCD

```
   lcd_printf("hello")
        │
        ▼
   hd44780_putc('h')
        │ encodes a 4-bit nibble + E pulse + RS + backlight into one byte
        ▼
   write_cb(lcd, byte)              ← registered with the HD44780 driver
        │ recovers struct lcd_i2c* from lcd->user_ctx
        ▼
   i2c_master_transmit(dev_handle, &byte, 1, 1000)
        │
        ▼
   PCF8574 latches the byte on its 8 outputs
        │
        ▼
   HD44780 sees E pulse, samples D7-D4 + RS, executes command/data
```

Each HD44780 byte requires 4 I²C transactions:

1. Upper nibble + E=1 (data setup)
2. Upper nibble + E=0 (latches data on falling edge)
3. Lower nibble + E=1
4. Lower nibble + E=0

This is why high-level operations like `lcd_clear()` are visibly slow on an oscilloscope — every command is multiple I²C transactions.

### Cursor position tracking

The HD44780 has its own internal cursor in DDRAM. The wrapper layer also tracks a logical cursor (`lcd_cursor_pos_row`, `lcd_cursor_pos_col`) so that:

- `lcd_printf()` can save and restore cursor position around clear operations
- `lcd_row_pin()` / `lcd_row_unpin()` know where to leave the cursor
- The wrapper's view of the display state stays consistent with the hardware

Both must be kept in sync. Every wrapper function that issues a `hd44780_gotoxy()` also updates the tracked position.

### Row wrap during `lcd_printf`

When writing more characters than fit on row 0, the loop in `lcd_printf` issues an explicit `hd44780_gotoxy(0, 1)` at column 16 to jump to row 1. This is necessary because the HD44780's DDRAM layout is non-contiguous:

```
row 0: addresses 0x00 .. 0x0F (visible)
       addresses 0x10 .. 0x27 (invisible — auto-incremented but off-screen)
row 1: addresses 0x40 .. 0x4F (visible)
```

Without the explicit `gotoxy`, characters past column 15 would land in invisible DDRAM rather than wrapping to row 1.

### Pinning logic

`lcd_row_pinning` is a 2-bit field representing which rows are protected:

| Value | Bit 1 (row 1) | Bit 0 (row 0) | Meaning |
|---|---|---|---|
| 0 (0b00) | unpinned | unpinned | both rows writable |
| 1 (0b01) | unpinned | pinned | only row 1 writable |
| 2 (0b10) | pinned | unpinned | only row 0 writable |
| 3 (0b11) | pinned | pinned | nothing writable |

`lcd_clear_row()` and `lcd_printf()` consult this field to decide what to clear and where to write.

### I²C driver mode

ESP-IDF's new I²C master driver (`driver/i2c_master.h`, ESP-IDF 5.x) has two modes:

- **Synchronous** — `trans_queue_depth = 0`, no event callback registered. `i2c_master_transmit()` blocks until the transaction completes. Errors are returned directly.
- **Asynchronous** — `trans_queue_depth > 0`. Transactions are queued; `i2c_master_transmit()` returns immediately. Errors come back via the registered callback. If no callback is registered, errors are silently dropped.

This driver uses **synchronous mode** (`TQUEUE_DEPTH = 0`). For slow peripherals like the HD44780, the async mode is unnecessary and has known issues (the driver's full-bus scan reports false positives at every address when async, because the driver doesn't surface NACK errors back to the caller).

### Init flow

```
lcd_init()
  ├─ i2c_new_master_bus()                  → bus_handle
  ├─ i2c_master_bus_add_device()           → dev_handle
  └─ hd44780_init(lcd, ctx=lcd_i2c_dev)    → runs HD44780 4-bit init sequence
       ├─ 3× write_nibble(FUNC_SET | 8_BIT) — required by HD44780 datasheet
       ├─ write_nibble(FUNC_SET) — switch to 4-bit
       ├─ write_byte(FUNC_SET | 2_LINES | font)
       ├─ display off
       ├─ clear
       ├─ entry mode (auto-increment)
       └─ display on
```

The HD44780 init must run with specific timing delays, because the HD44780 has no busy-wait checking exposed over the PCF8574 — the driver uses worst-case datasheet delays instead.

---

## Known limitations

- **Backlight is on by default** — set in the static initializer of `lcd_dev`. Use `hd44780_switch_backlight()` directly to toggle it at runtime. Why on by default? Because this driver was written in a dark room, and you can't read an LCD with your pupils dilated to f/1.4 anyway.
- **No support for >2 row LCDs in the wrapper** — `lcd_row_pin`, `lcd_clear_row`, etc. are hardcoded for 2-row LCDs. The underlying HD44780 driver supports up to 4 rows; extending the wrapper is straightforward.
- **`lcd_printf` clears before writing** — every call wipes the relevant rows. There is no append mode. This is intentional (matches the "live status display" use case) but may not suit all applications.

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `lcd_init` fails with `LCD_ERR_NEW_MASTER_BUS` | I²C port already in use, or GPIO conflict |
| `lcd_init` fails with `LCD_ERR_MASTER_ADD_DEVICE` | Wrong I²C address, or PCF8574 not on the bus |
| I²C scan reports devices at every address | `TQUEUE_DEPTH` was changed to non-zero (async mode, errors silently dropped) — keep it at 0 |
| Display is on but blank | **Contrast pot needs adjustment** (most common cause). Adjust the trim pot on the back of the LCD module. |
| Display shows solid black squares on row 0 only | Contrast pot is at the other extreme, OR LCD is in 8-bit mode (init sequence failed midway) |
| Garbled characters | I²C signal integrity (try lowering `HD44780_I2C_CLK_FREQ` to 50 kHz, shorten wires, add external 4.7 kΩ pull-ups) |
| Display works briefly then garbles | Power supply can't handle current spikes — add a 470 µF capacitor across the LCD VCC/GND |
| Pin mapping appears wrong (backlight responds but text doesn't) | Try the alternate PCF8574 → LCD pin mapping (`PIN_RS_BIT=4`, `PIN_BL_BIT=7`, data on bits 0-3) |

---

## License

The HD44780 driver files (`hd44780.c`, `hd44780.h`) are © Ruslan V. Uss and licensed BSD-3-Clause. See the original source at https://github.com/UncleRus/esp-idf-lib.

The wrapper layer (`lcd.c`, `lcd.h`) is provided under the same BSD-3-Clause terms.
