# HD44780_LCD_I2C

ESP-IDF component for driving HD44780-compatible character LCDs (e.g. 16x2, 20x4) over I²C through a PCF8574 backpack.

A clean high-level wrapper around the well-known HD44780 driver by Ruslan V. Uss (`hd44780.c` / `hd44780.h`), adding:

- `printf`-style formatted printing to the LCD
- Internal cursor state tracking
- **Row pinning** — protect rows from being overwritten or cleared
- Selective row clearing
- ESP-IDF v5.x new I²C master driver integration (`driver/i2c_master.h`)
- Thread-safe runtime operations via an internal recursive mutex
- Optional user-supplied I²C configuration (pins / port / clock source)
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

## Design model

This component is built as a **singleton driver**:

- A single LCD device instance is maintained internally inside `lcd.c`.
- The public API exposes only functions — no struct pointer is passed by callers.
- Callers cannot reach into the device state. All access is through the API.

This matches the typical "one LCD per project" use case and keeps the public surface small. If your project ever needs multiple LCDs simultaneously, this component would need to be reworked into a handle-based API; that's a future change, not supported today.

### Threading model

- Runtime operations (`lcd_printf`, `lcd_clear`, `lcd_clear_row`, `lcd_row_pin`, `lcd_row_unpin`, `lcd_set_cursor`) are **thread-safe** — they are protected by an internal recursive mutex.
- `lcd_init()` and `lcd_deinit()` are **NOT thread-safe**. They must be called from a single context (typically `app_main`) before / after any task uses the LCD.

The recursive mutex means a thread-safe API may internally call another thread-safe API (e.g. `lcd_printf` calling `lcd_clear`) without deadlocking.

### Synchronous I²C

The new ESP-IDF I²C master driver supports both sync and async modes. This component uses **synchronous mode** (`trans_queue_depth = 0`). For slow peripherals like the HD44780, async mode is unnecessary and has known issues (errors are silently dropped, scans report false ACKs at every address). Do not change `TQUEUE_DEPTH` in `lcd.c`.

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

Some clone backpacks use the swapped mapping (RS/RW/E/BL on P4-P7, data on P0-P3). If nothing displays despite correct I²C communication, edit the `PIN_*_BIT` defines in `lcd.c`.

### Default I²C address

`0x27` — the most common default for PCF8574 backpacks with all address jumpers floating.

Some variants (notably the PCF8574**A** chip) use `0x3F`. If `lcd_init()` reports the device isn't found, change `PCF8574_I2C_ADDR_DEFAULT` in `lcd.c`.

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

Standard ESP-IDF component. Drop it into your project's `components/` directory:

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

The component's own `CMakeLists.txt`:

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

The simplest possible usage — let the driver use its default I²C configuration (port 0, GPIO21/22):

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd.h"

void app_main(void) {
    if (lcd_init(NULL) != LCD_OK) {     // NULL = use defaults
        printf("LCD init failed!\n");
        return;
    }

    lcd_printf("Hello, ESP32!");
    vTaskDelay(pdMS_TO_TICKS(2000));

    lcd_set_cursor(1, 0);                // row 1, col 0
    lcd_printf("Counter: %d", 42);
    vTaskDelay(pdMS_TO_TICKS(2000));

    lcd_clear();
}
```

If you need to override the default I²C pins / port / clock source, pass a `struct lcd_user_handle`:

```c
struct lcd_user_handle cfg = {
    .esp_i2c_sda_pin_usr = GPIO_NUM_25,
    .esp_i2c_scl_pin_usr = GPIO_NUM_26,
    .esp_i2c_clk_src_usr = I2C_CLK_SRC_DEFAULT,
    .esp_i2c_port_usr    = I2C_NUM_1,
};
lcd_init(&cfg);
```

The PCF8574 address, clock frequency, LCD geometry, and PCF8574-to-HD44780 pin mapping are still compile-time constants in `lcd.c`. The `lcd_user_handle` only covers ESP-side I²C configuration that's most likely to differ between projects.

---

## Configuration

Hardware-specific defines live at the top of `lcd.c`:

```c
// Default ESP I2C bus config (overridable via lcd_user_handle)
#define ESP_I2C_PORT_DEFAULT       I2C_NUM_0
#define ESP_SDA_GPIO_DEFAULT       GPIO_NUM_21
#define ESP_SCL_GPIO_DEFAULT       GPIO_NUM_22
#define ESP_I2C_CLK_SRC_DEFAULT    I2C_CLK_SRC_DEFAULT

// Bus knobs — DO NOT CHANGE
#define GLITCH_IGN_CNT             7
#define INTR_PRIOR                 0
#define TQUEUE_DEPTH               0     // sync mode; see "Synchronous I²C" above
#define EN_INTR_PULLUP             true

// PCF8574 device — compile-time, not overridable at runtime
#define PCF8574_I2C_ADDR_DEFAULT      0x27
#define PCF8574_I2C_CLK_FREQ_DEFAULT  100000   // 100 kHz, safe for HD44780
#define PCF8574_ADDR_LEN_DEFAULT      I2C_ADDR_BIT_LEN_7

// LCD geometry
#define LCD_COL_CNT                16
#define LCD_ROW_CNT                2

// PCF8574 → LCD pin mapping (swap if backpack is wired differently)
#define PIN_RS_BIT  0
#define PIN_RW_BIT  1
#define PIN_E_BIT   2
#define PIN_BL_BIT  3
#define PIN_D4_BIT  4
#define PIN_D5_BIT  5
#define PIN_D6_BIT  6
#define PIN_D7_BIT  7
```

**Important:** `TQUEUE_DEPTH` is set to `0` to force the I²C master driver into synchronous mode. Setting it to non-zero switches the driver to async mode, in which errors are silently dropped and a full-bus scan reports false positives at every address. Keep it at 0 for this driver.

---

## API reference

All wrapper functions return `lcd_err_t` (a superset of `esp_err_t`) except `lcd_printf()`, which returns the number of characters written (or a negative error code).

Error codes defined in `lcd.h`:

```c
LCD_OK                       // == ESP_OK
LCD_ERR_NEW_MASTER_BUS       // i2c_new_master_bus() failed
LCD_ERR_MASTER_ADD_DEVICE    // i2c_master_bus_add_device() failed
LCD_ERR_INVALID_ARG          // bad argument (e.g. row out of range)
LCD_ERR_INVALID_STATE        // double-init, or operation before init
LCD_WARN_NOTHING_TO_CLEAR    // both rows pinned, lcd_clear_row() is a no-op
LCD_ERR_PUTC_FAILED          // a hd44780_putc() call failed
LCD_ERR_GOTOXY_FAILED        // a hd44780_gotoxy() call failed
LCD_ERR_CLEAR_FAILED         // a hd44780_clear() call failed
LCD_ERR_INIT_FAILED          // hd44780_init() failed
```

### `esp_err_t lcd_init(struct lcd_user_handle *luh)`

Initialize the I²C bus, register the PCF8574 as an I²C device, run the HD44780 init sequence (4-bit mode setup, function set, display on, clear), and create internal synchronization primitives.

- `luh = NULL` → use compile-time defaults for SDA, SCL, clock source, and I²C port.
- `luh != NULL` → use the supplied values for ESP-side I²C configuration.

Must be called exactly once before any other API. Calling it twice returns `ESP_ERR_INVALID_STATE`. Not thread-safe — call from `app_main` only.

### `esp_err_t lcd_deinit(void)`

Tear down the I²C device, delete the I²C master bus, and release internal synchronization primitives. After this call, no other LCD API may be used until `lcd_init()` is called again.

Not thread-safe. Returns `ESP_ERR_INVALID_STATE` if the LCD wasn't initialized.

### `esp_err_t lcd_clear(void)`

Clears the entire display. Also resets internal state:

- Cursor moves to (0, 0)
- Row pinning is reset to "no rows pinned"

This is the "nuclear" clear — for clearing only unpinned rows, use `lcd_clear_row()` instead.

### `esp_err_t lcd_clear_row(void)`

Clears rows depending on pinning state:

| Pinning state | Behavior |
|---|---|
| No rows pinned | Same as `lcd_clear()` — clears whole display |
| Row 0 pinned | Clears row 1 only |
| Row 1 pinned | Clears row 0 only |
| Both rows pinned | No-op, returns `LCD_WARN_NOTHING_TO_CLEAR` |

After clearing, the cursor is positioned at the start of the cleared row.

### `int lcd_printf(const char *fmt, ...)`

`printf`-style formatted print to the LCD. Returns the number of characters written, or a negative error code.

Behavior:

1. Saves the current cursor position as `old_pos`.
2. If any row is pinned, calls `lcd_clear_row()` to clear only the unpinned row(s).
3. If no rows are pinned, calls `lcd_clear()` to clear everything, then restores cursor to `old_pos`.
4. Formats `fmt`/`args` into an internal buffer of size `LCD_ROW_CNT * LCD_COL_CNT + 1`.
5. Writes characters to the LCD, automatically wrapping from row 0 to row 1 at column 16.
6. Restores the cursor position to `old_pos` at the end.

Each `lcd_printf` call effectively **replaces** the visible content of unpinned rows.

```c
lcd_printf("Temp: %d C", 23);     // unpinned: rewrites everything
lcd_row_pin(0);                    // protect row 0
lcd_printf("Counter: %lu", n);    // only row 1 gets rewritten
```

### `esp_err_t lcd_row_pin(uint8_t row)`

Pin a row. Pinned rows:

- Are **not cleared** by `lcd_printf()` or `lcd_clear_row()`
- Are **not written to** by subsequent `lcd_printf()` calls

After pinning, the cursor is moved to the next writable region:

- Pin row 0 → cursor moves to (1, 0)
- Pin row 1 → cursor moves to (0, 0)
- Both rows pinned → cursor moves to (1, 15) — at this point `lcd_printf()` returns -1 since there is nowhere to write

### `esp_err_t lcd_row_unpin(uint8_t row)`

Unpin a row, restoring it to a writable state. Resets the cursor to (0, 0).

### `esp_err_t lcd_set_cursor(uint8_t row, uint8_t col)`

Move the LCD hardware cursor and update the internal cursor state. Does not check whether the destination row is pinned (use with care if pinning is in effect).

---

## Internals

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

- **Synchronous** — `trans_queue_depth = 0`. `i2c_master_transmit()` blocks until the transaction completes. Errors are returned directly. Compatible with `i2c_master_probe`.
- **Asynchronous** — `trans_queue_depth > 0`. Transactions are queued; `i2c_master_transmit()` returns immediately. Errors come back via a registered callback. **If no callback is registered, errors are silently dropped.** Not compatible with `i2c_master_probe`.

This driver uses **synchronous mode** (`TQUEUE_DEPTH = 0`). For slow peripherals like the HD44780, async mode is unnecessary and has known issues — most notably, a full-bus scan reports false positives at every address when in async mode because NACK errors aren't surfaced back to the caller.

### Init flow

```
lcd_init(luh)
  ├─ guard: already initialized?    → ESP_ERR_INVALID_STATE
  ├─ apply user or default I²C config
  ├─ i2c_new_master_bus()           → bus_handle
  ├─ i2c_master_bus_add_device()    → dev_handle
  └─ hd44780_init(lcd, ctx=&lcd_dev)
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

- **Single LCD instance only.** The driver maintains one device internally and exposes a flat API. Supporting multiple LCDs would require reworking the API to take a handle.
- **Backlight is on by default** — set in the static initializer of the internal device. Use `hd44780_switch_backlight()` directly to toggle it at runtime.
- **No support for >2 row LCDs in the wrapper** — `lcd_row_pin`, `lcd_clear_row`, and similar APIs are hardcoded for 2-row LCDs. The underlying HD44780 driver supports up to 4 rows; extending the wrapper is straightforward.
- **`lcd_printf` clears before writing** — every call wipes the relevant rows. There is no append mode. This is intentional (matches the "live status display" use case) but may not suit all applications.
- **PCF8574 address and clock frequency are compile-time constants.** `lcd_user_handle` only covers ESP-side I²C configuration. If you need to change the PCF8574 address at runtime, edit `PCF8574_I2C_ADDR_DEFAULT` and rebuild.

---

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| `lcd_init` returns `LCD_ERR_NEW_MASTER_BUS` | I²C port already in use, or GPIO conflict |
| `lcd_init` returns `LCD_ERR_MASTER_ADD_DEVICE` | Wrong I²C address, or PCF8574 not on the bus |
| `lcd_init` returns `ESP_ERR_INVALID_STATE` | `lcd_init()` was called twice without an intervening `lcd_deinit()` |
| `lcd_init` succeeds but transmits time out during HD44780 init | Bus stuck low from a previous botched session — **fully power-cycle the LCD** (disconnect VCC for several seconds). Also check power-supply quality and USB cable. |
| I²C scan reports devices at every address | `TQUEUE_DEPTH` was changed to non-zero (async mode, errors silently dropped) — keep it at 0 |
| Display is on but blank | **Contrast pot needs adjustment** (most common cause). Adjust the trim pot on the back of the LCD module. |
| Display shows solid black squares on row 0 only | Contrast pot is at the other extreme, OR LCD is in 8-bit mode (init sequence failed midway) |
| Garbled characters | I²C signal integrity (try lowering `PCF8574_I2C_CLK_FREQ_DEFAULT` to 50 kHz, shorten wires, add external 4.7 kΩ pull-ups) |
| Display works briefly then garbles | Power supply can't handle current spikes — add a 470 µF capacitor across the LCD VCC/GND |
| Pin mapping appears wrong (backlight responds but text doesn't) | Try the alternate PCF8574 → LCD pin mapping (`PIN_RS_BIT=4`, `PIN_BL_BIT=7`, data on bits 0-3) |

---

## License

The HD44780 driver files (`hd44780.c`, `hd44780.h`) are © Ruslan V. Uss and licensed BSD-3-Clause. See the original source at https://github.com/UncleRus/esp-idf-lib.

The wrapper layer (`lcd.c`, `lcd.h`) is provided under the same BSD-3-Clause terms.
