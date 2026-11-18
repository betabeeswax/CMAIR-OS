#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <ctime>

namespace {
constexpr char kTag[] = "MCAI-OS";
constexpr int kWidth = 240;
constexpr int kHeight = 135;

// Cardputer ADV (Stamp-S3A) official M5Stack pin map.
constexpr gpio_num_t kLcdBacklight = GPIO_NUM_38;
constexpr gpio_num_t kLcdReset = GPIO_NUM_33;
constexpr gpio_num_t kLcdDc = GPIO_NUM_34;
constexpr gpio_num_t kLcdMosi = GPIO_NUM_35;
constexpr gpio_num_t kLcdSclk = GPIO_NUM_36;
constexpr gpio_num_t kLcdCs = GPIO_NUM_37;
constexpr gpio_num_t kI2cSda = GPIO_NUM_8;
constexpr gpio_num_t kI2cScl = GPIO_NUM_9;
constexpr std::uint8_t kKeyboardAddress = 0x34;
constexpr char kSdMountPoint[] = "/sdcard";

esp_lcd_panel_handle_t panel = nullptr;
sdmmc_card_t* sd_card = nullptr;
bool keyboard_ready = false;
bool sd_ready = false;
int selected_app = 0;
int active_app = -1;
int setting_brightness = 1;
int calc_value = 0;
int file_scroll = 0;
bool boot_button_was_pressed = false;
constexpr int kAppsPerPage = 5;
constexpr const char* kApps[] = {"FILE MANAGER","NOTES","CALCULATOR","SETTINGS","SYSTEM INFO","APP STORE","CLOCK","TIMER","KEYBOARD TEST","STORAGE","DIAGNOSTICS","GAMES","ABOUT"};
constexpr int kAppCount = sizeof(kApps) / sizeof(kApps[0]);
alignas(4) std::uint16_t framebuffer[kWidth * kHeight];

constexpr std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void fill_rect(int x, int y, int w, int h, std::uint16_t color) {
    const int x0 = std::max(0, x), y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + w), y1 = std::min(kHeight, y + h);
    for (int row = y0; row < y1; ++row)
        std::fill(framebuffer + row * kWidth + x0, framebuffer + row * kWidth + x1, color);
}

const std::uint8_t* glyph(char c) {
    static const std::uint8_t blank[5] = {};
    static const std::uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
    };
    static const std::uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E}
    };
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    static const std::uint8_t dash[5] = {8,8,8,8,8};
    if (c == '-') return dash;
    return blank;
}

void draw_text(const char* text, int x, int y, int scale, std::uint16_t color) {
    for (const char* p = text; *p; ++p, x += 6 * scale) {
        const auto* bits = glyph(*p);
        for (int col = 0; col < 5; ++col)
            for (int row = 0; row < 7; ++row)
                if (bits[col] & (1U << row))
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
    }
}

void render_app() {
    const auto white=rgb565(244,248,255), muted=rgb565(150,166,190), accent=rgb565(87,231,190);
    std::fill(std::begin(framebuffer),std::end(framebuffer),rgb565(11,16,29));
    draw_text("MCAI-OS",8,7,1,accent); draw_text(kApps[active_app],100,7,1,white);
    fill_rect(0,19,kWidth,1,rgb565(44,59,78));
    if(active_app==0) {
        draw_text("SD CARD FILES",8,27,1,accent);
        if(!sd_ready) draw_text("INSERT SD CARD",8,50,1,white);
        else {
            DIR* dir=opendir(kSdMountPoint);
            if(!dir) draw_text("CANNOT OPEN SD ROOT",8,50,1,white);
            else { struct dirent* ent; int row=0, seen=0;
                while((ent=readdir(dir))!=nullptr) { if(ent->d_name[0]=='.') continue; if(seen++<file_scroll) continue; if(row>=5) break; draw_text(ent->d_name,8,40+row*14,1,white); ++row; }
                if(row==0) { draw_text("SD CARD IS EMPTY",8,50,1,white); } closedir(dir); }
        }
        draw_text("UP/DOWN SCROLL",8,111,1,muted);
    } else if(active_app==1) {
        draw_text("NOTES",8,29,1,accent);
        if(!sd_ready) draw_text("SD CARD NEEDED TO SAVE",8,54,1,white);
        else { draw_text("SAVES TO:",8,49,1,muted); draw_text("/sdcard/MCAI-OS/notes",8,64,1,white); draw_text("ENTER CREATES A NOTE",8,88,1,accent); }
    } else if(active_app==2) {
        draw_text("SIMPLE CALCULATOR",8,30,1,accent);
        char n[24]; std::snprintf(n,sizeof(n),"%d",calc_value); draw_text(n,8,55,3,white);
        draw_text("UP +1   DOWN -1",8,91,1,muted); draw_text("ENTER RESET",8,105,1,muted);
    } else if(active_app==3) {
        draw_text("DEVICE SETTINGS",8,28,1,accent); draw_text("DISPLAY BRIGHTNESS",8,48,1,white);
        draw_text(setting_brightness==0?"LOW":setting_brightness==1?"MEDIUM":"HIGH",8,68,2,accent);
        draw_text("UP/DOWN TO CHANGE",8,98,1,muted);
    } else if(active_app==4) {
        draw_text("BOARD: CARDPUTER ADV",8,30,1,white); draw_text("CPU: ESP32-S3",8,46,1,white);
        draw_text("DISPLAY: 240 X 135",8,62,1,white); draw_text(keyboard_ready?"KEYBOARD: READY":"KEYBOARD: MISSING",8,78,1,white);
        draw_text(sd_ready?"MICROSD: MOUNTED":"MICROSD: NOT MOUNTED",8,94,1,white);
    } else if(active_app==5) {
        draw_text("APP STORE",8,29,1,accent); draw_text("APP CATALOGUE: 106 IDEAS",8,49,1,white);
        draw_text("INSTALLER NEEDS PACKAGE",8,67,1,white); draw_text("FORMAT + SIGNATURE SUPPORT",8,83,1,muted);
        draw_text("NOT CLAIMING FAKE INSTALLS",8,101,1,accent);
    } else if(active_app==6) {
        const std::time_t now=std::time(nullptr); std::tm tmv{}; localtime_r(&now,&tmv); char t[16];
        std::snprintf(t,sizeof(t),"%02d:%02d:%02d",tmv.tm_hour,tmv.tm_min,tmv.tm_sec);
        draw_text("DEVICE CLOCK",8,32,1,accent); draw_text(t,8,56,3,white);
        if(tmv.tm_year<120) draw_text("TIME NOT SET",8,101,1,muted);
    } else if(active_app==7) {
        draw_text("TIMER",8,32,2,accent); draw_text("TIMER SERVICE NOT READY",8,75,1,muted);
    } else if(active_app==8) {
        draw_text("PRESS KEYS TO TEST",8,35,1,accent); draw_text("ARROWS + ENTER",8,61,1,white);
    } else if(active_app==9) {
        draw_text(sd_ready?"MICROSD: READY":"MICROSD: NOT FOUND",8,40,1,white);
        draw_text("NO FORMAT OR ERASE ACTION",8,64,1,muted);
    } else if(active_app==10) {
        draw_text(keyboard_ready?"KEYPAD: PASS":"KEYPAD: FAIL",8,40,1,white);
        draw_text(sd_ready?"SD MOUNT: PASS":"SD MOUNT: FAIL",8,58,1,white);
        draw_text("DISPLAY: INITIALIZED",8,76,1,white);
    } else if(active_app==11) {
        draw_text("GAMES",8,35,2,accent); draw_text("NO GAMES INSTALLED YET",8,72,1,muted);
    } else { draw_text("MCAI-OS CARDPUTER ADV",8,38,1,white); draw_text("VERSION 0.1.0 ALPHA",8,58,1,accent); }
    fill_rect(0,122,kWidth,1,rgb565(44,59,78)); draw_text("BACKSPACE: HOME",8,126,1,muted);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,kWidth,kHeight,framebuffer));
}

void render_home() {
    const auto bg=rgb565(11,16,29), card=rgb565(22,31,49), accent=rgb565(87,231,190);
    const auto white=rgb565(244,248,255), muted=rgb565(150,166,190), selected=rgb565(37,68,75);
    if(active_app>=0) { render_app(); return; }
    std::fill(std::begin(framebuffer),std::end(framebuffer),bg);
    fill_rect(0,0,kWidth,3,accent); draw_text("MCAI-OS",8,7,2,accent);
    draw_text(sd_ready?"SD READY":"NO SD",158,12,1,sd_ready?accent:muted);
    draw_text("APP LAUNCHER",8,29,1,muted);
    const int page=selected_app/kAppsPerPage, pages=(kAppCount+kAppsPerPage-1)/kAppsPerPage;
    for(int row=0;row<kAppsPerPage;row++) { const int i=page*kAppsPerPage+row; if(i>=kAppCount) break;
        const int y=43+row*15; fill_rect(7,y,226,13,i==selected_app?selected:card);
        if(i==selected_app) { fill_rect(7,y,3,13,accent); } draw_text(kApps[i],15,y+3,1,i==selected_app?accent:white); }
    char p[20]; std::snprintf(p,sizeof(p),"PAGE %d/%d",page+1,pages); draw_text(p,8,122,1,accent);
    draw_text("UP/DOWN MOVE ENTER OPEN",85,122,1,muted);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,kWidth,kHeight,framebuffer));
}

void init_display() {
    gpio_config_t bl = {};
    bl.pin_bit_mask = 1ULL << kLcdBacklight; bl.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_config_t boot = {};
    boot.pin_bit_mask = 1ULL << GPIO_NUM_0;
    boot.mode = GPIO_MODE_INPUT;
    boot.pull_up_en = GPIO_PULLUP_ENABLE;
    boot.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&boot));
    ESP_ERROR_CHECK(gpio_set_level(kLcdBacklight,1));
    spi_bus_config_t bus = {};
    bus.sclk_io_num=kLcdSclk; bus.mosi_io_num=kLcdMosi; bus.miso_io_num=-1;
    bus.quadwp_io_num=-1; bus.quadhd_io_num=-1;
    bus.max_transfer_sz=kWidth*kHeight*sizeof(std::uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST,&bus,SPI_DMA_CH_AUTO));
    esp_lcd_panel_io_handle_t io=nullptr;
    esp_lcd_panel_io_spi_config_t io_config={};
    io_config.dc_gpio_num=kLcdDc; io_config.cs_gpio_num=kLcdCs; io_config.pclk_hz=40000000;
    io_config.lcd_cmd_bits=8; io_config.lcd_param_bits=8; io_config.spi_mode=0; io_config.trans_queue_depth=10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST,&io_config,&io));
    esp_lcd_panel_dev_config_t pc={};
    pc.reset_gpio_num=kLcdReset; pc.rgb_ele_order=LCD_RGB_ELEMENT_ORDER_BGR; pc.bits_per_pixel=16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io,&pc,&panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel,true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel,true,false));
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel,40,52));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel,true));
    ESP_LOGI(kTag,"Display initialized");
}

esp_err_t keyboard_write(std::uint8_t reg,std::uint8_t value) {
    const std::uint8_t packet[2] = {reg, value};
    return i2c_master_write_to_device(I2C_NUM_0,kKeyboardAddress,
        packet,sizeof(packet),pdMS_TO_TICKS(100));
}
esp_err_t keyboard_read(std::uint8_t reg,std::uint8_t* value) {
    return i2c_master_write_read_device(I2C_NUM_0,kKeyboardAddress,&reg,1,value,1,pdMS_TO_TICKS(100));
}

void init_keyboard() {
    i2c_config_t cfg={};
    cfg.mode=I2C_MODE_MASTER; cfg.sda_io_num=kI2cSda; cfg.scl_io_num=kI2cScl;
    cfg.sda_pullup_en=GPIO_PULLUP_ENABLE; cfg.scl_pullup_en=GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed=400000;
    if(i2c_param_config(I2C_NUM_0,&cfg)!=ESP_OK || i2c_driver_install(I2C_NUM_0,cfg.mode,0,0,0)!=ESP_OK) {
        ESP_LOGW(kTag,"I2C initialization failed"); return;
    }
    // Cardputer ADV uses a TCA8418 keypad controller at 0x34, scanning 7x8.
    if(keyboard_write(0x1D,0x7F)!=ESP_OK || keyboard_write(0x1E,0xFF)!=ESP_OK ||
       keyboard_write(0x1F,0x00)!=ESP_OK || keyboard_write(0x01,0x01)!=ESP_OK) {
        ESP_LOGW(kTag,"TCA8418 keyboard not detected"); return;
    }
    keyboard_ready=true;
    ESP_LOGI(kTag,"TCA8418 keyboard initialized");
}

void init_sd_card() {
    spi_bus_config_t bus={};
    bus.mosi_io_num=GPIO_NUM_14; bus.miso_io_num=GPIO_NUM_39;
    bus.sclk_io_num=GPIO_NUM_40; bus.quadwp_io_num=-1; bus.quadhd_io_num=-1;
    bus.max_transfer_sz=4000;
    if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK) {
        ESP_LOGW(kTag,"microSD SPI bus initialization failed"); return;
    }
    sdmmc_host_t host=SDSPI_HOST_DEFAULT(); host.slot=SPI2_HOST;
    sdspi_device_config_t slot=SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs=GPIO_NUM_12; slot.host_id=SPI2_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mount={};
    mount.format_if_mount_failed=false; mount.max_files=5; mount.allocation_unit_size=16*1024;
    const esp_err_t err=esp_vfs_fat_sdspi_mount(kSdMountPoint,&host,&slot,&mount,&sd_card);
    if(err==ESP_OK) { sd_ready=true; ESP_LOGI(kTag,"microSD mounted at %s",kSdMountPoint); }
    else ESP_LOGW(kTag,"microSD unavailable: %s",esp_err_to_name(err));
}

void poll_keyboard() {
    if(!keyboard_ready) return;
    std::uint8_t count=0;
    if(keyboard_read(0x03,&count)!=ESP_OK) return;
    count &= 0x0F;
    while(count--) {
        std::uint8_t event=0;
        if(keyboard_read(0x04,&event)!=ESP_OK || event==0) break;
        // TCA8418 sets bit 7 for RELEASE; bit 7 clear means PRESS.
        if((event & 0x80) == 0) continue;
        const std::uint8_t key = event & 0x7F;
        // ADV matrix values are decimal row/column scan codes, not hex key IDs.
        // Cardputer navigation keys from the ADV 4x14 matrix: comma=44, semicolon=53, period=48, slash=54.
        if(active_app >= 0) {
            if(key==65) { active_app=-1; file_scroll=0; }
            else if(key==53 || key==54) { if(active_app==0 && file_scroll>0) --file_scroll; else if(active_app==2) --calc_value; else if(active_app==3 && setting_brightness>0) --setting_brightness; }
            else if(key==58 || key==64) { if(active_app==0) ++file_scroll; else if(active_app==2) ++calc_value; else if(active_app==3 && setting_brightness<2) ++setting_brightness; }
            else if(key==67) {
                if(active_app==2) calc_value=0;
                else if(active_app==1 && sd_ready) { mkdir("/sdcard/MCAI-OS",0775); mkdir("/sdcard/MCAI-OS/notes",0775); FILE* note=std::fopen("/sdcard/MCAI-OS/notes/Welcome.txt","a"); if(note) { std::fputs("Welcome to MCAI-OS notes.\n",note); std::fclose(note); } }
            }
        } else if(key==53 || key==44) selected_app=(selected_app+kAppCount-1)%kAppCount;
        else if(key==48 || key==54) selected_app=(selected_app+1)%kAppCount;
        else if(key==67) { active_app=selected_app; ESP_LOGI(kTag,"Opened app: %s",kApps[selected_app]); }
    }
    std::uint8_t status=0;
    if(keyboard_read(0x02,&status)==ESP_OK && (status&0x01)) keyboard_write(0x02,0x01);
}

} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag,"Starting MCAI-OS Cardputer ADV - keypad input enabled");
    init_display();
    init_keyboard();
    init_sd_card();
    render_home();
    for(;;) {
        const int previous = selected_app;
        const int previous_page = active_app;
        poll_keyboard();

        // GPIO0 is the Cardputer ADV BOOT/shoulder button and acts as Select.
        const bool boot_pressed = gpio_get_level(GPIO_NUM_0) == 0;
        if(boot_pressed && !boot_button_was_pressed) {
            if(active_app >= 0) active_app = -1;
            else active_app = selected_app;
        }
        boot_button_was_pressed = boot_pressed;

        if(previous != selected_app || previous_page != active_app) render_home();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
