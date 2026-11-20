#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "mbedtls/sha256.h"
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
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <sys/time.h>
#include <unistd.h>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

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
// Normalized key actions produced from the Cardputer ADV's remapped 4x14 matrix.
constexpr std::uint8_t KEY_ESC=1, KEY_FN=2, KEY_CAPS=3, KEY_UP=4, KEY_DOWN=5,
                        KEY_LEFT=6, KEY_RIGHT=7, KEY_BACKSPACE=8, KEY_ENTER=9,
                        KEY_TAB=10, KEY_CTRL=11, KEY_ALT=12, KEY_OPT=13, KEY_DELETE=14;
bool caps_lock = false;
bool shift_held = false;
bool fn_lock = false;
bool is_up(std::uint8_t k){return k==KEY_UP;}
bool is_down(std::uint8_t k){return k==KEY_DOWN;}
bool is_left(std::uint8_t k){return k==KEY_LEFT;}
bool is_right(std::uint8_t k){return k==KEY_RIGHT;}
bool is_back(std::uint8_t k){return k==KEY_BACKSPACE||k==KEY_ESC;}
constexpr char kSdMountPoint[] = "/sdcard";

esp_lcd_panel_handle_t panel = nullptr;
sdmmc_card_t* sd_card = nullptr;
bool keyboard_ready = false;
bool sd_ready = false;
int selected_app = 0;
int active_app = -1;
int setting_brightness = 1;
int settings_selection=0;
bool sound_enabled=true, audio_ready=false;
int wifi_setup_step=0;
char wifi_ssid_input[33]={};
char wifi_password_input[65]={};
bool bluetooth_screen=false, ble_ready=false, ble_scan_active=false;
bool ble_complex_mode=false;
bool ble_device_named[12]={};
int last_clock_render_second=-1;
bool wifi_driver_ready=false;
bool wifi_connected=false;
bool wifi_connecting=false;
char wifi_task_ssid[33]={};
char wifi_task_password[65]={};
char wifi_ip_text[16]="0.0.0.0";
esp_netif_t* wifi_sta_netif=nullptr;
char wifi_networks[16][33]={}; int wifi_network_rssi[16]={}; int wifi_network_auth[16]={};
int wifi_network_count=0, wifi_network_selected=0, wifi_network_scroll=0;
int ble_device_count=0, ble_device_scroll=0;
char ble_devices[12][32]={};
char ble_rssi[12][8]={};
char ble_addresses[12][18]={};
esp_ble_scan_params_t ble_scan_params={};
char calc_buffer[48] = {};
int calc_length = 0;
bool calc_showing_result=false;
bool calc_error=false;
char calc_error_text[24]={};
int file_scroll = 0;
char note_buffer[512] = {};
int note_length = 0, note_scroll = 0;
char note_files[16][40] = {};
std::uint8_t note_folder[16] = {};
int note_count=0, note_selected=0, notes_action=1; // 1 = Add Note button, 0 = note list
char note_filename[112] = {};
bool note_editing=false;
char last_char = '-';
char last_key_label[20] = "NONE";
unsigned keypress_count = 0;
char current_dir[160] = "/sdcard";
char file_names[32][64] = {};
bool file_is_dir[32] = {};
int file_count = 0, file_selected = 0;
char viewer_buffer[700] = {};
bool viewing_file = false;
char store_names[128][64] = {}; char store_status[128][20] = {}; char store_ids[128][64] = {}; char store_urls[128][256] = {}; char store_hashes[128][65] = {}; size_t store_sizes[128] = {};
int store_count=0, store_selected=0, store_scroll=0;
bool store_loading=false;
bool ota_running=false;
char ota_message[48]="PRESS ENTER TO CHECK FOR UPDATES";
char store_message[40]="ADD WIFI.CFG ON SD"; bool wifi_started=false, catalogue_loaded=false, ntp_started=false;
char catalogue_buffer[32768]={}; size_t catalogue_length=0;
bool package_running=false; int package_return_app=5; char package_title[64]=""; char package_subtitle[96]=""; char package_id[64]=""; int package_counter=0, package_increment=1;
char clock_input[8] = {};
int clock_len = 0;
int timer_seconds = 60;
char timer_input[20]="00:01:00";
int timer_field=2; // 0=hours, 1=minutes, 2=seconds
bool stopwatch_running=false;
std::int64_t stopwatch_started_us=0, stopwatch_elapsed_us=0;
bool timer_running = false;
std::int64_t timer_end_us = 0;
bool game_started = false;
int snake_x[48] = {}, snake_y[48] = {}, snake_len = 3, snake_dx = 1, snake_dy = 0;
int food_x = 12, food_y = 4, game_score = 0;
std::int64_t game_tick_us = 0;
bool game_over = false;
bool dark_theme = true;
bool ui_dirty=true;
bool boot_button_was_pressed = false;
constexpr int kAppsPerPage = 4;
constexpr const char* kApps[] = {"FILE MANAGER","NOTES","CALCULATOR","SETTINGS","SYSTEM INFO","APP STORE","CLOCK","TIMER","KEYBOARD TEST","STORAGE","DIAGNOSTICS","GAMES","ABOUT","STOPWATCH","OS UPDATER","WI-FI TESTER","MUSIC PLAYER"};
constexpr int kAppCount = sizeof(kApps) / sizeof(kApps[0]);
int launcher_order[kAppCount]={};
bool launcher_foldered[kAppCount]={};
bool organizer_mode=false, folder_open=false;
int organizer_selected=0, folder_selected=0;
int installed_package_count=0;
char music_files[16][64]={}; int music_count=0,music_selected=0,music_scroll=0;
volatile bool music_playing=false,music_stop_requested=false;
mp3dec_t music_mp3_decoder{};
std::uint8_t music_mp3_input[8192]{};
mp3d_sample_t music_mp3_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME]{};
std::int16_t music_mp3_output[512]{};
bool wifi_test_running=false,wifi_test_done=false,wifi_test_online=false;
int wifi_test_rssi=0,wifi_test_http=0; char wifi_test_ip[16]="0.0.0.0";
char wifi_test_error[32]="NOT TESTED";
char installed_package_ids[16][64]={};
char installed_package_titles[16][64]={};
alignas(4) std::uint16_t framebuffer[kWidth * kHeight];

constexpr std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return static_cast<std::uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
bool parse_timer_fields(int& hours,int& minutes,int& seconds) {
    int a=0,b=0,c=0;
    const int fields=std::sscanf(timer_input,"%d:%d:%d",&a,&b,&c);
    if(fields==3){hours=a;minutes=b;seconds=c;}
    else if(fields==2){hours=0;minutes=a;seconds=b;}
    else if(fields==1){hours=0;minutes=0;seconds=a;}
    else return false;
    return hours>=0&&hours<=99&&minutes>=0&&minutes<60&&seconds>=0&&seconds<60;
}
void format_timer_fields(int hours,int minutes,int seconds) {
    hours=std::max(0,std::min(99,hours));
    minutes=std::max(0,std::min(59,minutes));
    seconds=std::max(0,std::min(59,seconds));
    std::snprintf(timer_input,sizeof(timer_input),"%02d:%02d:%02d",hours,minutes,seconds);
    timer_seconds=hours*3600+minutes*60+seconds;
}
void adjust_timer_field(int delta) {
    int hours=0,minutes=0,seconds=0;
    if(!parse_timer_fields(hours,minutes,seconds)){hours=0;minutes=0;seconds=0;}
    int* fields[]={&hours,&minutes,&seconds};
    const int maximum=timer_field==0?99:59;
    *fields[timer_field]=(*fields[timer_field]+delta+maximum+1)%(maximum+1);
    format_timer_fields(hours,minutes,seconds);
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
    // Standard 5x7 lowercase glyphs. Each value is one vertical column;
    // keep the same 7-pixel height and 6-pixel character advance as capitals.
    static const std::uint8_t lowercase[26][5] = {
        {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38},
        {0x38,0x44,0x44,0x44,0x20}, {0x38,0x44,0x44,0x48,0x7F},
        {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02},
        {0x0C,0x52,0x52,0x52,0x3E}, {0x7F,0x08,0x04,0x04,0x78},
        {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00},
        {0x7F,0x10,0x28,0x44,0x00}, {0x00,0x41,0x7F,0x40,0x00},
        {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78},
        {0x38,0x44,0x44,0x44,0x38}, {0x7C,0x14,0x14,0x14,0x08},
        {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08},
        {0x48,0x54,0x54,0x54,0x20}, {0x04,0x3F,0x44,0x40,0x20},
        {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C},
        {0x3C,0x40,0x30,0x40,0x3C}, {0x44,0x28,0x10,0x28,0x44},
        {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}
    };
    if (c >= 'a' && c <= 'z') return lowercase[c - 'a'];
    if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
    if (c >= '0' && c <= '9') return digits[c - '0'];
    static const std::uint8_t dash[5]={8,8,8,8,8}, plus[5]={8,8,62,8,8};
    static const std::uint8_t star[5]={65,34,20,34,65}, slash[5]={32,16,8,4,2};
    static const std::uint8_t lp[5]={0,28,34,65,0}, rp[5]={0,65,34,28,0};
    static const std::uint8_t eq[5]={20,20,20,20,20}, dot[5]={0,96,96,0,0};
    static const std::uint8_t comma[5]={0,64,48,0,0}, colon[5]={0,54,54,0,0};
    static const std::uint8_t semi[5]={0,86,54,0,0}, under[5]={64,64,64,64,64};
    static const std::uint8_t bang[5]={0,0,95,0,0}, question[5]={2,1,81,9,6};
    static const std::uint8_t lb[5]={0,127,65,65,0}, rb[5]={0,65,65,127,0};
    static const std::uint8_t quote[5]={0,3,0,3,0}, apos[5]={0,3,0,0,0};
    static const std::uint8_t less[5]={8,20,34,65,0}, greater[5]={0,65,34,20,8};
    static const std::uint8_t at[5]={62,65,93,85,30}, hash[5]={20,127,20,127,20};
    static const std::uint8_t percent[5]={35,19,8,100,98}, amp[5]={54,73,85,34,80};
    static const std::uint8_t caret[5]={4,2,1,2,4}, pipe[5]={0,0,127,0,0};
    static const std::uint8_t bs[5]={2,4,8,16,32}, brace_l[5]={8,54,65,65,0}, brace_r[5]={0,65,65,54,8};
    static const std::uint8_t dollar[5]={18,42,127,42,36}, tilde[5]={4,2,4,8,4}, tick[5]={0,1,2,0,0};
    switch(c){
        case '-':return dash; case '+':return plus; case '*':return star; case '/':return slash;
        case '(':return lp; case ')':return rp; case '=':return eq; case '.':return dot;
        case ',':return comma; case ':':return colon; case ';':return semi; case '_':return under;
        case '!':return bang; case '?':return question; case '[':return lb; case ']':return rb;
        case '"':return quote; case 0x27:return apos; case '<':return less; case '>':return greater;
        case '@':return at; case '#':return hash; case '$':return dollar; case '%':return percent;
        case '&':return amp; case '^':return caret; case '|':return pipe; case 0x5c:return bs;
        case '{':return brace_l; case '}':return brace_r; case '~':return tilde; case 0x60:return tick;
        default:return blank;
    }
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

void draw_text_tail(const char* text,int x,int y,int max_chars,int scale,std::uint16_t color) {
    if(!text)return;
    const size_t n=std::strlen(text);
    draw_text(n>(size_t)max_chars?text+n-max_chars:text,x,y,scale,color);
}
void fill_round_rect(int x,int y,int w,int h,int radius,std::uint16_t color) {
    const int r=std::max(0,std::min(radius,std::min(w,h)/2));
    for(int py=0;py<h;py++) for(int px=0;px<w;px++) {
        int cx=px<r?r-1:(px>=w-r?w-r:px);
        int cy=py<r?r-1:(py>=h-r?h-r:py);
        int dx=px-cx,dy=py-cy;
        if((px>=r&&px<w-r)||(py>=r&&py<h-r)||dx*dx+dy*dy<=r*r)
            fill_rect(x+px,y+py,1,1,color);
    }
}
void draw_mcaI_logo_splash() {
    // Match the supplied 240x135 MCAI-OS boot artwork, leaving a clear
    // green strip at the bottom for the startup instruction.
    const auto green=rgb565(220,220,220), black=rgb565(0,0,0);
    const auto white=rgb565(254,254,254), grey=rgb565(115,115,115);
    const auto frame=rgb565(120,120,120), navy=rgb565(0,0,0);
    std::fill(std::begin(framebuffer),std::end(framebuffer),green);
    const char keys[]="MCAI";
    for(int i=0;i<4;i++) {
        const int x=1+i*59;
        fill_round_rect(x+2,3,57,48,8,black);
        fill_round_rect(x,0,57,47,8,black);
        fill_round_rect(x+2,2,53,42,7,white);
        char letter[2]={keys[i],0};
        draw_text(letter,x+15,10,4,black);
    }
    fill_rect(63,53,114,61,black);
    fill_rect(65,55,110,57,frame);
    fill_rect(67,57,106,53,grey);
    fill_rect(67,57,106,11,navy);
    fill_rect(67,69,106,41,grey);
    draw_text("OS",83,69,6,black);
    draw_text("STARTING MCAI-OS",72,120,1,black);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,kWidth,kHeight,framebuffer));
}

void apply_brightness() {
    // Cardputer ADV shares GPIO38 with the display power rail. Its backlight
    // only behaves correctly with the board's 256 Hz / 9-bit / channel 7 PWM.
    const std::uint32_t brightness = setting_brightness==0 ? 64U :
                                     setting_brightness==1 ? 160U : 255U;
    const std::uint32_t duty = brightness << 1;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7, duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_7));
}
void load_wifi_settings() {
    wifi_ssid_input[0]=0; wifi_password_input[0]=0;
    if(!sd_ready)return;
    FILE* f=std::fopen("/sdcard/MCAI-OS/wifi.cfg","r");
    if(!f)return;
    std::fgets(wifi_ssid_input,sizeof(wifi_ssid_input),f);
    std::fgets(wifi_password_input,sizeof(wifi_password_input),f);
    std::fclose(f);
    auto trim=[](char* v){size_t n=std::strlen(v);while(n&&(v[n-1]=='\n'||v[n-1]=='\r'))v[--n]=0;};
    trim(wifi_ssid_input);trim(wifi_password_input);
}
bool save_wifi_settings() {
    if(!sd_ready)return false;
    mkdir("/sdcard/MCAI-OS",0775);
    FILE* f=std::fopen("/sdcard/MCAI-OS/wifi.cfg","w");
    if(!f)return false;
    std::fprintf(f,"%s\n%s\n",wifi_ssid_input,wifi_password_input);
    return std::fclose(f)==0;
}
void save_settings() {
    if(!sd_ready) return;
    mkdir("/sdcard/MCAI-OS",0775);
    FILE* f=std::fopen("/sdcard/MCAI-OS/settings.cfg","w");
    if(f){std::fprintf(f,"%d %d %d\n",setting_brightness,dark_theme?1:0,sound_enabled?1:0);std::fclose(f);}
}
void load_settings() {
    if(!sd_ready) return;
    FILE* f=std::fopen("/sdcard/MCAI-OS/settings.cfg","r");
    if(f){int b=1,t=1,snd=1;int n=std::fscanf(f,"%d %d %d",&b,&t,&snd);if(n>=1){setting_brightness=std::max(0,std::min(2,b));if(n>=2)dark_theme=t!=0;if(n>=3)sound_enabled=snd!=0;}std::fclose(f);}
}
void refresh_notes() {
    note_count=0;
    if(!sd_ready){std::snprintf(store_message,sizeof(store_message),"NOTES NEED SD CARD");return;}
    mkdir("/sdcard/MCAI-OS",0775);
    mkdir("/sdcard/MCAI-OS/notes",0775);
    // Read the current folder and older Cardputer notes folders so existing
    // notes created by previous MCAI-OS builds remain visible.
    const char* folders[]={"/sdcard/MCAI-OS/notes","/sdcard/notes","/sdcard/Notes"};
    for(std::uint8_t folder_index=0;folder_index<3;folder_index++) {
        const char* folder=folders[folder_index];
        DIR* d=opendir(folder);
        if(!d)continue;
        struct dirent* e;
        while((e=readdir(d))!=nullptr && note_count<16) {
            const size_t n=std::strlen(e->d_name);
            if(n<5 || e->d_name[0]=='.' ||
               std::tolower((unsigned char)e->d_name[n-4])!='.' ||
               std::tolower((unsigned char)e->d_name[n-3])!='t' ||
               std::tolower((unsigned char)e->d_name[n-2])!='x' ||
               std::tolower((unsigned char)e->d_name[n-1])!='t')continue;
            char full[112];
            std::snprintf(full,sizeof(full),"%s/%.39s",folder,e->d_name);
            struct stat st{};
            if(stat(full,&st)!=0 || !S_ISREG(st.st_mode))continue;
            bool duplicate=false;
            for(int i=0;i<note_count;i++)if(note_folder[i]==folder_index && std::strcmp(note_files[i],e->d_name)==0)duplicate=true;
            if(duplicate)continue;
            std::snprintf(note_files[note_count],sizeof(note_files[0]),"%.39s",e->d_name);
            note_folder[note_count]=folder_index;
            ++note_count;
        }
        closedir(d);
    }
    for(int i=0;i<note_count;i++)for(int j=i+1;j<note_count;j++)if(std::strcmp(note_files[j],note_files[i])<0){
        char name[40];const std::uint8_t folder_index=note_folder[i];
        std::memcpy(name,note_files[i],sizeof(name));std::memcpy(note_files[i],note_files[j],sizeof(name));std::memcpy(note_files[j],name,sizeof(name));
        note_folder[i]=note_folder[j];note_folder[j]=folder_index;
    }
    note_selected=std::min(note_selected,std::max(0,note_count-1));
    note_scroll=0;
    notes_action=note_count>0?0:1;
    std::snprintf(store_message,sizeof(store_message),note_count>0?"FOUND %d SAVED NOTES":"NO SAVED NOTES - ADD ONE",note_count);
}
bool save_note() {
    if(!sd_ready || !note_filename[0] || note_length<0 || note_length>=(int)sizeof(note_buffer)){
        std::snprintf(store_message,sizeof(store_message),"INSERT SD / CHECK NOTE");return false;
    }
    // Direct FAT write avoids the temp/rename/fsync sequence that can fail on SD.
    FILE* f=std::fopen(note_filename,"wb");
    if(!f){std::snprintf(store_message,sizeof(store_message),"NOTE FILE OPEN FAILED");return false;}
    const size_t written=std::fwrite(note_buffer,1,(size_t)note_length,f);
    const bool flush_ok=std::fflush(f)==0;
    const bool close_ok=std::fclose(f)==0;
    if(written!=(size_t)note_length||!flush_ok||!close_ok){
        std::snprintf(store_message,sizeof(store_message),"NOTE WRITE FAILED");return false;
    }
    struct stat verify{};
    if(stat(note_filename,&verify)!=0||verify.st_size!=note_length){
        std::snprintf(store_message,sizeof(store_message),"NOTE VERIFY FAILED");return false;
    }
    std::snprintf(store_message,sizeof(store_message),"NOTE SAVED");return true;
}
void open_note(int index) {
    if(!sd_ready || index<0 || index>=note_count)return;
    const char* folders[]={"/sdcard/MCAI-OS/notes","/sdcard/notes","/sdcard/Notes"};
    char path[112];
    std::snprintf(path,sizeof(path),"%s/%.39s",folders[note_folder[index]],note_files[index]);
    FILE* f=std::fopen(path,"r");
    if(!f){std::snprintf(store_message,sizeof(store_message),"CANNOT OPEN NOTE");return;}
    note_length=(int)std::fread(note_buffer,1,sizeof(note_buffer)-1,f);
    note_buffer[note_length]=0;
    std::fclose(f);
    std::snprintf(note_filename,sizeof(note_filename),"%s",path);
    note_editing=true;note_scroll=0;
    std::snprintf(store_message,sizeof(store_message),"NOTE OPENED");
}
void create_note() {
    if(!sd_ready){std::snprintf(store_message,sizeof(store_message),"INSERT SD CARD FIRST");return;}
    mkdir("/sdcard/MCAI-OS",0775);
    mkdir("/sdcard/MCAI-OS/notes",0775);
    char name[40]={};int number=1;
    for(;number<1000;number++){
        std::snprintf(name,sizeof(name),"NOTE-%03d.txt",number);
        char path[96];std::snprintf(path,sizeof(path),"/sdcard/MCAI-OS/notes/%s",name);
        struct stat st{};if(stat(path,&st)!=0)break;
    }
    if(number>=1000){std::snprintf(store_message,sizeof(store_message),"NOTE LIMIT REACHED");return;}
    std::snprintf(note_filename,sizeof(note_filename),"/sdcard/MCAI-OS/notes/%s",name);
    note_buffer[0]=0;note_length=0;note_scroll=0;note_editing=true;
    if(save_note()){refresh_notes();for(int i=0;i<note_count;i++)if(std::strcmp(note_files[i],name)==0){note_selected=i;break;}}
    else note_editing=false;
}
bool eval_expr(const char*& p,double& out);
bool eval_factor(const char*& p,double& out) {
    while(*p==' ') ++p;
    if(*p=='-'){++p;if(!eval_factor(p,out))return false;out=-out;return true;}
    if(*p=='('){++p;if(!eval_expr(p,out))return false;while(*p==' ')++p;if(*p!=')')return false;++p;return true;}
    char* end=nullptr;out=std::strtod(p,&end);if(end==p)return false;p=end;if(*p=='%'){++p;out/=100.0;}return std::isfinite(out);
}
bool eval_term(const char*& p,double& out) {
    if(!eval_factor(p,out))return false;
    for(;;){while(*p==' ')++p;char op=*p;if(op!='*'&&op!='/')return true;++p;double rhs=0;if(!eval_factor(p,rhs))return false;if(op=='/'&&rhs==0)return false;out=op=='*'?out*rhs:out/rhs;}
}
bool eval_expr(const char*& p,double& out) {
    if(!eval_term(p,out))return false;
    for(;;){while(*p==' ')++p;char op=*p;if(op!='+'&&op!='-')return true;++p;double rhs=0;if(!eval_term(p,rhs))return false;out=op=='+'?out+rhs:out-rhs;}
}
bool calculate() {
    const char* p=calc_buffer;double result=0;
    if(!eval_expr(p,result)){calc_error=true;std::snprintf(calc_error_text,sizeof(calc_error_text),"INVALID / DIV0");return false;}
    while(*p==' ') ++p;
    if(*p){
        calc_error=true;
        std::snprintf(calc_error_text,sizeof(calc_error_text),"INVALID INPUT");
        return false;
    }
    std::snprintf(calc_buffer,sizeof(calc_buffer),"%.8g",result);calc_length=(int)std::strlen(calc_buffer);calc_error=false;calc_error_text[0]=0;return true;
}
void refresh_files() {
    file_count=0; file_selected=0; file_scroll=0;
    DIR* d=opendir(current_dir); if(!d)return;
    struct dirent* e;
    while((e=readdir(d))!=nullptr && file_count<32) {
        if(e->d_name[0]=='.')continue;
        std::snprintf(file_names[file_count],sizeof(file_names[file_count]),"%.63s",e->d_name);
        char full[240];std::snprintf(full,sizeof(full),"%.159s/%.63s",current_dir,e->d_name);
        struct stat st{};file_is_dir[file_count]=(stat(full,&st)==0 && S_ISDIR(st.st_mode));
        ++file_count;
    }
    closedir(d);
}
esp_err_t catalogue_http_event(esp_http_client_event_t* evt) {
    if(evt->event_id==HTTP_EVENT_ON_DATA && evt->data && evt->data_len>0 &&
       catalogue_length+(size_t)evt->data_len<sizeof(catalogue_buffer)-1) {
        std::memcpy(catalogue_buffer+catalogue_length,evt->data,(size_t)evt->data_len);
        catalogue_length+=(size_t)evt->data_len; catalogue_buffer[catalogue_length]=0;
    }
    return ESP_OK;
}
void ble_gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    switch(event) {
    case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
        if(param->scan_param_cmpl.status==ESP_BT_STATUS_SUCCESS) {
            const esp_err_t e=esp_ble_gap_start_scanning(6);
            ble_scan_active=(e==ESP_OK);
            if(e!=ESP_OK){std::snprintf(store_message,sizeof(store_message),"BLE SCAN START FAIL");ESP_LOGE(kTag,"BLE scan start: %s",esp_err_to_name(e));}
        } else {
            ble_scan_active=false;
            std::snprintf(store_message,sizeof(store_message),"BLE PARAM SET FAIL");
            ESP_LOGE(kTag,"BLE scan parameters failed: %d",param->scan_param_cmpl.status);
        }
        ui_dirty=true;
        break;
    case ESP_GAP_BLE_SCAN_RESULT_EVT:
        if(param->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT) {
            uint8_t name_len=0;
            uint8_t* name=esp_ble_resolve_adv_data(param->scan_rst.ble_adv,ESP_BLE_AD_TYPE_NAME_CMPL,&name_len);
            char label[32]={};
            if(name && name_len) {
                size_t n=std::min((size_t)31,(size_t)name_len);
                std::memcpy(label,name,n); label[n]=0;
            } else {
                if(!ble_complex_mode) break;
                std::snprintf(label,sizeof(label),"UNNAMED DEVICE");
            }
            char address[18];
            std::snprintf(address,sizeof(address),"%02X:%02X:%02X:%02X:%02X:%02X",
                param->scan_rst.bda[0],param->scan_rst.bda[1],param->scan_rst.bda[2],
                param->scan_rst.bda[3],param->scan_rst.bda[4],param->scan_rst.bda[5]);
            int found=-1;
            for(int i=0;i<ble_device_count;i++) if(std::strcmp(ble_addresses[i],address)==0){found=i;break;}
            if(found<0 && ble_device_count<12) found=ble_device_count++;
            if(found>=0) {
                std::snprintf(ble_devices[found],sizeof(ble_devices[found]),"%.31s",label);
                ble_device_named[found]=(name && name_len>0);
                std::snprintf(ble_addresses[found],sizeof(ble_addresses[found]),"%s",address);
                std::snprintf(ble_rssi[found],sizeof(ble_rssi[found]),"%d",param->scan_rst.rssi);
            }
            ui_dirty=true;
        } else if(param->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            ble_scan_active=false;
            ui_dirty=true;
        }
        break;
    case ESP_GAP_BLE_SCAN_START_COMPLETE_EVT:
        if(param->scan_start_cmpl.status!=ESP_BT_STATUS_SUCCESS){ble_scan_active=false;std::snprintf(store_message,sizeof(store_message),"BLE SCAN START FAIL");}
        ui_dirty=true;
        break;
    case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
        ble_scan_active=false; ui_dirty=true; break;
    default: break;
    }
}
bool init_bluetooth() {
    if(ble_ready) return true;
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();e=nvs_flash_init();}
    if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE){std::snprintf(store_message,sizeof(store_message),"BT NVS INIT FAIL");ESP_LOGE(kTag,"BT NVS init: %s",esp_err_to_name(e));return false;}
    e=esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if(e!=ESP_OK && e!=ESP_ERR_NOT_SUPPORTED && e!=ESP_ERR_INVALID_STATE) {
        std::snprintf(store_message,sizeof(store_message),"BT MEMORY RELEASE FAIL");ESP_LOGE(kTag,"BT memory release: %s",esp_err_to_name(e)); return false;
    }
    esp_bt_controller_status_t status=esp_bt_controller_get_status();
    if(status==ESP_BT_CONTROLLER_STATUS_IDLE) {
        esp_bt_controller_config_t cfg=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        e=esp_bt_controller_init(&cfg);
        if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) { std::snprintf(store_message,sizeof(store_message),"BT CTRL INIT FAIL");ESP_LOGE(kTag,"BT controller init: %s",esp_err_to_name(e)); return false; }
    }
    status=esp_bt_controller_get_status();
    if(status==ESP_BT_CONTROLLER_STATUS_INITED) {
        e=esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) { std::snprintf(store_message,sizeof(store_message),"BT CTRL ENABLE FAIL");ESP_LOGE(kTag,"BT controller enable: %s",esp_err_to_name(e)); return false; }
    }
    esp_bluedroid_status_t host=esp_bluedroid_get_status();
    if(host==ESP_BLUEDROID_STATUS_UNINITIALIZED) {
        e=esp_bluedroid_init();
        if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) { std::snprintf(store_message,sizeof(store_message),"BT HOST INIT FAIL");ESP_LOGE(kTag,"Bluedroid init: %s",esp_err_to_name(e)); return false; }
        host=esp_bluedroid_get_status();
    }
    if(host==ESP_BLUEDROID_STATUS_INITIALIZED) {
        e=esp_bluedroid_enable();
        if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) { std::snprintf(store_message,sizeof(store_message),"BT HOST ENABLE FAIL");ESP_LOGE(kTag,"Bluedroid enable: %s",esp_err_to_name(e)); return false; }
    }
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_ENABLED){
        std::snprintf(store_message,sizeof(store_message),"BT CTRL NOT ENABLED");return false;
    }
    if(esp_bluedroid_get_status()!=ESP_BLUEDROID_STATUS_ENABLED){
        std::snprintf(store_message,sizeof(store_message),"BT HOST NOT ENABLED");return false;
    }
    e=esp_ble_gap_register_callback(ble_gap_callback);
    if(e!=ESP_OK && e!=ESP_ERR_INVALID_STATE) { std::snprintf(store_message,sizeof(store_message),"BT GAP CALLBACK FAIL");ESP_LOGE(kTag,"BLE GAP callback: %s",esp_err_to_name(e)); return false; }
    ble_ready=true;
    ESP_LOGI(kTag,"Bluetooth LE initialized");
    return true;
}
bool start_ble_scan() {
    ble_device_count=0; ble_device_scroll=0;
    std::memset(ble_devices,0,sizeof(ble_devices));
    std::memset(ble_rssi,0,sizeof(ble_rssi));
    std::memset(ble_addresses,0,sizeof(ble_addresses));
    if(!init_bluetooth()) return false;
    ble_scan_params.scan_type=BLE_SCAN_TYPE_PASSIVE;
    ble_scan_params.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
    ble_scan_params.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL;
    ble_scan_params.scan_interval=0x50;
    ble_scan_params.scan_window=0x20;
    ble_scan_params.scan_duplicate=BLE_SCAN_DUPLICATE_DISABLE;
    const esp_err_t e=esp_ble_gap_set_scan_params(&ble_scan_params);
    if(e!=ESP_OK) { std::snprintf(store_message,sizeof(store_message),"BLE SCAN SETUP FAIL"); ESP_LOGE(kTag,"BLE set scan params: %s",esp_err_to_name(e)); return false; }
    std::snprintf(store_message,sizeof(store_message),"SCANNING FOR 6 SECONDS");
    return true;
}
void stop_ble_scan() {
    if(ble_ready && ble_scan_active) esp_ble_gap_stop_scanning();
    ble_scan_active=false;
}
void start_time_sync() {
    setenv("TZ","EST5EDT,M3.2.0/2,M11.1.0/2",1);
    tzset();
    if(!ntp_started) {
        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0,"pool.ntp.org");
        esp_sntp_init();
        ntp_started=true;
        ESP_LOGI(kTag,"NTP time sync started");
    }
}
bool init_wifi_driver() {
    if(wifi_driver_ready) return true;
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();e=nvs_flash_init();}
    if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    e=esp_netif_init();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    e=esp_event_loop_create_default();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    if(!wifi_sta_netif){
        wifi_sta_netif=esp_netif_create_default_wifi_sta();
        if(!wifi_sta_netif){ESP_LOGE(kTag,"Wi-Fi station interface creation failed");return false;}
    }
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    e=esp_wifi_init(&init);if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    e=esp_wifi_set_mode(WIFI_MODE_STA);if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    e=esp_wifi_start();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    wifi_driver_ready=true;
    return true;
}
bool scan_wifi_networks() {
    wifi_network_count=0;wifi_network_selected=0;wifi_network_scroll=0;
    if(!init_wifi_driver()){std::snprintf(store_message,sizeof(store_message),"WIFI INIT FAILED");return false;}
    // Clear any stale/unfinished scan before starting a fresh full-band scan.
    esp_wifi_scan_stop();
    vTaskDelay(pdMS_TO_TICKS(40));
    wifi_scan_config_t scan={};
    scan.scan_type=WIFI_SCAN_TYPE_ACTIVE;
    scan.show_hidden=true;
    scan.scan_time.active.min=100;
    scan.scan_time.active.max=600;
    const esp_err_t e=esp_wifi_scan_start(&scan,true);
    if(e!=ESP_OK){std::snprintf(store_message,sizeof(store_message),"WIFI SCAN FAILED");ESP_LOGE(kTag,"Wi-Fi scan: %s",esp_err_to_name(e));return false;}
    uint16_t count=16;
    wifi_ap_record_t records[16]={};
    if(esp_wifi_scan_get_ap_records(&count,records)!=ESP_OK){std::snprintf(store_message,sizeof(store_message),"WIFI RESULTS FAILED");return false;}
    for(int i=0;i<(int)count&&i<16;i++){
        if(records[i].ssid[0]==0)continue;
        std::snprintf(wifi_networks[wifi_network_count],sizeof(wifi_networks[0]),"%.32s",reinterpret_cast<const char*>(records[i].ssid));
        wifi_network_rssi[wifi_network_count]=records[i].rssi;
        wifi_network_auth[wifi_network_count]=(int)records[i].authmode;
        ++wifi_network_count;
    }
    for(int i=0;i<wifi_network_count;i++)for(int j=i+1;j<wifi_network_count;j++)if(wifi_network_rssi[j]>wifi_network_rssi[i]){
        char name[33];std::memcpy(name,wifi_networks[i],sizeof(name));std::memcpy(wifi_networks[i],wifi_networks[j],sizeof(name));std::memcpy(wifi_networks[j],name,sizeof(name));
        int r=wifi_network_rssi[i];wifi_network_rssi[i]=wifi_network_rssi[j];wifi_network_rssi[j]=r;
        int auth=wifi_network_auth[i];wifi_network_auth[i]=wifi_network_auth[j];wifi_network_auth[j]=auth;
    }
    std::snprintf(store_message,sizeof(store_message),wifi_network_count?"FOUND %d NETWORKS":"NO WIFI NETWORKS FOUND",wifi_network_count);
    return wifi_network_count>0;
}
bool connect_wifi_credentials(const char* ssid,const char* password) {
    wifi_connected=false;std::snprintf(wifi_ip_text,sizeof(wifi_ip_text),"0.0.0.0");
    if(!ssid||!ssid[0]||std::strlen(ssid)>32||!password||std::strlen(password)>64||!init_wifi_driver()){
        std::snprintf(store_message,sizeof(store_message),"WIFI INIT / INPUT FAILED");return false;
    }
    wifi_config_t cfg={};
    std::memcpy(cfg.sta.ssid,ssid,std::strlen(ssid));
    std::memcpy(cfg.sta.password,password,std::strlen(password));
    cfg.sta.threshold.authmode=WIFI_AUTH_OPEN;
    esp_wifi_disconnect();
    if(esp_wifi_set_config(WIFI_IF_STA,&cfg)!=ESP_OK||esp_wifi_connect()!=ESP_OK){
        std::snprintf(store_message,sizeof(store_message),"WIFI CONNECT REQUEST FAILED");return false;
    }
    wifi_started=true;
    for(int i=0;i<32;i++){
        wifi_ap_record_t ap{};
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK){
            esp_netif_ip_info_t ip{};
            if(wifi_sta_netif&&esp_netif_get_ip_info(wifi_sta_netif,&ip)==ESP_OK&&ip.ip.addr!=0){
                std::snprintf(wifi_ip_text,sizeof(wifi_ip_text),IPSTR,IP2STR(&ip.ip));
                wifi_connected=true;
                std::snprintf(store_message,sizeof(store_message),"WIFI VERIFIED");
                start_time_sync();
                return true;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    esp_wifi_disconnect();
    wifi_connected=false;
    std::snprintf(wifi_ip_text,sizeof(wifi_ip_text),"0.0.0.0");
    std::snprintf(store_message,sizeof(store_message),"NO IP - CHECK PASSWORD / ROUTER");
    return false;
}
void wifi_connect_task(void*) {
    connect_wifi_credentials(wifi_task_ssid,wifi_task_password);
    wifi_connecting=false;
    ui_dirty=true;
    vTaskDelete(nullptr);
}
bool start_wifi_connect(const char* ssid,const char* password) {
    if(wifi_connecting)return false;
    std::snprintf(wifi_task_ssid,sizeof(wifi_task_ssid),"%.32s",ssid?ssid:"");
    std::snprintf(wifi_task_password,sizeof(wifi_task_password),"%.64s",password?password:"");
    wifi_connecting=true;
    std::snprintf(store_message,sizeof(store_message),"CONNECTING TO WIFI");
    if(xTaskCreate(wifi_connect_task,"wifi-connect",4096,nullptr,4,nullptr)!=pdPASS){wifi_connecting=false;std::snprintf(store_message,sizeof(store_message),"WIFI TASK FAILED");return false;}
    return true;
}
bool connect_wifi_from_sd() {
    if(wifi_driver_ready||wifi_started){
        // A valid DHCP address is the reliable sign that the station is online.
        // Do not require a separate AP-info query to succeed first.
        esp_netif_ip_info_t ip{};
        if(wifi_sta_netif&&esp_netif_get_ip_info(wifi_sta_netif,&ip)==ESP_OK&&ip.ip.addr!=0){
            std::snprintf(wifi_ip_text,sizeof(wifi_ip_text),IPSTR,IP2STR(&ip.ip));
            wifi_connected=true;wifi_started=true;start_time_sync();return true;
        }
        wifi_ap_record_t current{};
        if(esp_wifi_sta_get_ap_info(&current)==ESP_OK){
            for(int wait=0;wait<10;wait++){
                vTaskDelay(pdMS_TO_TICKS(250));
                if(wifi_sta_netif&&esp_netif_get_ip_info(wifi_sta_netif,&ip)==ESP_OK&&ip.ip.addr!=0){
                    std::snprintf(wifi_ip_text,sizeof(wifi_ip_text),IPSTR,IP2STR(&ip.ip));
                    wifi_connected=true;wifi_started=true;start_time_sync();return true;
                }
            }
        }
        wifi_connected=false;
    }
    FILE* f=std::fopen("/sdcard/MCAI-OS/wifi.cfg","r");
    if(!f){std::snprintf(store_message,sizeof(store_message),"CHOOSE WIFI IN SETTINGS");return false;}
    char ssid[33]={},password[65]={};
    bool ok=std::fgets(ssid,sizeof(ssid),f)&&std::fgets(password,sizeof(password),f);std::fclose(f);
    auto trim=[](char* s){size_t n=std::strlen(s);while(n&&(s[n-1]=='\n'||s[n-1]=='\r'))s[--n]=0;};
    trim(ssid);trim(password);
    if(!ok||!ssid[0]){std::snprintf(store_message,sizeof(store_message),"INVALID WIFI SETTINGS");return false;}
    return connect_wifi_credentials(ssid,password);
}
void ota_update_task(void*) {
    std::snprintf(ota_message,sizeof(ota_message),"CHECKING WI-FI...");
    ui_dirty=true;
    if(!connect_wifi_from_sd()) {
        std::snprintf(ota_message,sizeof(ota_message),"WI-FI FAILED - SETTINGS");
        ota_running=false;ui_dirty=true;vTaskDelete(nullptr);return;
    }
    start_time_sync();
    for(int wait=0;wait<20&&std::time(nullptr)<1577836800;wait++)vTaskDelay(pdMS_TO_TICKS(500));
    if(std::time(nullptr)<1577836800){
        std::snprintf(ota_message,sizeof(ota_message),"TIME NOT SYNCED - RETRY");
        ota_running=false;ui_dirty=true;vTaskDelete(nullptr);return;
    }
    std::snprintf(ota_message,sizeof(ota_message),"DOWNLOADING UPDATE...");
    ui_dirty=true;
    esp_http_client_config_t http_cfg={};
    http_cfg.url="https://github.com/betabeeswax/MCAI-OS/releases/download/v0.1.0-alpha/cmair-os-cardputer-adv-app.bin";
    http_cfg.crt_bundle_attach=esp_crt_bundle_attach;
    http_cfg.timeout_ms=45000;
    esp_https_ota_config_t ota_cfg={};
    ota_cfg.http_config=&http_cfg;
    const esp_err_t err=esp_https_ota(&ota_cfg);
    if(err==ESP_OK) {
        std::snprintf(ota_message,sizeof(ota_message),"UPDATE READY - RESTARTING");
        ui_dirty=true;
        vTaskDelay(pdMS_TO_TICKS(700));
        esp_restart();
    }
    std::snprintf(ota_message,sizeof(ota_message),"UPDATE FAILED: %.24s",esp_err_to_name(err));
    ota_running=false;ui_dirty=true;
    ESP_LOGE(kTag,"OTA update failed: %s",esp_err_to_name(err));
    vTaskDelete(nullptr);
}
void start_ota_update() {
    if(ota_running)return;
    ota_running=true;
    std::snprintf(ota_message,sizeof(ota_message),"STARTING UPDATE...");
    ui_dirty=true;
    if(xTaskCreate(ota_update_task,"mcai_ota",8192,nullptr,5,nullptr)!=pdPASS) {
        ota_running=false;
        std::snprintf(ota_message,sizeof(ota_message),"UPDATE TASK FAILED");
        ui_dirty=true;
    }
}

bool load_store_catalogue() {
    store_count=0;store_selected=0;store_scroll=0;catalogue_loaded=false;catalogue_length=0;catalogue_buffer[0]=0;
    for(int wait=0;wifi_connecting&&wait<120;wait++)vTaskDelay(pdMS_TO_TICKS(250));
    if(wifi_connecting){std::snprintf(store_message,sizeof(store_message),"WIFI STILL CONNECTING");return false;}
    if(!connect_wifi_from_sd())return false;
    // HTTPS certificate validation needs a real clock. Wi-Fi can have an IP
    // while NTP is still syncing, so wait before contacting GitHub.
    start_time_sync();
    for(int wait=0;wait<24&&std::time(nullptr)<1577836800;wait++)
        vTaskDelay(pdMS_TO_TICKS(500));
    if(std::time(nullptr)<1577836800){
        std::snprintf(store_message,sizeof(store_message),"TIME NOT SYNCED - RETRY");return false;
    }
    esp_http_client_config_t cfg={};
    cfg.url="https://raw.githubusercontent.com/betabeeswax/MCAI-OS-APPS/main/catalogue.json";
    cfg.event_handler=catalogue_http_event;cfg.timeout_ms=30000;cfg.buffer_size=4096;
    cfg.crt_bundle_attach=esp_crt_bundle_attach;cfg.keep_alive_enable=false;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client){std::snprintf(store_message,sizeof(store_message),"HTTP INIT FAILED");return false;}
    esp_err_t result=esp_http_client_perform(client);
    int status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
    if(result!=ESP_OK){std::snprintf(store_message,sizeof(store_message),"HTTPS FAIL: %.20s",esp_err_to_name(result));return false;}
    if(status!=200){std::snprintf(store_message,sizeof(store_message),"GITHUB HTTP %d",status);return false;}
    if(catalogue_length==0){std::snprintf(store_message,sizeof(store_message),"EMPTY CATALOGUE RESPONSE");return false;}
    if(catalogue_length>=sizeof(catalogue_buffer)-1){std::snprintf(store_message,sizeof(store_message),"CATALOGUE TOO LARGE");return false;}
    cJSON* root=cJSON_Parse(catalogue_buffer);
    cJSON* apps=root?cJSON_GetObjectItemCaseSensitive(root,"apps"):nullptr;
    if(!cJSON_IsArray(apps)){if(root)cJSON_Delete(root);std::snprintf(store_message,sizeof(store_message),"INVALID CATALOGUE JSON");return false;}
    cJSON* app=nullptr;
    cJSON_ArrayForEach(app,apps){
        if(store_count>=128)break;
        cJSON* name=cJSON_GetObjectItemCaseSensitive(app,"name");
        cJSON* id=cJSON_GetObjectItemCaseSensitive(app,"id");
        cJSON* status_item=cJSON_GetObjectItemCaseSensitive(app,"status");
        cJSON* package=cJSON_GetObjectItemCaseSensitive(app,"package");
        if(!cJSON_IsString(name)||!cJSON_IsString(id))continue;
        std::snprintf(store_names[store_count],64,"%s",name->valuestring);
        std::snprintf(store_ids[store_count],64,"%s",id->valuestring);
        std::snprintf(store_status[store_count],20,"%s",cJSON_IsString(status_item)?status_item->valuestring:"unknown");
        store_urls[store_count][0]=0;store_hashes[store_count][0]=0;store_sizes[store_count]=0;
        if(cJSON_IsObject(package)){
            cJSON* url=cJSON_GetObjectItemCaseSensitive(package,"url");
            cJSON* hash=cJSON_GetObjectItemCaseSensitive(package,"sha256");
            cJSON* format=cJSON_GetObjectItemCaseSensitive(package,"format");
            cJSON* size=cJSON_GetObjectItemCaseSensitive(package,"sizeBytes");
            if(cJSON_IsString(url)&&cJSON_IsString(hash)&&cJSON_IsString(format)&&
               std::strcmp(format->valuestring,"mcai-app-v1")==0&&cJSON_IsNumber(size)&&
               size->valuedouble>0&&size->valuedouble<=4096&&std::strncmp(url->valuestring,"https://",8)==0&&
               std::strlen(hash->valuestring)==64){
                std::snprintf(store_urls[store_count],256,"%s",url->valuestring);
                std::snprintf(store_hashes[store_count],65,"%s",hash->valuestring);
                store_sizes[store_count]=(size_t)size->valuedouble;
            }
        }
        ++store_count;
    }
    cJSON_Delete(root);catalogue_loaded=true;
    std::snprintf(store_message,sizeof(store_message),"%d APPS LOADED",store_count);
    return true;
}
void refresh_store_task(void*) {
    load_store_catalogue();store_loading=false;ui_dirty=true;vTaskDelete(nullptr);
}
void refresh_store() {
    if(store_loading)return;
    store_loading=true;store_count=0;store_selected=0;store_scroll=0;catalogue_loaded=false;
    std::snprintf(store_message,sizeof(store_message),"LOADING APP CATALOGUE");ui_dirty=true;
    if(xTaskCreate(refresh_store_task,"store-refresh",6144,nullptr,4,nullptr)!=pdPASS){
        store_loading=false;std::snprintf(store_message,sizeof(store_message),"STORE TASK FAILED");ui_dirty=true;
    }
}
bool package_path(char* out,size_t cap,const char* id) {
    if(!id||!id[0]||std::strstr(id,"..")||std::strchr(id,'/'))return false;
    std::snprintf(out,cap,"/sdcard/MCAI-OS/apps/%s.json",id);return true;
}
int store_builtin_index(const char* id) {
    if(std::strcmp(id,"file-manager")==0)return 0;
    if(std::strcmp(id,"notes")==0)return 1;
    if(std::strcmp(id,"calculator")==0)return 2;
    if(std::strcmp(id,"settings")==0)return 3;
    if(std::strcmp(id,"system-information")==0)return 4;
    if(std::strcmp(id,"app-store")==0)return 5;
    if(std::strcmp(id,"clock")==0)return 6;
    if(std::strcmp(id,"timer")==0)return 7;
    if(std::strcmp(id,"keyboard-test")==0)return 8;
    if(std::strcmp(id,"storage-manager")==0)return 9;
    if(std::strcmp(id,"diagnostics")==0)return 10;
    if(std::strcmp(id,"snake")==0)return 11;
    if(std::strcmp(id,"about")==0)return 12;
    if(std::strcmp(id,"stopwatch")==0)return 13;
    return -1;
}
bool package_installed(const char* id) {
    char path[160];if(!package_path(path,sizeof(path),id))return false;
    struct stat st{};return stat(path,&st)==0&&S_ISREG(st.st_mode);
}
bool launch_installed_package(const char* id) {
    char path[160];if(!sd_ready||!package_path(path,sizeof(path),id))return false;
    FILE* f=std::fopen(path,"r");if(!f)return false;
    char data[4096]={};size_t n=std::fread(data,1,sizeof(data)-1,f);std::fclose(f);
    if(n==0)return false;
    cJSON* root=cJSON_Parse(data);if(!root)return false;
    cJSON* fmt=cJSON_GetObjectItemCaseSensitive(root,"format");
    cJSON* pid=cJSON_GetObjectItemCaseSensitive(root,"id");
    cJSON* target=cJSON_GetObjectItemCaseSensitive(root,"target");
    cJSON* ui=cJSON_GetObjectItemCaseSensitive(root,"ui");
    cJSON* title=ui?cJSON_GetObjectItemCaseSensitive(ui,"title"):nullptr;
    cJSON* subtitle=ui?cJSON_GetObjectItemCaseSensitive(ui,"subtitle"):nullptr;
    cJSON* initial=ui?cJSON_GetObjectItemCaseSensitive(ui,"initial"):nullptr;
    cJSON* increment=ui?cJSON_GetObjectItemCaseSensitive(ui,"increment"):nullptr;
    bool valid=cJSON_IsString(fmt)&&std::strcmp(fmt->valuestring,"mcai-app-v1")==0&&
      cJSON_IsString(pid)&&std::strcmp(pid->valuestring,id)==0&&
      cJSON_IsString(target)&&std::strcmp(target->valuestring,"cardputer-adv")==0&&
      cJSON_IsObject(ui)&&cJSON_IsString(title)&&cJSON_IsString(subtitle)&&
      cJSON_IsNumber(initial)&&cJSON_IsNumber(increment)&&
      initial->valuedouble>=-999999&&initial->valuedouble<=999999&&
      increment->valuedouble>=1&&increment->valuedouble<=1000;
    if(valid){
      std::snprintf(package_id,sizeof(package_id),"%s",id);
      std::snprintf(package_title,sizeof(package_title),"%s",title->valuestring);
      std::snprintf(package_subtitle,sizeof(package_subtitle),"%s",subtitle->valuestring);
      package_counter=(int)initial->valuedouble;
      package_increment=(int)increment->valuedouble;
      package_running=true;active_app=17;
    }
    cJSON_Delete(root);return valid;
}
void save_launcher_layout() {
    if(!sd_ready)return;
    mkdir("/sdcard/MCAI-OS",0775);
    FILE* f=std::fopen("/sdcard/MCAI-OS/launcher.cfg.tmp","w");
    if(!f)return;
    std::fprintf(f,"MCAI-LAUNCHER-1\n");
    for(int i=0;i<kAppCount;i++){
        const int id=launcher_order[i];
        std::fprintf(f,"%d %d\n",id,(id>=0&&id<kAppCount&&launcher_foldered[id])?1:0);
    }
    const bool ok=std::fclose(f)==0;
    if(ok)rename("/sdcard/MCAI-OS/launcher.cfg.tmp","/sdcard/MCAI-OS/launcher.cfg");
}
void load_launcher_layout() {
    for(int i=0;i<kAppCount;i++){launcher_order[i]=i;launcher_foldered[i]=false;}
    if(!sd_ready)return;
    FILE* f=std::fopen("/sdcard/MCAI-OS/launcher.cfg","r");
    if(!f)return;
    char header[32]={};std::fgets(header,sizeof(header),f);
    if(std::strcmp(header,"MCAI-LAUNCHER-1\n")!=0){std::fclose(f);return;}
    bool seen[kAppCount]={};
    for(int i=0;i<kAppCount;i++){
        int id=-1,folder=0;
        if(std::fscanf(f,"%d %d",&id,&folder)!=2||id<0||id>=kAppCount||seen[id])break;
        launcher_order[i]=id;launcher_foldered[id]=folder!=0;seen[id]=true;
    }
    std::fclose(f);
}
int launcher_folder_count() {
    int n=0;for(int i=0;i<kAppCount;i++)if(launcher_foldered[i])++n;return n;
}
int launcher_root_count() {
    int n=installed_package_count+(launcher_folder_count()>0?1:0);
    for(int i=0;i<kAppCount;i++)if(!launcher_foldered[launcher_order[i]])++n;
    return n;
}
int launcher_root_item(int position) {
    int n=0;
    for(int i=0;i<kAppCount;i++){const int id=launcher_order[i];if(launcher_foldered[id])continue;if(n++==position)return id;}
    if(launcher_folder_count()>0){if(n++==position)return -1;}
    const int p=position-n;
    if(p>=0&&p<installed_package_count)return kAppCount+p;
    return -2;
}
int launcher_folder_item(int position) {
    int n=0;for(int i=0;i<kAppCount;i++){const int id=launcher_order[i];if(launcher_foldered[id]&&n++==position)return id;}return -2;
}
const char* launcher_item_name(int item) {
    if(item==-1)return "MY FOLDER";
    if(item>=0&&item<kAppCount)return kApps[item];
    const int p=item-kAppCount;
    if(p>=0&&p<installed_package_count)return installed_package_titles[p];
    return "UNKNOWN APP";
}
void refresh_installed_packages() {
    installed_package_count=0;
    if(!sd_ready)return;
    mkdir("/sdcard/MCAI-OS",0775);mkdir("/sdcard/MCAI-OS/apps",0775);
    DIR* d=opendir("/sdcard/MCAI-OS/apps");if(!d)return;
    struct dirent* e;
    while((e=readdir(d))!=nullptr&&installed_package_count<16){
        const size_t n=std::strlen(e->d_name);
        if(n<6||std::strcmp(e->d_name+n-5,".json")!=0)continue;
        char path[160];std::snprintf(path,sizeof(path),"/sdcard/MCAI-OS/apps/%.63s",e->d_name);
        FILE* f=std::fopen(path,"r");if(!f)continue;
        char data[4096]={};const size_t bytes=std::fread(data,1,sizeof(data)-1,f);std::fclose(f);if(!bytes)continue;
        cJSON* root=cJSON_Parse(data);if(!root)continue;
        cJSON* id=cJSON_GetObjectItemCaseSensitive(root,"id");
        cJSON* ui=cJSON_GetObjectItemCaseSensitive(root,"ui");
        cJSON* title=ui?cJSON_GetObjectItemCaseSensitive(ui,"title"):nullptr;
        cJSON* fmt=cJSON_GetObjectItemCaseSensitive(root,"format");
        if(cJSON_IsString(fmt)&&std::strcmp(fmt->valuestring,"mcai-app-v1")==0&&cJSON_IsString(id)&&cJSON_IsString(title)){
            bool duplicate=false;for(int i=0;i<installed_package_count;i++)if(std::strcmp(installed_package_ids[i],id->valuestring)==0)duplicate=true;
            if(!duplicate){std::snprintf(installed_package_ids[installed_package_count],64,"%s",id->valuestring);
                std::snprintf(installed_package_titles[installed_package_count],64,"%s",title->valuestring);++installed_package_count;}
        }
        cJSON_Delete(root);
    }
    closedir(d);
}
void refresh_music_files();
void start_launcher_item(int item) {
    if(item==-1){folder_open=true;folder_selected=0;return;}
    if(item>=kAppCount){
        const int p=item-kAppCount;
        if(p>=0&&p<installed_package_count){package_return_app=-1;if(!launch_installed_package(installed_package_ids[p]))
            std::snprintf(store_message,sizeof(store_message),"APP LOAD FAILED");}
        return;
    }
    if(item<0||item>=kAppCount)return;
    active_app=item;
    if(item==0){std::snprintf(current_dir,sizeof(current_dir),"/sdcard");refresh_files();}
    if(item==1){note_editing=false;refresh_notes();}
    if(item==5){refresh_store();}
    if(item==6)last_clock_render_second=-1;
    if(item==15){wifi_test_done=false;}
    if(item==16)refresh_music_files();
}
void install_store_package() {
    if(!catalogue_loaded||store_count==0)return;
    if((std::strcmp(store_status[store_selected],"available")!=0&&std::strcmp(store_status[store_selected],"experimental")!=0)||
       !store_urls[store_selected][0]||std::strlen(store_hashes[store_selected])!=64||store_sizes[store_selected]==0){
      std::snprintf(store_message,sizeof(store_message),"NO VERIFIED PACKAGE");return;
    }
    if(!sd_ready){std::snprintf(store_message,sizeof(store_message),"INSERT SD CARD");return;}
    if(package_installed(store_ids[store_selected])){std::snprintf(store_message,sizeof(store_message),"INSTALLED - ENTER TO OPEN");return;}
    catalogue_length=0;catalogue_buffer[0]=0;
    esp_http_client_config_t cfg={};cfg.url=store_urls[store_selected];cfg.event_handler=catalogue_http_event;
    cfg.timeout_ms=20000;cfg.buffer_size=1024;cfg.crt_bundle_attach=esp_crt_bundle_attach;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client){std::snprintf(store_message,sizeof(store_message),"PACKAGE HTTP INIT FAIL");return;}
    esp_err_t result=esp_http_client_perform(client);int status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
    if(result!=ESP_OK||status!=200||catalogue_length==0||catalogue_length>4096||catalogue_length!=store_sizes[store_selected]){
      std::snprintf(store_message,sizeof(store_message),"PACKAGE DOWNLOAD FAIL");return;
    }
    unsigned char digest[32];
    if(mbedtls_sha256((const unsigned char*)catalogue_buffer,catalogue_length,digest,0)!=0){
      std::snprintf(store_message,sizeof(store_message),"CHECKSUM ERROR");return;
    }
    char hex[65];for(int i=0;i<32;i++)std::snprintf(hex+i*2,3,"%02x",digest[i]);hex[64]=0;
    if(std::strcmp(hex,store_hashes[store_selected])!=0){std::snprintf(store_message,sizeof(store_message),"CHECKSUM MISMATCH");return;}
    cJSON* root=cJSON_Parse(catalogue_buffer);
    cJSON* fmt=root?cJSON_GetObjectItemCaseSensitive(root,"format"):nullptr;
    cJSON* id=root?cJSON_GetObjectItemCaseSensitive(root,"id"):nullptr;
    cJSON* target=root?cJSON_GetObjectItemCaseSensitive(root,"target"):nullptr;
    bool valid=cJSON_IsString(fmt)&&std::strcmp(fmt->valuestring,"mcai-app-v1")==0&&
      cJSON_IsString(id)&&std::strcmp(id->valuestring,store_ids[store_selected])==0&&
      cJSON_IsString(target)&&std::strcmp(target->valuestring,"cardputer-adv")==0;
    if(root)cJSON_Delete(root);
    if(!valid){std::snprintf(store_message,sizeof(store_message),"INVALID APP PACKAGE");return;}
    mkdir("/sdcard/MCAI-OS",0775);mkdir("/sdcard/MCAI-OS/apps",0775);
    char path[160];if(!package_path(path,sizeof(path),store_ids[store_selected])){std::snprintf(store_message,sizeof(store_message),"BAD APP ID");return;}
    FILE* f=std::fopen(path,"w");if(!f){std::snprintf(store_message,sizeof(store_message),"SD WRITE FAILED");return;}
    size_t wrote=std::fwrite(catalogue_buffer,1,catalogue_length,f);bool saved=std::fclose(f)==0&&wrote==catalogue_length;
    if(!saved){unlink(path);std::snprintf(store_message,sizeof(store_message),"APP SAVE FAILED");return;}
    std::snprintf(store_message,sizeof(store_message),"INSTALLED - ENTER TO OPEN");
}
void seed_store() { /* Store entries are fetched from the official GitHub catalogue. */ }
bool wifi_http_probe(const char* url,int& status,esp_err_t& result) {
    esp_http_client_config_t cfg={};
    cfg.url=url;cfg.timeout_ms=12000;cfg.crt_bundle_attach=esp_crt_bundle_attach;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client){result=ESP_FAIL;status=0;return false;}
    result=esp_http_client_perform(client);
    status=esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    return result==ESP_OK&&(status==200||status==204);
}
void wifi_test_task(void*) {
    wifi_test_done=false;wifi_test_online=false;wifi_test_http=0;
    std::snprintf(wifi_test_error,sizeof(wifi_test_error),"CHECKING");
    if(!connect_wifi_from_sd()) {
        std::snprintf(wifi_test_error,sizeof(wifi_test_error),"NO IP / WIFI SETTINGS");
        wifi_test_running=false;wifi_test_done=true;ui_dirty=true;vTaskDelete(nullptr);return;
    }
    wifi_ap_record_t ap{};if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)wifi_test_rssi=ap.rssi;
    std::snprintf(wifi_test_ip,sizeof(wifi_test_ip),"%s",wifi_ip_text);
    start_time_sync();
    for(int wait=0;wait<20&&std::time(nullptr)<1577836800;wait++)vTaskDelay(pdMS_TO_TICKS(500));
    esp_err_t result=ESP_FAIL;int status=0;
    wifi_test_online=wifi_http_probe("https://connectivitycheck.gstatic.com/generate_204",status,result);
    wifi_test_http=status;
    if(!wifi_test_online){
        int fallback_status=0;esp_err_t fallback_result=ESP_FAIL;
        wifi_test_online=wifi_http_probe("https://www.google.com/generate_204",fallback_status,fallback_result);
        if(wifi_test_online){wifi_test_http=fallback_status;result=fallback_result;}
        else if(fallback_status){wifi_test_http=fallback_status;result=fallback_result;}
    }
    if(wifi_test_online)std::snprintf(wifi_test_error,sizeof(wifi_test_error),"HTTPS OK");
    else if(wifi_test_http)std::snprintf(wifi_test_error,sizeof(wifi_test_error),"HTTP %d",wifi_test_http);
    else std::snprintf(wifi_test_error,sizeof(wifi_test_error),"%.25s",esp_err_to_name(result));
    wifi_test_running=false;wifi_test_done=true;ui_dirty=true;vTaskDelete(nullptr);
}
void start_wifi_test() {
    if(wifi_test_running) return;
    wifi_test_running=true;
    wifi_test_done=false;
    wifi_test_online=false;
    std::snprintf(wifi_test_error,sizeof(wifi_test_error),"CHECKING");
    if(xTaskCreate(wifi_test_task,"wifi-test",6144,nullptr,4,nullptr)!=pdPASS){wifi_test_running=false;wifi_test_done=true;std::snprintf(store_message,sizeof(store_message),"WIFI TEST TASK FAILED");}
    ui_dirty=true;
}
bool music_name_is_mp3(const char* name) {
    if(!name)return false;
    const size_t n=std::strlen(name);
    if(n<4)return false;
    char ext[5]={};
    for(int i=0;i<4;i++)ext[i]=(char)std::tolower((unsigned char)name[n-4+i]);
    return std::strcmp(ext,".mp3")==0;
}
void refresh_music_files() {
    music_count=0;music_selected=0;music_scroll=0;
    if(!sd_ready){std::snprintf(store_message,sizeof(store_message),"INSERT SD CARD");return;}
    mkdir("/sdcard/MCAI-OS",0775);mkdir("/sdcard/MCAI-OS/music",0775);
    DIR* d=opendir("/sdcard/MCAI-OS/music");
    if(!d){std::snprintf(store_message,sizeof(store_message),"CANNOT OPEN MUSIC FOLDER");return;}
    struct dirent* e;
    while((e=readdir(d))&&music_count<16){
        size_t n=std::strlen(e->d_name);if(n<4)continue;
        const char* ext=e->d_name+n-4;
        if(std::strcmp(ext,".wav")&&std::strcmp(ext,".WAV")&&!music_name_is_mp3(e->d_name))continue;
        std::snprintf(music_files[music_count++],64,"%.63s",e->d_name);
    }
    closedir(d);
}
void music_play_mp3(FILE* f) {
    mp3dec_init(&music_mp3_decoder);
    size_t input_len=0;
    bool eof=false,decoded=false;
    while(!music_stop_requested){
        if(!eof&&input_len<sizeof(music_mp3_input)){
            const size_t got=std::fread(music_mp3_input+input_len,1,sizeof(music_mp3_input)-input_len,f);
            input_len+=got;
            if(got==0)eof=true;
        }
        int consumed=0;
        while(!music_stop_requested&&input_len-(size_t)consumed>=4){
            mp3dec_frame_info_t info{};
            const int samples=mp3dec_decode_frame(&music_mp3_decoder,
                music_mp3_input+consumed,(int)(input_len-(size_t)consumed),
                music_mp3_pcm,&info);
            if(info.frame_bytes<=0)break;
            consumed+=info.frame_bytes;
            if(samples<=0||info.hz<=0||(info.channels!=1&&info.channels!=2))continue;
            decoded=true;
            const int output_frames=std::min(4096/2,(samples*44100+info.hz-1)/info.hz);
            for(int base=0;base<output_frames&&!music_stop_requested;base+=256){
                const int count=std::min(256,output_frames-base);
                for(int i=0;i<count;i++){
                    const int source=std::min(samples-1,((base+i)*info.hz)/44100);
                    const int left=music_mp3_pcm[source*info.channels];
                    const int right=info.channels==2?music_mp3_pcm[source*2+1]:left;
                    music_mp3_output[i*2]=(std::int16_t)left;
                    music_mp3_output[i*2+1]=(std::int16_t)right;
                }
                size_t written=0;
                if(i2s_write(I2S_NUM_0,music_mp3_output,(size_t)count*4,&written,pdMS_TO_TICKS(500))!=ESP_OK)break;
            }
        }
        if(consumed>0){
            input_len-=(size_t)consumed;
            if(input_len)std::memmove(music_mp3_input,music_mp3_input+consumed,input_len);
        }
        if(eof){
            if(consumed==0||input_len<4)break;
        }else if(input_len==sizeof(music_mp3_input)&&consumed==0){
            // Recover from corrupt data without letting a bad file stall playback.
            std::memmove(music_mp3_input,music_mp3_input+1,--input_len);
        }
    }
    if(!decoded&&!music_stop_requested)std::snprintf(store_message,sizeof(store_message),"INVALID OR UNSUPPORTED MP3");
}
void music_play_wav(FILE* f) {
    bool valid=false;
    unsigned char h[12]={};
    if(std::fread(h,1,12,f)==12&&!std::memcmp(h,"RIFF",4)&&!std::memcmp(h+8,"WAVE",4)){
        unsigned short format=0,channels=0,bits=0;unsigned int rate=0;unsigned char ch[8];
        while(std::fread(ch,1,8,f)==8){
            unsigned int size=(unsigned int)ch[4]|((unsigned int)ch[5]<<8)|((unsigned int)ch[6]<<16)|((unsigned int)ch[7]<<24);
            if(!std::memcmp(ch,"fmt ",4)&&size>=16){
                unsigned char fmt[16];if(std::fread(fmt,1,16,f)!=16)break;
                format=(unsigned short)(fmt[0]|(fmt[1]<<8));channels=(unsigned short)(fmt[2]|(fmt[3]<<8));
                rate=(unsigned int)fmt[4]|((unsigned int)fmt[5]<<8)|((unsigned int)fmt[6]<<16)|((unsigned int)fmt[7]<<24);
                bits=(unsigned short)(fmt[14]|(fmt[15]<<8));if(size>16)fseek(f,(long)(size-16),SEEK_CUR);
            }else if(!std::memcmp(ch,"data",4)){
                valid=format==1&&channels==2&&rate==44100&&bits==16;
                if(valid){
                    unsigned char buf[1024];size_t n;
                    while(!music_stop_requested&&(n=std::fread(buf,1,sizeof(buf),f))>0){
                        size_t wrote=0;if(i2s_write(I2S_NUM_0,buf,n,&wrote,pdMS_TO_TICKS(250))!=ESP_OK)break;
                    }
                }
                break;
            }else {fseek(f,(long)size,SEEK_CUR);if(size&1)fseek(f,1,SEEK_CUR);}
        }
    }
    if(!valid)std::snprintf(store_message,sizeof(store_message),"NEED 44.1KHZ 16-BIT STEREO WAV");
}
void music_play_task(void*) {
    char path[128]={};
    if(music_selected>=0&&music_selected<music_count)
        std::snprintf(path,sizeof(path),"/sdcard/MCAI-OS/music/%.63s",music_files[music_selected]);
    FILE* f=path[0]?std::fopen(path,"rb"):nullptr;
    if(!f)std::snprintf(store_message,sizeof(store_message),"CANNOT OPEN MUSIC FILE");
    else {
        if(music_name_is_mp3(path))music_play_mp3(f);
        else music_play_wav(f);
        std::fclose(f);
    }
    music_playing=false;music_stop_requested=false;ui_dirty=true;vTaskDelete(nullptr);
}
void start_music_playback() {
    if(music_playing){music_stop_requested=true;return;}
    if(!audio_ready){std::snprintf(store_message,sizeof(store_message),"AUDIO NOT READY");return;}
    if(music_selected<0||music_selected>=music_count)return;
    music_stop_requested=false;music_playing=true;
    if(xTaskCreate(music_play_task,"music-player",8192,nullptr,4,nullptr)!=pdPASS){music_playing=false;std::snprintf(store_message,sizeof(store_message),"MUSIC TASK FAILED");}
    ui_dirty=true;
}
void open_selected_file() {
    if(file_count==0)return;
    char path[240];std::snprintf(path,sizeof(path),"%s/%s",current_dir,file_names[file_selected]);
    if(file_is_dir[file_selected]){std::snprintf(current_dir,sizeof(current_dir),"%.159s",path);file_scroll=0;refresh_files();return;}
    FILE* f=std::fopen(path,"r");if(!f)return;
    const size_t n=std::fread(viewer_buffer,1,sizeof(viewer_buffer)-1,f);viewer_buffer[n]=0;std::fclose(f);viewing_file=true;file_scroll=0;
}
int wrapped_line_count(const char* text,int width) {
    int lines=1,col=0;
    if(!text)return 0;
    for(const char* p=text;*p;++p){
        if(*p=='\n'){++lines;col=0;}
        else {if(col==width){++lines;col=0;}++col;}
    }
    return lines;
}
void render_text_page(const char* text,int scroll,int visible,int x,int y,std::uint16_t color) {
    if(!text)return;
    int line=0,n=0;char row[37]={};
    for(const char* p=text;;++p){
        const bool end=(*p==0);const char c=end?0:*p;
        if(end||c=='\n'||n==36){
            if(line>=scroll&&line<scroll+visible)draw_text(row,x,y+(line-scroll)*13,1,color);
            ++line;n=0;row[0]=0;
            if(end)break;
            if(c=='\n')continue;
        }
        row[n++]=c;row[n]=0;
    }
}
int note_visual_line_count() {
    int lines=1,col=0;
    for(int i=0;i<note_length;i++) {
        if(note_buffer[i]=='\n'){++lines;col=0;}
        else {if(col==35){++lines;col=0;}++col;}
    }
    return lines;
}
void render_note_page(int x,int y,int visible,std::uint16_t color) {
    int line=0,n=0;
    char row[36]={};
    for(int i=0;i<=note_length;i++) {
        const bool end=(i==note_length);
        const char c=end?0:note_buffer[i];
        if(end||c=='\n'||n==35) {
            if(line>=note_scroll&&line<note_scroll+visible)
                draw_text(row,x,y+(line-note_scroll)*12,1,color);
            ++line;n=0;row[0]=0;
            if(end)break;
            if(c=='\n')continue;
        }
        row[n++]=c;row[n]=0;
    }
}
void render_app() {
    const auto white=dark_theme?rgb565(255,255,255):rgb565(0,0,0);
    const auto muted=dark_theme?rgb565(170,170,170):rgb565(75,75,75);
    const auto accent=dark_theme?rgb565(255,255,255):rgb565(0,0,0);
    const auto bg=dark_theme?rgb565(0,0,0):rgb565(255,255,255);
    const auto ink=white;
    std::fill(std::begin(framebuffer),std::end(framebuffer),bg);
    fill_rect(0,0,kWidth,22,rgb565(0,0,0));
    fill_round_rect(7,4,14,14,4,accent);
    draw_text("M",11,8,1,rgb565(0,0,0));
    if(active_app==17)draw_text(package_title,28,7,1,ink);
    else if(active_app>=0&&active_app<kAppCount)draw_text(kApps[active_app],28,7,1,ink);
    else draw_text("MCAI APP",28,7,1,ink);
    if(caps_lock)draw_text("CAP",174,7,1,accent);
    if(shift_held)draw_text("SHF",194,7,1,accent);
    if(fn_lock)draw_text("FN",218,7,1,accent);
    fill_rect(0,22,kWidth,1,dark_theme?rgb565(90,90,90):rgb565(130,130,130));
    char line[64];
    switch(active_app) {
    case 0:
        if(viewing_file){
            draw_text("FILE VIEWER",8,27,1,accent);
            const int viewer_lines=wrapped_line_count(viewer_buffer,36);
            render_text_page(viewer_buffer,file_scroll,5,8,43,ink);
            char page[32];std::snprintf(page,sizeof(page),"LINE %d/%d",std::min(file_scroll+1,std::max(1,viewer_lines)),std::max(1,viewer_lines));
            draw_text(page,8,112,1,muted);draw_text("UP/DN SCROLL  BKSP BACK",8,124,1,muted);
        } else {
            const char* tail=std::strlen(current_dir)>36?current_dir+std::strlen(current_dir)-36:current_dir;
            draw_text(tail,8,25,1,accent);
            for(int i=0;i<5;i++){int ix=file_scroll+i;if(ix>=file_count)break;std::snprintf(line,sizeof(line),"%c %.32s",file_is_dir[ix]?'>':' ',file_names[ix]);draw_text(line,8,42+i*14,1,ix==file_selected?accent:ink);}
            draw_text("UP/DN MOVE  ENTER OPEN",8,112,1,muted);draw_text("BKSP BACK",8,124,1,muted);
        }
        break;
    case 1:
        if(!note_editing){
            draw_text("MY NOTES",8,25,1,accent);
            fill_round_rect(151,22,81,17,4,notes_action==1?accent:muted);
            draw_text("+ ADD NOTE",158,27,1,notes_action==1?bg:ink);
            fill_rect(8,43,224,1,rgb565(80,80,80));
            for(int i=0;i<5;i++){
                const int ix=note_scroll+i;
                if(ix>=note_count)break;
                fill_round_rect(8,47+i*12,224,11,2,ix==note_selected&&notes_action==0?(dark_theme?rgb565(64,64,64):rgb565(205,205,205)):bg);
                draw_text(note_files[ix],14,49+i*12,1,ix==note_selected&&notes_action==0?accent:ink);
            }
            if(note_count==0)draw_text("NO SAVED NOTES YET",14,63,1,muted);
            char note_count_label[24];std::snprintf(note_count_label,sizeof(note_count_label),"%d NOTES",note_count);draw_text(note_count_label,8,111,1,muted);
            draw_text("UP/DN MOVE  ENTER OPEN",104,111,1,muted);
            draw_text("N: NEW  BKSP: BACK",8,124,1,muted);
        } else {
            const char* note_title=std::strrchr(note_filename,'/');
            draw_text(note_title?note_title+1:note_filename,8,25,1,accent);
            render_note_page(8,40,6,ink);
            draw_text("TYPE EDIT  UP/DOWN SCROLL",8,112,1,muted);
            draw_text("FN+ENTER SAVE & EXIT  BKSP DEL",8,124,1,muted);
        }
        break;
    case 2:
        draw_text("CALCULATOR",8,27,1,accent);draw_text(calc_buffer,8,49,2,ink);
        if(calc_error)draw_text(calc_error_text,8,70,1,rgb565(190,190,190));
        draw_text("P +   M -   X *   D /",8,87,1,muted);draw_text("DIGITS / BRACKETS / DECIMAL",8,101,1,muted);draw_text("ENTER = RESULT  BKSP = DEL",8,116,1,muted);break;
    case 3:
        if(bluetooth_screen){
            draw_text("BLUETOOTH LE",8,25,1,accent);
            draw_text(ble_complex_mode?"COMPLEX":"SIMPLE",174,25,1,muted);
            for(int i=0;i<5;i++){int ix=ble_device_scroll+i;if(ix>=ble_device_count)break;char row[40];
                if(ble_complex_mode)std::snprintf(row,sizeof(row),"%.17s %s",ble_device_named[ix]?ble_devices[ix]:ble_addresses[ix],ble_rssi[ix]);
                else std::snprintf(row,sizeof(row),"%.24s",ble_devices[ix]);
                draw_text(row,8,43+i*13,1,ix==ble_device_scroll?accent:ink);}
            if(ble_device_count==0)draw_text(ble_scan_active?"LOOKING FOR DEVICES":"NO NAMED DEVICES",8,58,1,ink);
            draw_text(store_message,8,108,1,muted);
            draw_text("M MODE  ENTER RESCAN  ESC BACK",8,124,1,muted);
        } else if(wifi_setup_step==1||wifi_setup_step==2||wifi_setup_step==3||wifi_setup_step==4){
            if(wifi_setup_step==3){
                draw_text("CHOOSE WIFI NETWORK",8,28,1,accent);
                for(int i=0;i<5;i++){int ix=wifi_network_scroll+i;if(ix>=wifi_network_count)break;char row[40];std::snprintf(row,sizeof(row),"%c %.20s %ddB",ix==wifi_network_selected?'>':' ',wifi_networks[ix],wifi_network_rssi[ix]);draw_text(row,8,43+i*12,1,ix==wifi_network_selected?accent:ink);}
                if(wifi_network_count==0)draw_text("NO NETWORKS - PRESS R",8,60,1,ink);
                draw_text(store_message,8,101,1,muted);
                draw_text("UP/DN ENTER  R RESCAN",8,114,1,muted);draw_text("M MANUAL  ESC BACK",8,126,1,muted);
            } else {
                const bool entering_ssid=(wifi_setup_step==1);
                draw_text(entering_ssid?"WIFI NETWORK NAME":"WIFI PASSWORD",8,29,1,accent);
                if(entering_ssid)draw_text_tail(wifi_ssid_input,8,52,36,1,ink);
                else draw_text_tail(wifi_password_input,8,52,36,1,ink);
                draw_text("TYPE USING KEYBOARD",8,78,1,muted);
                draw_text(entering_ssid?"ENTER: NEXT":"PASSWORD SHOWN",8,98,1,muted);
                draw_text("ESC: CANCEL  BKSP: DELETE",8,116,1,muted);
            }
        } else {
            draw_text("SETTINGS",8,25,1,accent);
            draw_text(wifi_connected?"WI-FI CONNECTED":(wifi_connecting?"WI-FI CONNECTING":"WI-FI OFFLINE"),8,39,1,wifi_connected?accent:(wifi_connecting?accent:muted));
            if(wifi_connected)draw_text(wifi_ip_text,8,49,1,accent);
            const char* rows[]={"BRIGHTNESS","THEME","WI-FI NETWORKS","BLUETOOTH DEVICES","SOUND"};
            for(int i=0;i<5;i++){
                char item[40];
                if(i==0)std::snprintf(item,sizeof(item),"BRIGHTNESS  %s",setting_brightness==0?"LOW":setting_brightness==1?"MEDIUM":"HIGH");
                else if(i==1)std::snprintf(item,sizeof(item),"THEME       %s",dark_theme?"DARK":"LIGHT");
                else if(i==4)std::snprintf(item,sizeof(item),"SOUND       %s",sound_enabled?"ON":"OFF");
                else std::snprintf(item,sizeof(item),"%s",rows[i]);
                fill_rect(6,59+i*11,228,10,i==settings_selection?rgb565(70,70,70):bg);
                draw_text(item,12,60+i*11,1,i==settings_selection?accent:ink);
            }
            draw_text("UP/DN SELECT  LEFT/RIGHT CHANGE",8,117,1,muted);
            draw_text("ENTER OPEN  ESC BACK",8,128,1,muted);
        }
        break;
    case 4:
        std::snprintf(line,sizeof(line),"CPU ESP32-S3");draw_text(line,8,30,1,ink);
        std::snprintf(line,sizeof(line),"UPTIME %lu SEC",(unsigned long)(esp_timer_get_time()/1000000));draw_text(line,8,48,1,ink);
        std::snprintf(line,sizeof(line),"HEAP %lu KB",(unsigned long)(esp_get_free_heap_size()/1024));draw_text(line,8,66,1,ink);
        draw_text("M5STACK CARDPUTER ADV",8,84,1,accent);break;
    case 5:
        draw_text("GITHUB APP STORE",8,25,1,accent);
        draw_text(catalogue_loaded?"MCAI-OS-APPS / MAIN":"GITHUB CATALOGUE",8,39,1,ink);
        if(store_loading){draw_text("CONNECTING / DOWNLOADING...",8,70,1,accent);}
        else if(!catalogue_loaded&&store_count==0){draw_text("CATALOGUE NOT LOADED",8,65,1,accent);draw_text("SEE ERROR BELOW",8,80,1,ink);}
        for(int i=0;i<4;i++){int ix=store_scroll+i;if(ix>=store_count)break;char row[40];std::snprintf(row,sizeof(row),"%c %.20s %s",ix==store_selected?'>':' ',store_names[ix],store_builtin_index(store_ids[ix])>=0?"BUILT-IN":(package_installed(store_ids[ix])?"INSTALLED":(std::strcmp(store_status[ix],"available")==0?"AVAILABLE":(std::strcmp(store_status[ix],"experimental")==0?"EXPERIMENTAL":"PLANNED"))));draw_text(row,8,54+i*13,1,ix==store_selected?accent:ink);}
        draw_text(store_message,8,110,1,muted);draw_text("UP/DOWN ENTER R RETRY BKSP BACK",8,122,1,muted);break;
    case 6: {
        std::time_t now=std::time(nullptr);std::tm tmv{};localtime_r(&now,&tmv);
        if(tmv.tm_year<120){draw_text("TIME NOT SYNCED",8,33,2,accent);draw_text("SET WIFI IN SETTINGS",8,67,1,ink);}
        else {std::snprintf(line,sizeof(line),"%02d:%02d:%02d",tmv.tm_hour,tmv.tm_min,tmv.tm_sec);draw_text("SYSTEM CLOCK",8,30,1,accent);draw_text(line,8,55,3,ink);}
        draw_text("ESC: BACK",8,111,1,muted);break;}
    case 15:
        draw_text("WI-FI CONNECTION TEST",8,29,1,accent);
        draw_text(wifi_test_running?"RUNNING TEST...":(wifi_test_done?(wifi_test_online?"INTERNET OK":"INTERNET FAILED"):"PRESS ENTER TO TEST"),8,48,1,wifi_test_online?accent:ink);
        draw_text(wifi_connected?"CONNECTED":"NOT CONNECTED",8,66,1,ink);
        std::snprintf(line,sizeof(line),"IP %.15s",wifi_test_ip);draw_text(line,8,80,1,muted);
        std::snprintf(line,sizeof(line),"SIGNAL %d dBm",wifi_test_rssi);draw_text(line,8,94,1,muted);
        std::snprintf(line,sizeof(line),"HTTP %d",wifi_test_http);draw_text(line,8,108,1,muted);
        draw_text("ENTER TEST  R RETEST  BKSP BACK",8,124,1,muted);break;
    case 16:
        draw_text("MUSIC / MP3 + WAV",8,27,1,accent);
        for(int i=0;i<5;i++){int ix=music_scroll+i;if(ix>=music_count)break;
            fill_round_rect(8,42+i*13,224,12,2,ix==music_selected?(dark_theme?rgb565(64,64,64):rgb565(205,205,205)):bg);
            draw_text(music_files[ix],14,44+i*13,1,ix==music_selected?accent:ink);}
        if(music_count==0)draw_text("ADD MP3/WAV TO MCAI-OS/MUSIC",8,65,1,muted);
        draw_text(music_playing?"PLAYING - ENTER TO STOP":"ENTER PLAY / STOP",8,111,1,music_playing?accent:muted);
        draw_text("UP/DN SELECT  R RESCAN  BKSP BACK",8,124,1,muted);break;
    case 13: {
        const std::int64_t elapsed=stopwatch_elapsed_us+(stopwatch_running?(esp_timer_get_time()-stopwatch_started_us):0);
        const int centis=(int)(elapsed/10000);
        const int seconds=centis/100, minutes=seconds/60;
        std::snprintf(line,sizeof(line),"%02d-%02d-%02d",minutes,seconds%60,centis%100);
        draw_text("STOPWATCH",8,29,1,accent);draw_text(line,8,53,3,ink);
        draw_text(stopwatch_running?"RUNNING":"PAUSED",8,91,1,muted);
        draw_text("ENTER START / PAUSE",8,110,1,muted);draw_text("R RESET",8,123,1,muted);
        break;
    }
    case 7: {
        int left=timer_seconds;if(timer_running)left=(int)std::max<std::int64_t>(0,(timer_end_us-esp_timer_get_time()+999999)/1000000);
        std::snprintf(line,sizeof(line),"%02d:%02d:%02d",left/3600,(left/60)%60,left%60);
        draw_text("COUNTDOWN TIMER",8,25,1,accent);draw_text(timer_running?line:timer_input,8,49,3,ink);
        const char* fields[]={"HOURS","MINUTES","SECONDS"};
        char field_line[32];std::snprintf(field_line,sizeof(field_line),timer_running?"RUNNING":"EDIT %s",fields[timer_field]);
        draw_text(field_line,8,91,1,muted);
        draw_text(timer_running?"ENTER PAUSE":"LEFT/RIGHT FIELD  UP/DN +/-",8,110,1,muted);
        draw_text("ENTER START  R 1 MIN  C CLEAR",8,123,1,muted);break;}
    case 8:
        draw_text("KEYBOARD TEST",8,27,1,accent);draw_text("LAST KEY:",8,48,1,muted);draw_text(last_key_label,68,48,1,ink);
        std::snprintf(line,sizeof(line),"KEY EVENTS: %lu",(unsigned long)keypress_count);draw_text(line,8,69,1,ink);
        draw_text("SHIFT=UPPER  FN+SHIFT=CAPS",8,91,1,muted);draw_text("TEST ALL 56 KEYS",8,105,1,muted);break;
    case 9: {
        if(sd_ready){draw_text("SD CARD MOUNTED",8,35,1,accent);draw_text("FILESYSTEM FAT",8,54,1,ink);draw_text("USE FILE MANAGER",8,73,1,muted);}else draw_text("SD CARD NOT MOUNTED",8,43,1,ink);break;}
    case 10:
        draw_text(keyboard_ready?"KEYBOARD PASS":"KEYBOARD FAIL",8,30,1,ink);draw_text(sd_ready?"SD MOUNT PASS":"SD MOUNT FAIL",8,48,1,ink);
        std::snprintf(line,sizeof(line),"FREE HEAP %lu",(unsigned long)esp_get_free_heap_size());draw_text(line,8,66,1,ink);
        std::snprintf(line,sizeof(line),"UPTIME %lu SEC",(unsigned long)(esp_timer_get_time()/1000000));draw_text(line,8,84,1,ink);break;
    case 14:
        draw_text("SYSTEM UPDATER",8,28,1,accent);
        fill_round_rect(8,42,224,37,5,rgb565(32,32,32));
        draw_text(ota_running?"UPDATING MCAI-OS":"CHECK FOR OS UPDATE",17,51,1,ink);
        draw_text(ota_message,8,91,1,muted);
        draw_text(ota_running?"PLEASE KEEP DEVICE POWERED":"ENTER: CHECK / INSTALL",8,111,1,accent);
        draw_text("ESC: BACK",8,124,1,muted);
        break;
    case 17: {
        char count[32];std::snprintf(count,sizeof(count),"%d",package_counter);
        draw_text(package_subtitle,8,30,1,muted);draw_text(count,8,55,3,accent);
        draw_text("ENTER: +1",8,94,1,ink);draw_text("BACKSPACE: STORE",8,111,1,muted);break;
    }
    case 11:
        if(!game_started){
            draw_text("GAME LIBRARY",8,26,1,accent);
            fill_round_rect(8,39,224,38,5,rgb565(32,32,32));
            fill_round_rect(13,44,30,28,4,rgb565(190,190,190));
            draw_text("S",23,52,2,bg);
            draw_text("SNAKE",51,45,1,ink);
            draw_text("ARCADE CLASSIC",51,59,1,muted);
            draw_text("MORE GAMES CAN BE ADDED",8,89,1,muted);
            draw_text("ENTER: PLAY SNAKE",8,111,1,accent);
            draw_text("ESC: HOME",8,124,1,muted);
        } else {
            draw_text("SNAKE",8,25,1,accent);
            if(game_over)draw_text("GAME OVER - ENTER",100,25,1,accent);
            for(int i=0;i<snake_len;i++)fill_rect(16+snake_x[i]*9,38+snake_y[i]*8,7,6,accent);
            fill_rect(16+food_x*9,38+food_y*8,7,6,rgb565(180,180,180));
            std::snprintf(line,sizeof(line),"SCORE %d",game_score);draw_text(line,8,116,1,ink);
            draw_text("ARROWS MOVE  ESC GAMES",8,128,1,muted);
        }
        break;
    default:
        draw_text("MCAI-OS FOR CARDPUTER ADV",8,37,1,ink);draw_text("VERSION 0.2.0",8,57,1,accent);draw_text("BUILT-IN APP SUITE",8,77,1,muted);break;
    }
    fill_rect(0,122,kWidth,1,rgb565(80,80,80)); // Keep app-specific footer text clear; ESC still returns home.
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,kWidth,kHeight,framebuffer));
}

void render_home() {
    const auto bg=dark_theme?rgb565(0,0,0):rgb565(255,255,255);
    const auto panel_bg=dark_theme?rgb565(24,24,24):rgb565(245,245,245);
    const auto panel_alt=dark_theme?rgb565(48,48,48):rgb565(220,220,220);
    const auto ink=dark_theme?rgb565(255,255,255):rgb565(0,0,0);
    const auto muted=dark_theme?rgb565(170,170,170):rgb565(75,75,75);
    const auto accent=dark_theme?rgb565(255,255,255):rgb565(0,0,0);
    const auto cyan=dark_theme?rgb565(190,190,190):rgb565(70,70,70);
    const auto purple=rgb565(115,115,115);
    const auto selected=dark_theme?rgb565(64,64,64):rgb565(205,205,205);
    if(active_app>=0) { render_app(); return; }
    std::fill(std::begin(framebuffer),std::end(framebuffer),bg);

    // MCAI-OS v2 visual system: compact status bar, app tiles and clear focus.
    fill_rect(0,0,kWidth,25,rgb565(0,0,0));
    fill_round_rect(7,5,15,15,4,accent);
    draw_text("M",11,9,1,rgb565(0,0,0));
    draw_text("MCAI",28,6,1,ink);
    draw_text("OS",54,6,1,accent);
    draw_text(sd_ready?"SD":"NO SD",164,6,1,sd_ready?accent:muted);
    draw_text(wifi_connected?"WIFI":"OFF",198,6,1,wifi_connected?cyan:muted);
    if(caps_lock)draw_text("C",224,6,1,accent);
    if(fn_lock)draw_text("F",233,6,1,purple);
    fill_rect(0,25,kWidth,1,dark_theme?rgb565(90,90,90):rgb565(130,130,130));

    if(organizer_mode) {
        draw_text("EDIT HOME",9,31,1,accent);
        const int page=organizer_selected/kAppsPerPage;
        for(int row=0;row<4;row++) {
            const int pos=page*kAppsPerPage+row;
            if(pos>=kAppCount)break;
            const int id=launcher_order[pos], y=44+row*17;
            fill_round_rect(7,y,226,15,3,pos==organizer_selected?selected:panel_bg);
            draw_text(kApps[id],14,y+4,1,pos==organizer_selected?accent:ink);
            if(launcher_foldered[id])draw_text("FOLDER",184,y+4,1,muted);
        }
        draw_text("O/ESC DONE   F TO FOLDER",8,124,1,muted);
    } else if(folder_open) {
        draw_text("MY FOLDER",9,31,1,accent);
        const int total=launcher_folder_count();
        if(folder_selected>=total)folder_selected=std::max(0,total-1);
        const int page=folder_selected/kAppsPerPage;
        for(int row=0;row<4;row++) {
            const int pos=page*kAppsPerPage+row;
            if(pos>=total)break;
            const int id=launcher_folder_item(pos), y=44+row*18;
            fill_round_rect(7,y,226,16,4,pos==folder_selected?selected:panel_bg);
            draw_text(kApps[id],15,y+5,1,pos==folder_selected?accent:ink);
            draw_text("OPEN",190,y+5,1,muted);
        }
        draw_text("ENTER OPEN   BKSP BACK",8,124,1,muted);
    } else {
        const int total=launcher_root_count();
        if(selected_app>=total)selected_app=std::max(0,total-1);
        const int page=selected_app/kAppsPerPage;
        const int pages=std::max(1,(total+kAppsPerPage-1)/kAppsPerPage);
        draw_text("YOUR APPS",9,31,1,muted);
        for(int tile=0;tile<4;tile++) {
            const int pos=page*kAppsPerPage+tile;
            if(pos>=total)continue;
            const int item=launcher_root_item(pos);
            const int x=(tile%2)?123:7;
            const int y=(tile/2)?78:43;
            const bool active=pos==selected_app;
            const auto tile_bg=active?selected:((tile%2)?panel_alt:panel_bg);
            fill_round_rect(x,y,110,31,5,tile_bg);
            fill_round_rect(x+5,y+6,19,19,5,active?accent:(tile==1?cyan:tile==2?purple:muted));
            const char* label=launcher_item_name(item);
            char icon[2]={label&&label[0]?label[0]:'?',0};
            draw_text(icon,x+11,y+12,1,rgb565(0,0,0));
            char short_name[15]={};
            if(label)std::snprintf(short_name,sizeof(short_name),"%.13s",label);
            draw_text(short_name,x+30,y+8,1,active?accent:ink);
            draw_text(active?"SELECTED":"APP",x+30,y+19,1,muted);
            if(active)fill_rect(x+4,y+28,102,2,accent);
        }
        char page_label[20];
        std::snprintf(page_label,sizeof(page_label),"PAGE %d / %d",page+1,pages);
        draw_text(page_label,8,119,1,accent);
        draw_text("ARROWS MOVE",100,119,1,muted);
        draw_text("ENTER OPEN   O EDIT",8,130,1,muted);
    }
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,kWidth,kHeight,framebuffer));
}
void init_display() {
    gpio_config_t boot = {};
    boot.pin_bit_mask = 1ULL << GPIO_NUM_0;
    boot.mode = GPIO_MODE_INPUT;
    boot.pull_up_en = GPIO_PULLUP_ENABLE;
    boot.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&boot));
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
    // Cardputer ADV uses the TCA8418 7x8 keypad matrix.
    // Only keypad events are enabled; GPIO event interrupts are not used.
    const std::uint8_t setup[][2] = {
        {0x1D,0x7F}, // KP_GPIO1: rows R0-R6
        {0x1E,0xFF}, // KP_GPIO2: columns C0-C7
        {0x1F,0x00}, // KP_GPIO3: unused
        {0x01,0x99}  // CFG: auto-increment, overflow mode, interrupt config, key events
    };
    for(const auto& reg:setup) {
        if(keyboard_write(reg[0],reg[1])!=ESP_OK) {
            ESP_LOGW(kTag,"TCA8418 setup failed at register 0x%02X",reg[0]); return;
        }
    }
    std::uint8_t stale=0;
    while(keyboard_read(0x03,&stale)==ESP_OK && (stale&0x0F)) {
        std::uint8_t discard=0;
        if(keyboard_read(0x04,&discard)!=ESP_OK) break;
    }
    keyboard_write(0x02,0x1F); // clear all pending interrupt status bits
    keyboard_ready=true;
    ESP_LOGI(kTag,"TCA8418 7x8 matrix initialized");
}

bool es8311_write(std::uint8_t reg,std::uint8_t value) {
    const std::uint8_t packet[2]={reg,value};
    return i2c_master_write_to_device(I2C_NUM_0,0x18,packet,sizeof(packet),pdMS_TO_TICKS(100))==ESP_OK;
}
bool init_audio() {
    const std::uint8_t seq[][2]={{0x00,0x80},{0x01,0xB5},{0x02,0x18},{0x0D,0x01},
                                 {0x12,0x00},{0x13,0x10},{0x32,0xBF},{0x37,0x08}};
    for(const auto& reg:seq) {
        if(!es8311_write(reg[0],reg[1])){ESP_LOGW(kTag,"ES8311 init failed at 0x%02X",reg[0]);return false;}
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    i2s_config_t cfg={};
    cfg.mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX);
    cfg.sample_rate=44100;
    cfg.bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT;
    cfg.channel_format=I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format=I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags=0;
    cfg.dma_buf_count=8;
    cfg.dma_buf_len=256;
    cfg.tx_desc_auto_clear=true;
    cfg.use_apll=false;
    if(i2s_driver_install(I2S_NUM_0,&cfg,0,nullptr)!=ESP_OK){ESP_LOGW(kTag,"I2S install failed");return false;}
    i2s_pin_config_t pins={};
    pins.mck_io_num=I2S_PIN_NO_CHANGE;
    pins.bck_io_num=GPIO_NUM_41;
    pins.ws_io_num=GPIO_NUM_43;
    pins.data_out_num=GPIO_NUM_42;
    pins.data_in_num=I2S_PIN_NO_CHANGE;
    if(i2s_set_pin(I2S_NUM_0,&pins)!=ESP_OK){i2s_driver_uninstall(I2S_NUM_0);ESP_LOGW(kTag,"I2S pin setup failed");return false;}
    audio_ready=true;
    ESP_LOGI(kTag,"ES8311 speaker ready");
    return true;
}
void play_tone(std::uint32_t frequency,std::uint32_t duration_ms) {
    if(!audio_ready||!sound_enabled||frequency==0)return;
    constexpr std::uint32_t rate=44100;
    constexpr size_t chunk=128;
    std::int16_t samples[chunk*2];
    const std::uint32_t total=rate*duration_ms/1000;
    for(std::uint32_t base=0;base<total;base+=chunk) {
        const size_t n=std::min((std::size_t)chunk,(std::size_t)(total-base));
        for(size_t i=0;i<n;i++) {
            const float phase=6.28318530718f*(float)frequency*(float)(base+i)/(float)rate;
            const std::int16_t sample=(std::int16_t)(4200.0f*std::sin(phase));
            samples[i*2]=sample;samples[i*2+1]=sample;
        }
        size_t written=0;
        if(i2s_write(I2S_NUM_0,samples,n*4,&written,pdMS_TO_TICKS(100))!=ESP_OK||written<n*4)break;
    }
}
void play_menu_sound() {
    if(!audio_ready||!sound_enabled)return;
    // A short, gentle ascending two-note chime for menu movement/selection.
    play_tone(880,18);
    play_tone(1175,28);
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

char decode_key(int row, int col, bool shifted) {
    static const char normal[4][14] = {
        {'`','1','2','3','4','5','6','7','8','9','0','-','=','\0'},
        {'\0','q','w','e','r','t','y','u','i','o','p','[',']','\\'},
        {'\0','\0','a','s','d','f','g','h','j','k','l',';', '\'','\0'},
        {'\0','\0','\0','z','x','c','v','b','n','m',',','.','/',' '}
    };
    static const char upper[4][14] = {
        {'~','!','@','#','$','%','^','&','*','(',')','_','+','\0'},
        {'\0','Q','W','E','R','T','Y','U','I','O','P','{','}','|'},
        {'\0','\0','A','S','D','F','G','H','J','K','L',':','"','\0'},
        {'\0','\0','\0','Z','X','C','V','B','N','M','<','>','?',' '}
    };
    if(row<0||row>=4||col<0||col>=14)return 0;
    return shifted?upper[row][col]:normal[row][col];
}
void poll_keyboard() {
    if(!keyboard_ready)return;
    std::uint8_t count=0;if(keyboard_read(0x03,&count)!=ESP_OK)return;count&=0x0F;
    while(count--){
        std::uint8_t event=0;if(keyboard_read(0x04,&event)!=ESP_OK||event==0)break;
        // TCA8418 event format: high bit SET means key DOWN;
        // high bit CLEAR means key UP (release).
        const bool pressed=(event&0x80)!=0;
        const int raw=(event&0x7F);
        if(raw<=0||raw>70)continue; // Cardputer ADV is a 7-row x 8-column matrix.
        const int raw_index=raw-1, raw_row=raw_index/10, raw_col=raw_index%10;
        // TCA8418 raw FIFO positions are remapped exactly as M5Stack's ADV driver.
        const int row=(raw_col+4)%4, col=raw_row*2+(raw_col>3?1:0);
        if(row<0||row>=4||col<0||col>=14)continue;
        ui_dirty=true;
        if(!pressed){
            if(row==2&&col==0)fn_lock=false;
            if(row==2&&col==1)shift_held=false;
            continue;
        }
        if(row==2&&col==0){fn_lock=true;std::snprintf(last_key_label,sizeof(last_key_label),"FN");++keypress_count;continue;}
        if(row==2&&col==1){
            if(fn_lock)caps_lock=!caps_lock;
            else shift_held=true;
            std::snprintf(last_key_label,sizeof(last_key_label),caps_lock?"CAPS":"SHIFT");
            ++keypress_count;continue;
        }
        std::uint8_t key=0;
        char ch=decode_key(row,col,shift_held!=caps_lock);
        // Swap the physical apostrophe and Escape functions: Escape normally,
        // apostrophe with Fn. Del is Delete normally and Backspace with Fn.
        // The four physical arrow keys navigate normally; Fn types their legends.
        if(row==2&&col==12){if(fn_lock){ch='\'';key=0;}else{key=KEY_ESC;ch=0;}}
        if(fn_lock){
            if(row==0&&col>=1&&col<=12){ch=0;std::snprintf(last_key_label,sizeof(last_key_label),"F%d",col);}
            else if(row==0&&col==13){key=KEY_BACKSPACE;ch=0;}
            else if(row==2&&col==11){ch=';';key=0;}
            else if(row==3&&col==10){ch=',';key=0;}
            else if(row==3&&col==11){ch='.';key=0;}
            else if(row==3&&col==12){ch='/';key=0;}
        } else {
            if(row==2&&col==11){key=KEY_UP;ch=0;}
            else if(row==3&&col==10){key=KEY_LEFT;ch=0;}
            else if(row==3&&col==11){key=KEY_DOWN;ch=0;}
            else if(row==3&&col==12){key=KEY_RIGHT;ch=0;}
        }
        if(!key){
            if(row==0&&col==13)key=KEY_DELETE;
            else if(row==1&&col==0)key=KEY_TAB;
            else if(row==2&&col==13)key=KEY_ENTER;
            else if(row==3&&col==0)key=KEY_CTRL;
            else if(row==3&&col==1)key=KEY_OPT;
            else if(row==3&&col==2)key=KEY_ALT;
        }
        if(key==KEY_ESC)std::snprintf(last_key_label,sizeof(last_key_label),"ESC");
        else if(key==KEY_UP)std::snprintf(last_key_label,sizeof(last_key_label),"UP");
        else if(key==KEY_DOWN)std::snprintf(last_key_label,sizeof(last_key_label),"DOWN");
        else if(key==KEY_LEFT)std::snprintf(last_key_label,sizeof(last_key_label),"LEFT");
        else if(key==KEY_RIGHT)std::snprintf(last_key_label,sizeof(last_key_label),"RIGHT");
        else if(key==KEY_BACKSPACE)std::snprintf(last_key_label,sizeof(last_key_label),"BACKSPACE");
        else if(key==KEY_ENTER)std::snprintf(last_key_label,sizeof(last_key_label),"ENTER");
        else if(key==KEY_TAB)std::snprintf(last_key_label,sizeof(last_key_label),"TAB");
        else if(key==KEY_CTRL)std::snprintf(last_key_label,sizeof(last_key_label),"CTRL");
        else if(key==KEY_ALT)std::snprintf(last_key_label,sizeof(last_key_label),"ALT");
        else if(key==KEY_OPT)std::snprintf(last_key_label,sizeof(last_key_label),"OPT");
        else if(key==KEY_DELETE)std::snprintf(last_key_label,sizeof(last_key_label),"DELETE");
        else if(fn_lock&&row==0&&col>=1&&col<=12)std::snprintf(last_key_label,sizeof(last_key_label),"F%d",col);
        else if(ch)std::snprintf(last_key_label,sizeof(last_key_label),"%c",ch);
        else std::snprintf(last_key_label,sizeof(last_key_label),"FN KEY");
        last_char=ch?ch:(key==KEY_UP?'^':key==KEY_DOWN?'v':key==KEY_LEFT?'<':key==KEY_RIGHT?'>':'?');
        ++keypress_count;
        if(active_app<0){
            if(organizer_mode){
                if(is_back(key)||ch=='o'||ch=='O')organizer_mode=false;
                else if(is_up(key))organizer_selected=(organizer_selected+kAppCount-1)%kAppCount;
                else if(is_down(key))organizer_selected=(organizer_selected+1)%kAppCount;
                else if(is_left(key)||is_right(key)){
                    const int next=organizer_selected+(is_left(key)?-1:1);
                    if(next>=0&&next<kAppCount){std::swap(launcher_order[organizer_selected],launcher_order[next]);organizer_selected=next;save_launcher_layout();}
                } else if(ch=='f'||ch=='F'){
                    const int id=launcher_order[organizer_selected];launcher_foldered[id]=!launcher_foldered[id];save_launcher_layout();
                }
            } else if(folder_open){
                const int total=launcher_folder_count();
                if(is_back(key)){folder_open=false;selected_app=0;}
                else if(is_up(key)&&total>0)folder_selected=(folder_selected+total-1)%total;
                else if(is_down(key)&&total>0)folder_selected=(folder_selected+1)%total;
                else if(key==KEY_ENTER&&total>0)start_launcher_item(launcher_folder_item(folder_selected));
            } else {
                const int total=launcher_root_count();
                if(ch=='o'||ch=='O'){organizer_mode=true;organizer_selected=0;}
                else if(is_left(key)){if(total>0&&selected_app%2==1)--selected_app;play_menu_sound();}
                else if(is_right(key)){if(total>0&&selected_app%2==0&&selected_app+1<total)++selected_app;play_menu_sound();}
                else if(is_up(key)){if(selected_app>=2)selected_app-=2;play_menu_sound();}
                else if(is_down(key)){if(selected_app+2<total)selected_app+=2;play_menu_sound();}
                else if(key==KEY_ENTER){play_menu_sound();start_launcher_item(launcher_root_item(selected_app));}
            }
            ui_dirty=true;
            continue;
        }
        if(active_app==0){
            if(viewing_file){if(is_back(key)){viewing_file=false;file_scroll=0;}else if(is_up(key)||is_left(key))file_scroll=std::max(0,file_scroll-1);else if(is_down(key)||is_right(key)){const int lines=wrapped_line_count(viewer_buffer,36);file_scroll=std::min(std::max(0,lines-5),file_scroll+1);}}
            else if(is_back(key)){if(std::strcmp(current_dir,"/sdcard")!=0){char* slash=std::strrchr(current_dir,'/');if(slash&&slash!=current_dir)*slash=0;else std::strcpy(current_dir,"/sdcard");refresh_files();}else active_app=-1;}
            else if(is_up(key)||is_left(key)){if(file_selected>0)--file_selected;if(file_selected<file_scroll)file_scroll=file_selected;}
            else if(is_down(key)||is_right(key)){if(file_selected+1<file_count)++file_selected;if(file_selected>=file_scroll+5)file_scroll=file_selected-4;}
            else if(key==KEY_ENTER)open_selected_file();
        } else if(active_app==1){
            if(!note_editing){
                if(is_up(key)){
                    if(notes_action==0&&note_selected==0)notes_action=1;
                    else if(notes_action==0&&note_selected>0){--note_selected;if(note_selected<note_scroll)note_scroll=note_selected;}
                }
                else if(is_down(key)){
                    if(notes_action==1){notes_action=0;note_selected=std::min(note_selected,std::max(0,note_count-1));}
                    else if(note_selected+1<note_count){++note_selected;if(note_selected>=note_scroll+5)note_scroll=note_selected-4;}
                }
                else if(is_left(key)||is_right(key))notes_action=is_left(key)?0:1;
                else if(key==KEY_ENTER){if(notes_action==1)create_note();else if(note_count>0)open_note(note_selected);else notes_action=1;}
                else if(ch=='n'||ch=='N')create_note();
                else if(key==KEY_BACKSPACE||key==KEY_ESC)active_app=-1;
            } else {
                if(key==KEY_ESC||(key==KEY_ENTER&&fn_lock)){save_note();note_editing=false;refresh_notes();notes_action=0;}
                else if(is_up(key)){note_scroll=std::max(0,note_scroll-1);}
                else if(is_down(key)){note_scroll=std::min(std::max(0,note_visual_line_count()-6),note_scroll+1);}
                else if(key==KEY_BACKSPACE){if(note_length>0){--note_length;note_buffer[note_length]=0;save_note();}}
                else if(key==KEY_ENTER&&note_length<(int)sizeof(note_buffer)-2){note_buffer[note_length++]='\n';note_buffer[note_length]=0;note_scroll=std::max(0,note_visual_line_count()-6);save_note();}
                else if(ch>=32&&ch<127&&note_length<(int)sizeof(note_buffer)-2){note_buffer[note_length++]=ch;note_buffer[note_length]=0;note_scroll=std::max(0,note_visual_line_count()-6);save_note();}
            }
        } else if(active_app==2){
            if(key==KEY_ESC)active_app=-1;
            else if(key==KEY_BACKSPACE||key==KEY_DELETE){if(key==KEY_DELETE){calc_buffer[0]=0;calc_length=0;calc_showing_result=false;}else if(calc_length>0){calc_buffer[--calc_length]=0;calc_showing_result=false;}calc_error=false;calc_error_text[0]=0;}
            else if(key==KEY_ENTER){calc_showing_result=calculate();}
            else if(calc_length<(int)sizeof(calc_buffer)-2){
                char op=0;
                if(ch=='p'||ch=='P')op='+';else if(ch=='m'||ch=='M')op='-';else if(ch=='x'||ch=='X')op='*';else if(ch=='d'||ch=='D')op='/';
                const bool allowed=(ch>='0'&&ch<='9')||ch=='.'||ch=='('||ch==')'||ch=='%'||op;
                if(allowed){if(calc_showing_result){calc_buffer[0]=0;calc_length=0;calc_showing_result=false;}calc_error=false;calc_error_text[0]=0;calc_buffer[calc_length++]=op?op:ch;calc_buffer[calc_length]=0;}
            }
        } else if(active_app==3){
            if(bluetooth_screen){
                if(key==KEY_ESC||key==KEY_BACKSPACE){stop_ble_scan();bluetooth_screen=false;}
                else if(key==KEY_ENTER){stop_ble_scan();start_ble_scan();}
                else if(is_up(key)){ble_device_scroll=std::max(0,ble_device_scroll-1);}
                else if(is_down(key)){ble_device_scroll=std::min(std::max(0,ble_device_count-5),ble_device_scroll+1);}
                else if(ch=='m'||ch=='M'){ble_complex_mode=!ble_complex_mode;stop_ble_scan();start_ble_scan();}
            } else if(wifi_setup_step>0){
                if(key==KEY_ESC){wifi_setup_step=0;}
                else if(wifi_setup_step==3){
                    if(is_up(key)){wifi_network_selected=std::max(0,wifi_network_selected-1);if(wifi_network_selected<wifi_network_scroll)wifi_network_scroll=wifi_network_selected;}
                    else if(is_down(key)){wifi_network_selected=std::min(std::max(0,wifi_network_count-1),wifi_network_selected+1);if(wifi_network_selected>=wifi_network_scroll+5)wifi_network_scroll=wifi_network_selected-4;}
                    else if(ch=='r'||ch=='R'){scan_wifi_networks();}
                    else if(key==KEY_BACKSPACE){wifi_setup_step=0;}
                    else if(ch=='m'||ch=='M'){wifi_ssid_input[0]=0;wifi_password_input[0]=0;wifi_setup_step=1;}
                    else if(key==KEY_ENTER&&wifi_network_count>0){
                        std::snprintf(wifi_ssid_input,sizeof(wifi_ssid_input),"%s",wifi_networks[wifi_network_selected]);
                        wifi_password_input[0]=0;
                        if(wifi_network_auth[wifi_network_selected]==WIFI_AUTH_OPEN){
                            save_wifi_settings();
                            start_wifi_connect(wifi_ssid_input,"");
                            wifi_setup_step=0;
                        } else wifi_setup_step=4;
                    }
                } else if(key==KEY_BACKSPACE){
                    char* field=(wifi_setup_step==1)?wifi_ssid_input:wifi_password_input;
                    size_t n=std::strlen(field);if(n)field[n-1]=0;
                } else if(key==KEY_ENTER){
                    if(wifi_setup_step==1){wifi_setup_step=2;}
                    else {
                        save_wifi_settings();
                        start_wifi_connect(wifi_ssid_input,wifi_password_input);
                        wifi_setup_step=0;
                    }
                } else if(ch>=32&&ch<127){
                    if(wifi_setup_step==1&&std::strlen(wifi_ssid_input)<32){size_t n=std::strlen(wifi_ssid_input);wifi_ssid_input[n]=ch;wifi_ssid_input[n+1]=0;}
                    else if((wifi_setup_step==2||wifi_setup_step==4)&&std::strlen(wifi_password_input)<64){size_t n=std::strlen(wifi_password_input);wifi_password_input[n]=ch;wifi_password_input[n+1]=0;}
                }
            } else if(is_up(key)||is_down(key)){settings_selection=(settings_selection+(is_up(key)?4:1))%5;}
            else if(is_left(key)||is_right(key)){
                if(settings_selection==0){setting_brightness=std::max(0,std::min(2,setting_brightness+(is_right(key)?1:-1)));apply_brightness();save_settings();}
                else if(settings_selection==1){dark_theme=!dark_theme;save_settings();}
                else if(settings_selection==4){sound_enabled=!sound_enabled;save_settings();}
            } else if(key==KEY_ENTER){
                if(settings_selection==1){dark_theme=!dark_theme;save_settings();}
                else if(settings_selection==2){load_wifi_settings();wifi_setup_step=3;scan_wifi_networks();}
                else if(settings_selection==3){bluetooth_screen=true;if(!start_ble_scan())ui_dirty=true;}
                else if(settings_selection==4){sound_enabled=!sound_enabled;save_settings();}
            } else if(key==KEY_ESC||key==KEY_BACKSPACE)active_app=-1;
        } else if(active_app==5){
            if(is_back(key))active_app=-1;
            else if(store_loading){std::snprintf(store_message,sizeof(store_message),"PLEASE WAIT - LOADING");}
            else if(is_up(key)||is_left(key)){if(store_selected>0)--store_selected;if(store_selected<store_scroll)store_scroll=store_selected;}
            else if(is_down(key)||is_right(key)){if(store_selected+1<store_count)++store_selected;if(store_selected>=store_scroll+4)store_scroll=store_selected-3;}
            else if(ch=='r'||ch=='R'){refresh_store();}
            else if(key==KEY_ENTER){
                if(store_count<=0){std::snprintf(store_message,sizeof(store_message),"NO APPS - PRESS R TO RETRY");}
                else {
                    const int builtin=store_builtin_index(store_ids[store_selected]);
                    if(builtin>=0){
                        active_app=builtin;
                        if(builtin==0){std::snprintf(current_dir,sizeof(current_dir),"/sdcard");refresh_files();}
                        if(builtin==1){note_editing=false;refresh_notes();}
                        if(builtin==5)refresh_store();
                        if(builtin==6)last_clock_render_second=-1;
                    } else if(package_installed(store_ids[store_selected])) {
                        package_return_app=5;
                        if(!launch_installed_package(store_ids[store_selected]))std::snprintf(store_message,sizeof(store_message),"APP LOAD FAILED");
                    } else {install_store_package();refresh_installed_packages();}
                }
            }
        } else if(active_app==13){
            if(is_back(key)){stopwatch_running=false;stopwatch_elapsed_us=0;active_app=-1;}
            else if(key==KEY_ENTER){if(stopwatch_running){stopwatch_elapsed_us+=esp_timer_get_time()-stopwatch_started_us;stopwatch_running=false;}else{stopwatch_started_us=esp_timer_get_time();stopwatch_running=true;}}
            else if(ch=='r'||ch=='R'){stopwatch_elapsed_us=0;if(stopwatch_running)stopwatch_started_us=esp_timer_get_time();}
        } else if(active_app==14){
            if(is_back(key)){if(!ota_running)active_app=-1;}
            else if(key==KEY_ENTER&&!ota_running)start_ota_update();
            else if((ch=='r'||ch=='R')&&!ota_running)start_ota_update();
        } else if(active_app==15){
            if(is_back(key))active_app=-1;
            else if(key==KEY_ENTER||ch=='r'||ch=='R')start_wifi_test();
        } else if(active_app==16){
            if(is_back(key)){if(music_playing)music_stop_requested=true;active_app=-1;}
            else if(is_up(key)){if(music_selected>0)--music_selected;if(music_selected<music_scroll)music_scroll=music_selected;}
            else if(is_down(key)){if(music_selected+1<music_count)++music_selected;if(music_selected>=music_scroll+5)music_scroll=music_selected-4;}
            else if(key==KEY_ENTER)start_music_playback();
            else if(ch=='r'||ch=='R')refresh_music_files();
        } else if(active_app==17){
            if(is_back(key)){package_running=false;active_app=package_return_app;}
            else if(key==KEY_ENTER){package_counter=std::min(999999,package_counter+package_increment);}
        } else if(active_app==6){
            if(key==KEY_ESC||key==KEY_BACKSPACE)active_app=-1;
        } else if(active_app==7){
            if(key==KEY_ENTER){
                if(timer_running){
                    timer_running=false;
                    timer_seconds=std::max(0,(int)((timer_end_us-esp_timer_get_time()+999999)/1000000));
                    format_timer_fields(timer_seconds/3600,(timer_seconds/60)%60,timer_seconds%60);
                } else {
                    int h=0,m=0,sec=0;
                    if(parse_timer_fields(h,m,sec)){
                        timer_seconds=h*3600+m*60+sec;
                        if(timer_seconds>0){timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;timer_running=true;}
                        else std::snprintf(store_message,sizeof(store_message),"SET A TIME ABOVE ZERO");
                    } else std::snprintf(store_message,sizeof(store_message),"USE HH:MM:SS");
                }
            } else if(!timer_running&&is_left(key))timer_field=(timer_field+2)%3;
            else if(!timer_running&&is_right(key))timer_field=(timer_field+1)%3;
            else if(!timer_running&&is_up(key))adjust_timer_field(1);
            else if(!timer_running&&is_down(key))adjust_timer_field(-1);
            else if(!timer_running&&key==KEY_BACKSPACE){size_t n=std::strlen(timer_input);if(n)timer_input[n-1]=0;}
            else if(!timer_running&&(ch=='c'||ch=='C'))timer_input[0]=0;
            else if(!timer_running&&(ch=='r'||ch=='R')){timer_field=2;format_timer_fields(0,1,0);}
            else if(!timer_running&&((ch>='0'&&ch<='9')||ch==':')&&std::strlen(timer_input)<8){size_t n=std::strlen(timer_input);timer_input[n]=ch;timer_input[n+1]=0;}
        } else if(active_app==8){/* Every key press is counted above. */}
        else if(active_app==11){
            if(is_back(key)){if(game_started)game_started=false;else active_app=-1;}
            else if(!game_started&&key==KEY_ENTER){game_started=true;game_over=false;snake_len=3;snake_x[0]=10;snake_y[0]=4;snake_x[1]=9;snake_y[1]=4;snake_x[2]=8;snake_y[2]=4;snake_dx=1;snake_dy=0;food_x=14;food_y=4;game_score=0;game_tick_us=esp_timer_get_time();}
            else if(is_up(key)&&snake_dy==0){snake_dx=0;snake_dy=-1;}
            else if(is_down(key)&&snake_dy==0){snake_dx=0;snake_dy=1;}
            else if(is_left(key)&&snake_dx==0){snake_dx=-1;snake_dy=0;}
            else if(is_right(key)&&snake_dx==0){snake_dx=1;snake_dy=0;}
            else if(game_over&&key==KEY_ENTER){game_started=false;}
        }
        if(key==KEY_ESC&&active_app!=0&&active_app!=1&&active_app!=2&&active_app!=3&&active_app!=5&&active_app!=6&&active_app!=11)active_app=-1;
    }
    std::uint8_t status=0;if(keyboard_read(0x02,&status)==ESP_OK&&(status&0x01))keyboard_write(0x02,0x01);
}} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag,"Starting MCAI-OS Cardputer ADV");
    // Set up the ADV backlight before touching the LCD. GPIO38 must not be
    // driven as a plain HIGH output: it is a shared power/backlight rail.
    ledc_timer_config_t timer={};
    timer.speed_mode=LEDC_LOW_SPEED_MODE;
    timer.duty_resolution=LEDC_TIMER_9_BIT;
    timer.timer_num=LEDC_TIMER_3;
    timer.freq_hz=256;
    timer.clk_cfg=LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel={};
    channel.gpio_num=kLcdBacklight;
    channel.speed_mode=LEDC_LOW_SPEED_MODE;
    channel.channel=LEDC_CHANNEL_7;
    channel.intr_type=LEDC_INTR_DISABLE;
    channel.timer_sel=LEDC_TIMER_3;
    channel.duty=256;
    channel.hpoint=0;
    ESP_ERROR_CHECK(ledc_channel_config(&channel));
    init_display();
    init_keyboard();
    draw_mcaI_logo_splash();
    // Short, non-interactive splash: boot always continues to the real launcher.
    vTaskDelay(pdMS_TO_TICKS(350));
    init_audio();init_sd_card();load_settings();load_launcher_layout();refresh_installed_packages();seed_store();apply_brightness();
    const esp_err_t ota_mark=esp_ota_mark_app_valid_cancel_rollback();
    if(ota_mark!=ESP_OK&&ota_mark!=ESP_ERR_NOT_FOUND)ESP_LOGW(kTag,"OTA validation mark: %s",esp_err_to_name(ota_mark));
    render_home();play_tone(660,90);
    for(;;){
        const int old_app=active_app,old_sel=selected_app;
        poll_keyboard();
        const bool boot_pressed=gpio_get_level(GPIO_NUM_0)==0;
        if(boot_pressed&&!boot_button_was_pressed){
            if(active_app>=0){if(active_app==1)save_note();active_app=-1;viewing_file=false;}
            else if(organizer_mode){organizer_mode=false;}
            else if(folder_open){const int id=launcher_folder_item(folder_selected);start_launcher_item(id);}
            else {const int item=launcher_root_item(selected_app);if(item==-1){folder_open=true;folder_selected=0;}else start_launcher_item(item);}
        }
        boot_button_was_pressed=boot_pressed;
        const std::int64_t now=esp_timer_get_time();
        if(timer_running&&now>=timer_end_us){timer_running=false;timer_seconds=0;std::snprintf(timer_input,sizeof(timer_input),"00:00:00");ui_dirty=true;play_tone(880,180);play_tone(1175,180);play_tone(1568,260);}
        if(active_app==11&&game_started&&!game_over&&now-game_tick_us>220000){
            game_tick_us=now;
            for(int i=snake_len-1;i>0;i--){snake_x[i]=snake_x[i-1];snake_y[i]=snake_y[i-1];}
            snake_x[0]+=snake_dx;snake_y[0]+=snake_dy;
            if(snake_x[0]<0||snake_x[0]>=23||snake_y[0]<0||snake_y[0]>=9)game_over=true;
            for(int i=1;i<snake_len;i++)if(snake_x[0]==snake_x[i]&&snake_y[0]==snake_y[i])game_over=true;
            if(snake_x[0]==food_x&&snake_y[0]==food_y){if(snake_len<47)++snake_len;++game_score;food_x=(food_x+7)%23;food_y=(food_y+3)%9;}
        }
        const int clock_second=(int)std::time(nullptr);
        const bool clock_tick=(active_app==6&&clock_second!=last_clock_render_second);
        if(old_app!=active_app||old_sel!=selected_app||clock_tick||active_app==7||active_app==11||active_app==13||active_app==5||ui_dirty){render_home();ui_dirty=false;if(active_app==6)last_clock_render_second=clock_second;}
        if(active_app==1&&note_editing&&note_length>0&&note_length%24==0)save_note();
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}
