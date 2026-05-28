#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lcd.h"
#include "logging.h"


/*      Pin mapping between HD44780 and PCF8574
 *      HD44780 exposes a register of 8 bits, whose bits runs as P7-P6-...-P0
 *      P0 : RS 
 *      P1 : RW
 *      P2 : E
 *      P3 : BL     
 *      P4 : D4
 *      P5 : D5
 *      P6 : D6
 *      P7 : D7
**/

    


// I2C bus config params
#define ESP_I2C_PORT_DEFAULT        I2C_NUM_0
#define ESP_SDA_GPIO_DEFAULT        GPIO_NUM_21
#define ESP_SCL_GPIO_DEFAULT        GPIO_NUM_22
#define ESP_I2C_CLK_SRC_DEFAULT     I2C_CLK_SRC_DEFAULT
        //  ----- DO NOT CHANGE -----
#define GLITCH_IGN_CNT              7
#define INTR_PRIOR                  0
#define TQUEUE_DEPTH                0    // 0 is for putting i2c in sync mode, which is okay for simple LCDs
#define EN_INTR_PULLUP              true


// PCF8574 I2C device config params and pin mapping
#define PCF8574_I2C_ADDR_DEFAULT        0x27                    // ---- [MODIFY IF DIFFERS] ----
#define PCF8574_I2C_CLK_FREQ_DEFAULT    100000                  // ---- [MODIFY IF DIFFERS] ---- 
#define PCF8574_ADDR_LEN_DEFAULT        I2C_ADDR_BIT_LEN_7      // ---- [MODIFY IF DIFFERS] ----

#define PIN_RS_BIT      0 
#define PIN_RW_BIT      1
#define PIN_E_BIT       2
#define PIN_BL_BIT      3 
#define PIN_D4_BIT      4
#define PIN_D5_BIT      5
#define PIN_D6_BIT      6
#define PIN_D7_BIT      7

// HD44780 specific params
#define LCD_COL_CNT     16
#define LCD_ROW_CNT     2
#define ROW(x)          (x)         // x in [0,1]
#define COL(x)          (x)         // x in [0, 15]




/**
 * @brief LCD device descriptor for HD44780 + PCF8574 + ESP configuration.
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
     * @brief I2C device address of PCF8574.
     */
    uint8_t pcf8574_i2c_addr;

    /**
     * @brief Address length of the PCF backpack
     */
    uint8_t pcf8574_i2c_addr_len;

    /**
     * @brief SDA GPIO pin number.
     */
    uint8_t esp_i2c_sda_pin;

    /**
     * @brief SCL GPIO pin number.
     */
    uint8_t esp_i2c_scl_pin;

    /**
     * @brief I2C clock frequency in Hz.
     */
    uint32_t esp_i2c_freq;

    /**
     * ESP-IDF I2C clock source like ABP etc
     */
    uint8_t esp_i2c_clk_src;    
    
    /**
     * @brief ESP-IDF I2C controller port.
     */
    uint8_t esp_i2c_port;

    /**
     * @brief Number of LCD columns.
     */
    const uint8_t lcd_cols;

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
     * @brief Initialization tracking of each LCD device
     */
    bool lcd_init_state;

    /**
     * @brief Semaphore mutex for LCD state protection on multicores
     */
    SemaphoreHandle_t lcd_mutex; 

    /**
     * @brief ESP-IDF I2C master bus handle.
     */
    i2c_master_bus_handle_t bus_handle;

    /**
     * @brief ESP-IDF I2C device handle.
     */
    i2c_master_dev_handle_t dev_handle;  
};

struct lcd_i2c lcd_dev = {
    .lcd = { .write_cb = write_cb, 
             .pins = { .rs = PIN_RS_BIT , .e = PIN_E_BIT , .d4 = PIN_D4_BIT , .d5 = PIN_D5_BIT , .d6 = PIN_D6_BIT , .d7 = PIN_D7_BIT , .bl = PIN_BL_BIT  },
             .font = HD44780_FONT_5X8,
             .lines = LCD_ROW_CNT,
             .backlight = true    // keeping initial state "ON"        
    },
    .pcf8574_i2c_addr = PCF8574_I2C_ADDR_DEFAULT,
    .pcf8574_i2c_addr_len = PCF8574_ADDR_LEN_DEFAULT,
    .esp_i2c_sda_pin = 0,
    .esp_i2c_scl_pin = 0,    
    .esp_i2c_freq = PCF8574_I2C_CLK_FREQ_DEFAULT,
    .esp_i2c_clk_src = 0,
    .esp_i2c_port = 0,
    .lcd_cols = LCD_COL_CNT,
    .lcd_row_pinning = 0,
    .lcd_cursor_pos_row = 0,
    .lcd_cursor_pos_col = 0,
    .lcd_init_state = false,
    .lcd_mutex = NULL,
    .bus_handle = NULL,
    .dev_handle = NULL
};


#define LCD_DEV_DEFAULT_ESP_CONFIG_INIT(dev)    do{ dev.esp_i2c_sda_pin = ESP_SDA_GPIO_DEFAULT; \
                                                    dev.esp_i2c_scl_pin = ESP_SCL_GPIO_DEFAULT; \
                                                    dev.esp_i2c_clk_src = ESP_I2C_CLK_SRC_DEFAULT; \
                                                    dev.esp_i2c_port = ESP_I2C_PORT_DEFAULT; \
                                                  }while(0);

//=============================================== internals (exposed for hd44780)==========================================
esp_err_t write_cb(const hd44780_t *lcd, uint8_t data){
    struct lcd_i2c* ctx = (struct lcd_i2c*)(lcd->user_ctx);
    return i2c_master_transmit(ctx->dev_handle, &data, 1, 1000);
}


//=============================================== internals (for lcd.c file only) ==========================================
static inline void lcd_ops_lock(void){
    xSemaphoreTakeRecursive(lcd_dev.lcd_mutex, portMAX_DELAY);
}

static inline void lcd_ops_unlock(void){
    xSemaphoreGiveRecursive(lcd_dev.lcd_mutex);
}
//=============================================== API ================================================

esp_err_t lcd_init(struct lcd_user_handle* luh){  // must be called in app_main();
    PRINT_MSG("[lcd_init() started]\n");
    
    if(!(lcd_dev.lcd_mutex = xSemaphoreCreateRecursiveMutex())){
        return ESP_ERR_NO_MEM;
    }
    int retval = ESP_OK;
    if(lcd_dev.lcd_init_state){
        vSemaphoreDelete(lcd_dev.lcd_mutex);
        PRINT_MSG("invalid state\n");
        return LCD_ERR_INVALID_STATE;
    }    
    
    if(!luh){
        // user want default ESP config
        LCD_DEV_DEFAULT_ESP_CONFIG_INIT(lcd_dev);
    }else{
        // setting user specific ESP config
        lcd_dev.esp_i2c_sda_pin = luh->esp_i2c_sda_pin_usr;
        lcd_dev.esp_i2c_scl_pin = luh->esp_i2c_scl_pin_usr;
        lcd_dev.esp_i2c_clk_src = luh->esp_i2c_clk_src_usr;
        lcd_dev.esp_i2c_port = luh->esp_i2c_port_usr;
    }
        
    // creating i2c bus
    PRINT_MSG("[lcd_init()] creating I2C bus...\n");
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = lcd_dev.esp_i2c_port,
        .sda_io_num = lcd_dev.esp_i2c_sda_pin,
        .scl_io_num = lcd_dev.esp_i2c_scl_pin,
        .clk_source = lcd_dev.esp_i2c_clk_src,
        .glitch_ignore_cnt = GLITCH_IGN_CNT,
        .intr_priority = INTR_PRIOR,
        .trans_queue_depth = TQUEUE_DEPTH,
        .flags.enable_internal_pullup = EN_INTR_PULLUP,
    }; 

    retval = i2c_new_master_bus(&bus_cfg, &(lcd_dev.bus_handle));
    if(retval != ESP_OK){
        vSemaphoreDelete(lcd_dev.lcd_mutex);
        PRINT_MSG("[lcd_init()] retval : %d\n", retval);        
        return LCD_ERR_NEW_MASTER_BUS;     
    }

    // adding i2c device on bus
    PRINT_MSG("[lcd_init()] adding I2C device on bus...\n");
    i2c_device_config_t dev_cfg = {
        .device_address = PCF8574_I2C_ADDR_DEFAULT ,
        .scl_speed_hz = PCF8574_I2C_CLK_FREQ_DEFAULT,
        .dev_addr_length = PCF8574_ADDR_LEN_DEFAULT,
    };

    retval = i2c_master_bus_add_device((lcd_dev.bus_handle), &dev_cfg, &(lcd_dev.dev_handle));
    if(retval != ESP_OK){
        i2c_del_master_bus(lcd_dev.bus_handle);
        lcd_dev.bus_handle = NULL;
        vSemaphoreDelete(lcd_dev.lcd_mutex);
        PRINT_MSG("[lcd_init()] retval : %d\n", retval);
        return LCD_ERR_MASTER_ADD_DEVICE;
    }

    // init LCD
    PRINT_MSG("[lcd_init()] initializing HD47880...\n");
    retval = hd44780_init(&(lcd_dev.lcd), (void*)&lcd_dev);
    if(retval != ESP_OK){
        i2c_master_bus_rm_device(lcd_dev.dev_handle);
        lcd_dev.dev_handle = NULL;
        i2c_del_master_bus(lcd_dev.bus_handle);
        lcd_dev.bus_handle = NULL;
        vSemaphoreDelete(lcd_dev.lcd_mutex);       
        PRINT_MSG("retval : %d\n", retval);        
        return LCD_ERR_INIT_FAILED;
    }
    lcd_dev.lcd_init_state = true;
    PRINT_MSG("[lcd_init() exited]\n");
    return LCD_OK;
}

esp_err_t lcd_clear(void){
    PRINT_MSG("[lcd_clear() entered]\n");
    esp_err_t retval = ESP_OK;      
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    lcd_dev.lcd_cursor_pos_row = 0;
    lcd_dev.lcd_cursor_pos_col = 0;
    lcd_dev.lcd_row_pinning = 0;
    retval = hd44780_clear(&(lcd_dev.lcd));
    if(retval != ESP_OK){
        lcd_ops_unlock();
        PRINT_MSG("[lcd_clear()] retval : %d\n", retval);
        return LCD_ERR_CLEAR_FAILED;
    }
    lcd_ops_unlock();
    PRINT_MSG("[lcd_clear() exited]\n");
    return LCD_OK;
}

// clears non pinned rows
esp_err_t lcd_clear_row(void){
    PRINT_MSG("[lcd_clear_row() entered]\n");
    esp_err_t retval = ESP_OK;
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    switch(lcd_dev.lcd_row_pinning){
        case 0:         // no row pinned, clearing both
            retval = lcd_clear(); // automatically brings cursor pos to (0,0)
            if(retval != ESP_OK){
                lcd_ops_unlock();
                PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);
                return LCD_ERR_CLEAR_FAILED;
            }           
            break;
        case 1:         // row0 pinned, so clearing row1 only 
             for(uint8_t i = 0; i < LCD_COL_CNT; i++){
                retval = hd44780_putc(&(lcd_dev.lcd), ' ');          
                if(retval != ESP_OK){
                    lcd_ops_unlock();
                    PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);                  
                    return LCD_ERR_PUTC_FAILED;
                }
            }
            retval = hd44780_gotoxy(&(lcd_dev.lcd), COL(0), ROW(1)); 
            if(retval != ESP_OK){
                lcd_ops_unlock();
                PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);                
                return LCD_ERR_GOTOXY_FAILED;    
            } 
            break;
        case 2:         // row1 pinned, so clearing row0 only
            for(uint8_t i = 0; i < LCD_COL_CNT; i++){
                retval = hd44780_putc(&(lcd_dev.lcd), ' ');          
                if(retval != ESP_OK){
                    lcd_ops_unlock();
                    PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);                    
                    return LCD_ERR_PUTC_FAILED;
                }
            }
            retval = hd44780_gotoxy(&(lcd_dev.lcd), COL(0), ROW(0));
            if(retval != ESP_OK){
                lcd_ops_unlock();
                PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);               
                return LCD_ERR_GOTOXY_FAILED;    
            } 
            break;
        case 3:         // both rows pinned, nothing to clear
        default:
            lcd_ops_unlock();
            return LCD_WARN_NOTHING_TO_CLEAR;   
    }
    lcd_ops_unlock();  
    PRINT_MSG("[lcd_clear_row() exited]\n");  
    return LCD_OK;
}

int lcd_printf(const char *fmt, ...){
    PRINT_MSG("[lcd_printf() entered]\n");
    if(!fmt){
        return LCD_ERR_INVALID_ARG;
    }
    esp_err_t retval = ESP_OK;
    char buff[LCD_ROW_CNT*LCD_COL_CNT + 1] = {0};       // +1 for null terminator

    /* 
       pin and unpin functions must set cursor position after pinning; 
       if r0 pinned, C(r, c) --> C(1, 0) ; 
       if r1 pinned, C(r, c) --> C(0, 0) ; 
       if both pinned, C(r, c) --> C(1, 15) ; 
       Also, lcd_printf() must check for available space and write in unpinned regions or rows only, 
       and write only much chars which can fill all the available cells, 
       each lcd_printf() must over ride the previous lcd_printf() calls data, i.e. while exiting, 
       lcd_printf() must reset the cursor position according to current pinned row states
    */
    va_list args;
    va_start(args, fmt);
    vsnprintf(buff, sizeof(buff), fmt, args);
    va_end(args);
    int written = strlen(buff);
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    if(lcd_dev.lcd_row_pinning == ((1<<ROW(1))|(1<<ROW(0)))){
        lcd_ops_unlock();
        return -1;
    }
    PRINT_MSG("Curr msg to print : %s\n", fmt);
    uint8_t old_pos_row = lcd_dev.lcd_cursor_pos_row;
    uint8_t old_pos_col = lcd_dev.lcd_cursor_pos_col;

    PRINT_MSG("old_row : %d\nold_col : %d\n", lcd_dev.lcd_cursor_pos_row, lcd_dev.lcd_cursor_pos_col);

    if(lcd_dev.lcd_row_pinning){
        // some row is pinned
        PRINT_MSG("Some row is pinned, : %d\n", lcd_dev.lcd_row_pinning);
        retval = lcd_clear_row(); 
        if(retval != ESP_OK){
            lcd_ops_unlock();
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_CLEAR_FAILED;
        }
    }else{
        // nothing pinned, clearing the LCD
        retval = lcd_clear();
        if(retval != ESP_OK){
            lcd_ops_unlock();
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_CLEAR_FAILED;
        }
        
        retval = hd44780_gotoxy(&(lcd_dev.lcd), old_pos_col, old_pos_row); 
        if(retval != ESP_OK){      // just intermediate
            lcd_ops_unlock();
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_GOTOXY_FAILED;    
        }    
    }
  
    for(uint8_t i = 0;i < written; i++ ){
        retval = hd44780_putc(&(lcd_dev.lcd), buff[i]); 
        if(retval != ESP_OK){
            lcd_ops_unlock();
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_PUTC_FAILED;
        }
        if(i == LCD_COL_CNT-1){
            retval = hd44780_gotoxy(&(lcd_dev.lcd), COL(0), ROW(1)); 
            if(retval != ESP_OK){
                lcd_ops_unlock();
                PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
                return LCD_ERR_GOTOXY_FAILED;
            }
        }
    }

    // resetting the cursor position
    lcd_dev.lcd_cursor_pos_row = old_pos_row;
    lcd_dev.lcd_cursor_pos_col = old_pos_col;
    retval = hd44780_gotoxy(&(lcd_dev.lcd), old_pos_col, old_pos_row); 
    if(retval != ESP_OK){
        PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
        lcd_ops_unlock();
        return LCD_ERR_GOTOXY_FAILED;    
    }    
    lcd_ops_unlock();
    PRINT_MSG("[lcd_printf() exited]\n");
    return written;
}

esp_err_t lcd_row_pin(uint8_t row){
    PRINT_MSG("[lcd_row_pin() entered]\n");
    esp_err_t retval = ESP_OK;
    uint8_t _row = 0;
    uint8_t _col = 0;
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    if(row == ROW(0) || row == ROW(1)){
        PRINT_MSG("row to pin : %d\n", row);
        (lcd_dev.lcd_row_pinning)|=(1<<row);
        PRINT_MSG("row %d pinned\n", row);
        PRINT_MSG("lcd_row_pinning val : %d\n", lcd_dev.lcd_row_pinning);
        lcd_dev.lcd_cursor_pos_col = COL(0);
        _col = COL(0);
        if(lcd_dev.lcd_row_pinning == ((1<<ROW(1))|(1<<ROW(0)))){
            // both got pinned, setting C(1,15)
            lcd_dev.lcd_cursor_pos_col = COL(15);
            lcd_dev.lcd_cursor_pos_row = ROW(1);
            _col = COL(15);            
            _row = ROW(1);
                        
        }else 
        if(row == ROW(0)){
            lcd_dev.lcd_cursor_pos_row = ROW(1);
            _row = ROW(1);    
        }else{
            lcd_dev.lcd_cursor_pos_row = ROW(0);
            _row = ROW(0);
        }
        retval = hd44780_gotoxy(&(lcd_dev.lcd), _col, _row);
        if(retval != ESP_OK){
            lcd_ops_unlock();
            PRINT_MSG("[lcd_row_pin()] retval : %d\n", retval);
            return LCD_ERR_GOTOXY_FAILED;
        }
        PRINT_MSG("curr pos (r,c) : (%d, %d)\n", lcd_dev.lcd_cursor_pos_row, lcd_dev.lcd_cursor_pos_col);
    }else{ 
        lcd_ops_unlock();
        return LCD_ERR_INVALID_ARG; 
    }
    lcd_ops_unlock();
    PRINT_MSG("[lcd_row_pin() exited]\n");
    return LCD_OK;
}

esp_err_t lcd_row_unpin(uint8_t row){
    PRINT_MSG("[lcd_row_unpin() entered]\n");    
    esp_err_t retval = ESP_OK;
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    if(row == ROW(0) || row == ROW(1)){
        (lcd_dev.lcd_row_pinning)&=(~(1<<row));
        lcd_dev.lcd_cursor_pos_row = ROW(0);   // ROW(0) for both row0 and row 1 unpinning      
        lcd_dev.lcd_cursor_pos_col = COL(0);
        retval = hd44780_gotoxy(&(lcd_dev.lcd), COL(0), ROW(0)); 
        if(retval != ESP_OK){
            lcd_ops_unlock();
            PRINT_MSG("[lcd_row_unpin()] retval : %d\n", retval);
            return LCD_ERR_GOTOXY_FAILED;
        }
    }else{
        lcd_ops_unlock();
        return LCD_ERR_INVALID_ARG;
    }
    lcd_ops_unlock();
    PRINT_MSG("[lcd_row_unpin() exited]\n");
    return LCD_OK;
}

esp_err_t lcd_set_cursor(uint8_t row, uint8_t col){
    PRINT_MSG("[lcd_set_cursor() entered]\n");    
    if (row >= LCD_ROW_CNT || col >= LCD_COL_CNT){
        return LCD_ERR_INVALID_ARG;
    }

    esp_err_t retval = ESP_OK;
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    lcd_dev.lcd_cursor_pos_row = row;
    lcd_dev.lcd_cursor_pos_col = col;
    retval = hd44780_gotoxy(&(lcd_dev.lcd), col, row); 
    if(retval != ESP_OK){
        lcd_ops_unlock();
        PRINT_MSG("[lcd_set_cursor()] retval : %d\n", retval);
        return LCD_ERR_GOTOXY_FAILED;
    }
    lcd_ops_unlock();
    PRINT_MSG("[lcd_set_cursor() exited]\n");
    return LCD_OK;
}

esp_err_t lcd_deinit(void){
    PRINT_MSG("[lcd_deinit() entered]\n");
    lcd_ops_lock();
    if(!(lcd_dev.lcd_init_state)){
        PRINT_MSG("[lcd_clear()] LCD not initialized\n");
        lcd_ops_unlock();
        return LCD_ERR_INVALID_STATE;
    }
    i2c_master_bus_rm_device(lcd_dev.dev_handle);
    i2c_del_master_bus(lcd_dev.bus_handle);
    lcd_dev.dev_handle = NULL;
    lcd_dev.bus_handle = NULL;
    lcd_dev.lcd_row_pinning = 0;
    lcd_dev.lcd_cursor_pos_row = 0;
    lcd_dev.lcd_cursor_pos_col = 0;
    lcd_dev.lcd_init_state = false;
    lcd_ops_unlock();    
    vSemaphoreDelete(lcd_dev.lcd_mutex);    
        
    lcd_dev.lcd_mutex = NULL;
    PRINT_MSG("[lcd_deinit() exited]\n");
    return LCD_OK;
}
