#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>

namespace {
constexpr char kTag[] = "MCAI-OS";
constexpr int kWidth = 240;
constexpr int kHeight = 135;

// M5Stack Cardputer ADV (Stamp-S3A) official LCD pin map.
constexpr gpio_num_t kLcdBacklight = GPIO_NUM_38;
constexpr gpio_num_t kLcdReset = GPIO_NUM_33;
constexpr gpio_num_t kLcdDc = GPIO_NUM_34;
constexpr gpio_num_t kLcdMosi = GPIO_NUM_35;
constexpr gpio_num_t kLcdSclk = GPIO_NUM_36;
constexpr gpio_num_t kLcdCs = GPIO_NUM_37;

esp_lcd_panel_handle_t panel = nullptr;
std::uint16_t framebuffer[kWidth * kHeight];

constexpr std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

void fill_rect(int x, int y, int w, int h, std::uint16_t color) {
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(kWidth, x + w);
    const int y1 = std::min(kHeight, y + h);
    for (int row = y0; row < y1; ++row) {
        std::fill(framebuffer + row * kWidth + x0,
                  framebuffer + row * kWidth + x1, color);
    }
}

const std::uint8_t* glyph(char c) {
    // Five-column, seven-row compact font for the boot screen.
    static const std::uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const std::uint8_t letters[26][5] = {
        {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36},
        {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C},
        {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x09,0x01},
        {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F},
        {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
        {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40},
        {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F},
        {0x3E,0x41,0x41,0x41,0x3E}, {0x7F,0x09,0x09,0x09,0x06},
        {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46},
        {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
        {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F},
        {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63},
        {0x07,0x08,0x70,0x08,0x07}, {0x61,0x51,0x49,0x45,0x43}
    };
    static const std::uint8_t digits[10][5] = {
        {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00},
        {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31},
        {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39},
        {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03},
        {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}
    };
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    static const std::uint8_t dash[5] = {0x08,0x08,0x08,0x08,0x08};
    if (c == '-') return dash;
    return blank;
}

void draw_text(const char* text, int x, int y, int scale, std::uint16_t color) {
    for (const char* p = text; *p; ++p, x += 6 * scale) {
        const auto* bits = glyph(*p);
        for (int col = 0; col < 5; ++col) {
            for (int row = 0; row < 7; ++row) {
                if (bits[col] & (1U << row)) {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void draw_boot_screen() {
    const auto bg = rgb565(11, 16, 29);
    const auto panel_bg = rgb565(22, 31, 49);
    const auto accent = rgb565(87, 231, 190);
    const auto white = rgb565(244, 248, 255);
    const auto muted = rgb565(150, 166, 190);

    std::fill(std::begin(framebuffer), std::end(framebuffer), bg);
    fill_rect(0, 0, kWidth, 4, accent);
    fill_rect(12, 17, 216, 101, panel_bg);
    fill_rect(12, 17, 4, 101, accent);
    draw_text("MCAI-OS", 29, 31, 3, accent);
    draw_text("CARDPUTER ADV", 30, 65, 1, white);
    fill_rect(29, 83, 182, 1, rgb565(53, 68, 91));
    draw_text("DISPLAY ONLINE", 30, 94, 1, muted);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, kWidth, kHeight, framebuffer));
}

void init_display() {
    gpio_config_t backlight = {};
    backlight.pin_bit_mask = 1ULL << kLcdBacklight;
    backlight.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&backlight));
    ESP_ERROR_CHECK(gpio_set_level(kLcdBacklight, 1));

    spi_bus_config_t bus = {};
    bus.sclk_io_num = kLcdSclk;
    bus.mosi_io_num = kLcdMosi;
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = kWidth * kHeight * sizeof(std::uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = nullptr;
    esp_lcd_panel_io_spi_config_t io_config = {};
    io_config.dc_gpio_num = kLcdDc;
    io_config.cs_gpio_num = kLcdCs;
    io_config.pclk_hz = 40000000;
    io_config.lcd_cmd_bits = 8;
    io_config.lcd_param_bits = 8;
    io_config.spi_mode = 0;
    io_config.trans_queue_depth = 10;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &io));

    esp_lcd_panel_dev_config_t panel_config = {};
    panel_config.reset_gpio_num = kLcdReset;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR;
    panel_config.bits_per_pixel = 16;
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel, true));
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, false));
    // The ADV's 135x240 ST7789 glass is offset inside the controller RAM.
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel, 40, 52));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    vTaskDelay(pdMS_TO_TICKS(30));
    ESP_LOGI(kTag, "Cardputer ADV ST7789 display initialized");
}
} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "Starting MCAI-OS Cardputer ADV");
    init_display();
    draw_boot_screen();

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
