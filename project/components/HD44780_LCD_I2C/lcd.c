#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "driver/i2c_master.h"
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
#define ESP_I2C_PORT    I2C_NUM_0
#define ESP_SDA_GPIO    GPIO_NUM_21
#define ESP_SCL_GPIO    GPIO_NUM_22
#define I2C_CLK_SRC     I2C_CLK_SRC_DEFAULT
#define GLITCH_IGN_CNT  7
#define INTR_PRIOR      0
#define TQUEUE_DEPTH    0               // do not touch, 0 is for putting i2c in sync mode, which is okay for simple LCDs
#define EN_INTR_PULLUP  true


// I2C device config params
#define HD44780_I2C_ADDR        0x27
#define HD44780_I2C_CLK_FREQ    100000
#define HD44780_ADDR_LEN        I2C_ADDR_BIT_LEN_7


// HD44780 specific params
#define LCD_COL_CNT     16
#define LCD_ROW_CNT     2
#define ROW(x)          (x)         // x in [0,1]
#define COL(x)          (x)         // x in [0, 15]



// PCF8574 specific params
#define PIN_RS_BIT      0 //4
#define PIN_RW_BIT      1
#define PIN_E_BIT       2
#define PIN_BL_BIT      3 //7 
#define PIN_D4_BIT      4
#define PIN_D5_BIT      5
#define PIN_D6_BIT      6
#define PIN_D7_BIT      7
 




struct lcd_i2c lcd_dev = {
    .lcd = { .write_cb = write_cb, 
             .pins = { .rs = PIN_RS_BIT , .e = PIN_E_BIT , .d4 = PIN_D4_BIT , .d5 = PIN_D5_BIT , .d6 = PIN_D6_BIT , .d7 = PIN_D7_BIT , .bl = PIN_BL_BIT  },
             .font = HD44780_FONT_5X8,
             .lines = LCD_ROW_CNT,
             .backlight = true    // keeping initial state "ON"        
    },
    .lcd_cols = LCD_COL_CNT,
    .dev_i2c_addr = HD44780_I2C_ADDR,
    .dev_i2c_sda_pin = ESP_SDA_GPIO,
    .dev_i2c_scl_pin = ESP_SCL_GPIO,
    .dev_i2c_freq = HD44780_I2C_CLK_FREQ,
    .esp_i2c_port = ESP_I2C_PORT
};



//=============================================== internals (exposed for hd44780)==========================================
esp_err_t write_cb(const hd44780_t *lcd, uint8_t data){
    struct lcd_i2c* ctx = (struct lcd_i2c*)(lcd->user_ctx);
    return i2c_master_transmit(ctx->dev_handle, &data, 1, 1000);
}

//=============================================== API ================================================
esp_err_t lcd_init(struct lcd_i2c* lcd_i2c_dev){
    PRINT_MSG("[lcd_init() started]\n");
    esp_err_t retval = ESP_OK;
    if(!lcd_i2c_dev){
        return ESP_ERR_INVALID_ARG;
    }

    // creating i2c bus
    PRINT_MSG("[lcd_init()] creating I2C bus...\n");
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = ESP_I2C_PORT,
        .sda_io_num = ESP_SDA_GPIO,
        .scl_io_num = ESP_SCL_GPIO,
        .clk_source = I2C_CLK_SRC,
        .glitch_ignore_cnt = GLITCH_IGN_CNT,
        .intr_priority = INTR_PRIOR,
        .trans_queue_depth = TQUEUE_DEPTH,
        .flags.enable_internal_pullup = EN_INTR_PULLUP,
    }; 

    retval = i2c_new_master_bus(&bus_cfg, &(lcd_i2c_dev->bus_handle));
    if(retval != ESP_OK){
        PRINT_MSG("[lcd_init()] retval : %d\n", retval);
        return LCD_ERR_NEW_MASTER_BUS;     
    }


    // adding i2c device on bus
    PRINT_MSG("[lcd_init()] adding I2C device on bus...\n");
    i2c_device_config_t dev_cfg = {
        .device_address = HD44780_I2C_ADDR ,
        .scl_speed_hz = HD44780_I2C_CLK_FREQ,
        .dev_addr_length = HD44780_ADDR_LEN,
    };

    retval = i2c_master_bus_add_device((lcd_i2c_dev->bus_handle), &dev_cfg, &(lcd_i2c_dev->dev_handle));
    if(retval != ESP_OK){
        PRINT_MSG("[lcd_init()] retval : %d\n", retval);
        return LCD_ERR_MASTER_ADD_DEVICE;
    }

    // init LCD
    PRINT_MSG("[lcd_init()] initializing HD47880...\n");
    retval = hd44780_init(&(lcd_i2c_dev->lcd), (void*)lcd_i2c_dev);
    if(retval != ESP_OK){
        PRINT_MSG("retval : %d\n", retval);
        return LCD_ERR_INIT_FAILED;
    }
    PRINT_MSG("[lcd_init() exited]\n");
    return LCD_OK;
}

// forcefully clears the screen, resets the pinned states
esp_err_t lcd_clear(struct lcd_i2c* lcd_i2c_dev){
    PRINT_MSG("[lcd_clear() entered]\n");
    esp_err_t retval = ESP_OK;      
    lcd_i2c_dev->lcd_cursor_pos_row = 0;
    lcd_i2c_dev->lcd_cursor_pos_col = 0;
    lcd_i2c_dev->lcd_row_pinning = 0;
    retval = hd44780_clear(&(lcd_i2c_dev->lcd));
    if(retval != ESP_OK){
        PRINT_MSG("[lcd_clear()] retval : %d\n", retval);
        return LCD_ERR_CLEAR_FAILED;
    }
    PRINT_MSG("[lcd_clear() exited]\n");
    return LCD_OK;
}

// clears non pinned rows
esp_err_t lcd_clear_row(struct lcd_i2c* lcd_i2c_dev){
    PRINT_MSG("[lcd_clear_row() entered]\n");
    esp_err_t retval = ESP_OK;
    switch(lcd_i2c_dev->lcd_row_pinning){
        case 0:         // no row pinned, clearing both
            retval = lcd_clear(lcd_i2c_dev); // automatically brings cursor pos to (0,0)
            if(retval != ESP_OK){
                PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);
                return LCD_ERR_CLEAR_FAILED;
            }           
            break;
        case 1:         // row0 pinned, so clearing row1 only 
             for(uint8_t i = 0; i < LCD_COL_CNT; i++){
                retval = hd44780_putc(&(lcd_i2c_dev->lcd), ' ');          
                if(retval != ESP_OK){
                    PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);
                    return LCD_ERR_PUTC_FAILED;
                }
            }
            retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), COL(0), ROW(1)); 
            if(retval != ESP_OK){
                PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);
                return LCD_ERR_GOTOXY_FAILED;    
            } 
            break;
        case 2:         // row1 pinned, so clearing row0 only
            for(uint8_t i = 0; i < LCD_COL_CNT; i++){
                retval = hd44780_putc(&(lcd_i2c_dev->lcd), ' ');          
                if(retval != ESP_OK){
                    PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);
                    return LCD_ERR_PUTC_FAILED;
                }
            }
            retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), COL(0), ROW(0));
            if(retval != ESP_OK){
                PRINT_MSG("[lcd_clear_row()] retval : %d\n", retval);
                return LCD_ERR_GOTOXY_FAILED;    
            } 
            break;
        case 3:         // both rows pinned, nothing to clear
        default:
            return LCD_WARN_NOTHING_TO_CLEAR;   
    }
    PRINT_MSG("[lcd_clear_row() exited]\n");     
    return LCD_OK;
}

int lcd_printf(struct lcd_i2c* lcd_i2c_dev, const char *fmt, ...){
    PRINT_MSG("[lcd_printf() entered]\n");
    esp_err_t retval = ESP_OK;
    if(lcd_i2c_dev->lcd_row_pinning == ((1<<ROW(1))|(1<<ROW(0)))){
        return -1;
    }
    PRINT_MSG("Curr msg to print : %s\n", fmt);
    uint8_t old_pos_row = lcd_i2c_dev->lcd_cursor_pos_row;
    uint8_t old_pos_col = lcd_i2c_dev->lcd_cursor_pos_col;

    PRINT_MSG("old_row : %d\nold_col : %d\n", lcd_i2c_dev->lcd_cursor_pos_row, lcd_i2c_dev->lcd_cursor_pos_col);

    if(lcd_i2c_dev->lcd_row_pinning){
        // some row is pinned
        PRINT_MSG("Some row is pinned, : %d\n", lcd_i2c_dev->lcd_row_pinning);
        retval = lcd_clear_row(lcd_i2c_dev); 
        if(retval != ESP_OK){
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_CLEAR_FAILED;
        }
    }else{
        // nothing pinned, clearing the LCD
        retval = lcd_clear(lcd_i2c_dev);
        if(retval != ESP_OK){
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_CLEAR_FAILED;
        }
        
        retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), old_pos_col, old_pos_row); 
        if(retval != ESP_OK){      // just intermediate
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_GOTOXY_FAILED;    
        }    
    }

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
    for(uint8_t i = 0;i < written; i++ ){
        retval = hd44780_putc(&(lcd_i2c_dev->lcd), buff[i]); 
        if(retval != ESP_OK){
            PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
            return LCD_ERR_PUTC_FAILED;
        }
        if(i == LCD_COL_CNT){
            retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), COL(0), ROW(1)); 
            if(retval != ESP_OK){
                PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
                return LCD_ERR_GOTOXY_FAILED;
            }
        }
    }

    // resetting the cursor position
    lcd_i2c_dev->lcd_cursor_pos_row = old_pos_row;
    lcd_i2c_dev->lcd_cursor_pos_col = old_pos_col;
    retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), old_pos_col, old_pos_row); 
    if(retval != ESP_OK){
        PRINT_MSG("[lcd_printf()] retval : %d\n", retval);
        return LCD_ERR_GOTOXY_FAILED;    
    }    
    PRINT_MSG("[lcd_printf() exited]\n");
    return written;
}

esp_err_t lcd_row_pin(struct lcd_i2c* lcd_i2c_dev, uint8_t row){
    PRINT_MSG("[lcd_row_pin() entered]\n");
    esp_err_t retval = ESP_OK;
    uint8_t _row = 0;
    uint8_t _col = 0;
    if(row == ROW(0) || row == ROW(1)){
        PRINT_MSG("row to pin : %d\n", row);
        (lcd_i2c_dev->lcd_row_pinning)|=(1<<row);
        PRINT_MSG("row %d pinned\n", row);
        PRINT_MSG("lcd_row_pinning val : %d\n", lcd_i2c_dev->lcd_row_pinning);
        lcd_i2c_dev->lcd_cursor_pos_col = COL(0);
        _col = COL(0);
        if(lcd_i2c_dev->lcd_row_pinning == ((1<<ROW(1))|(1<<ROW(0)))){
            // both got pinned, setting C(1,15)
            lcd_i2c_dev->lcd_cursor_pos_col = COL(15);
            lcd_i2c_dev->lcd_cursor_pos_row = ROW(1);
            _col = COL(15);            
            _row = ROW(1);
                        
        }else 
        if(row == ROW(0)){
            lcd_i2c_dev->lcd_cursor_pos_row = ROW(1);
            _row = ROW(1);    
        }else{
            lcd_i2c_dev->lcd_cursor_pos_row = ROW(0);
            _row = ROW(0);
        }
        retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), _col, _row);
        if(retval != ESP_OK){
            PRINT_MSG("[lcd_row_pin()] retval : %d\n", retval);
            return LCD_ERR_GOTOXY_FAILED;
        }
        PRINT_MSG("curr pos (r,c) : (%d, %d)\n", lcd_i2c_dev->lcd_cursor_pos_row, lcd_i2c_dev->lcd_cursor_pos_col);
    }else{ 
        return LCD_ERR_INVALID_ARG; 
    }
    PRINT_MSG("[lcd_row_pin() exited]\n");
    return LCD_OK;
}

esp_err_t lcd_row_unpin(struct lcd_i2c* lcd_i2c_dev, uint8_t row){
    PRINT_MSG("[lcd_row_unpin() entered]\n");    
    esp_err_t retval = ESP_OK;
    if(row == ROW(0) || row == ROW(1)){
        (lcd_i2c_dev->lcd_row_pinning)&=(~(1<<row));
        lcd_i2c_dev->lcd_cursor_pos_row = ROW(0);   // ROW(0) for both row0 and row 1 unpinning      
        lcd_i2c_dev->lcd_cursor_pos_col = COL(0);
        retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), COL(0), ROW(0)); 
        if(retval != ESP_OK){
            PRINT_MSG("[lcd_row_unpin()] retval : %d\n", retval);
            return LCD_ERR_GOTOXY_FAILED;
        }
    }else{
        return LCD_ERR_INVALID_ARG;
    }
    PRINT_MSG("[lcd_row_unpin() exited]\n");
    return LCD_OK;
}

esp_err_t lcd_set_cursor(struct lcd_i2c* lcd_i2c_dev, uint8_t row, uint8_t col){
    PRINT_MSG("[lcd_set_cursor() entered]\n");    
    esp_err_t retval = ESP_OK;
    lcd_i2c_dev->lcd_cursor_pos_row = row;
    lcd_i2c_dev->lcd_cursor_pos_col = col;
    retval = hd44780_gotoxy(&(lcd_i2c_dev->lcd), col, row); 
    if(retval != ESP_OK){
        PRINT_MSG("[lcd_set_cursor()] retval : %d\n", retval);
        return LCD_ERR_GOTOXY_FAILED;
    }
    PRINT_MSG("[lcd_set_cursor() exited]\n");
    return LCD_OK;
}
