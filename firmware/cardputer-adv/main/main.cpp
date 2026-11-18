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
#include <sys/time.h>
#include <unistd.h>

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
constexpr std::uint8_t KEY_ESC=1, KEY_FN=3, KEY_CAPS=7, KEY_UP=57, KEY_DOWN=58,
                        KEY_LEFT=54, KEY_RIGHT=64, KEY_BACKSPACE=65, KEY_ENTER=67;
bool caps_lock = false;
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
int ble_device_count=0, ble_device_scroll=0;
char ble_devices[12][32]={};
char calc_buffer[48] = {};
int calc_length = 0;
int file_scroll = 0;
char note_buffer[512] = {};
int note_length = 0, note_scroll = 0;
char note_files[16][40] = {};
int note_count=0, note_selected=0;
char note_filename[64] = {};
bool note_editing=false;
char last_char = '-';
unsigned keypress_count = 0;
char current_dir[160] = "/sdcard";
char file_names[32][64] = {};
bool file_is_dir[32] = {};
int file_count = 0, file_selected = 0;
char viewer_buffer[700] = {};
bool viewing_file = false;
char store_names[128][64] = {}; char store_status[128][20] = {}; char store_ids[128][64] = {}; char store_urls[128][256] = {}; char store_hashes[128][65] = {}; size_t store_sizes[128] = {};
int store_count=0, store_selected=0, store_scroll=0;
char store_message[40]="ADD WIFI.CFG ON SD"; bool wifi_started=false, catalogue_loaded=false, ntp_started=false;
char catalogue_buffer[32768]={}; size_t catalogue_length=0;
bool package_running=false; char package_title[64]=""; char package_subtitle[96]=""; char package_id[64]=""; int package_counter=0, package_increment=1;
char clock_input[8] = {};
int clock_len = 0;
int timer_seconds = 60;
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
    note_count=0; note_selected=0;
    if(!sd_ready) return;
    mkdir("/sdcard/MCAI-OS",0775);
    mkdir("/sdcard/MCAI-OS/notes",0775);
    DIR* d=opendir("/sdcard/MCAI-OS/notes"); if(!d) return;
    struct dirent* e;
    while((e=readdir(d))!=nullptr && note_count<16) {
        const size_t n=std::strlen(e->d_name);
        if(n<5 || std::strcmp(e->d_name+n-4,".txt")!=0) continue;
        std::snprintf(note_files[note_count],sizeof(note_files[note_count]),"%.39s",e->d_name);
        ++note_count;
    }
    closedir(d);
}
void save_note() {
    if(!sd_ready || !note_filename[0]) return;
    FILE* f=std::fopen(note_filename,"w");
    if(f){std::fwrite(note_buffer,1,note_length,f);std::fclose(f);}
}
void open_note(int index) {
    if(index<0 || index>=note_count) return;
    std::snprintf(note_filename,sizeof(note_filename),"/sdcard/MCAI-OS/notes/%.39s",note_files[index]);
    note_length=0; note_buffer[0]=0;
    FILE* f=std::fopen(note_filename,"r");
    if(f){note_length=(int)std::fread(note_buffer,1,sizeof(note_buffer)-1,f);note_buffer[note_length]=0;std::fclose(f);}
    note_editing=true; note_scroll=0;
}
void create_note() {
    if(!sd_ready){note_filename[0]=0;return;}
    mkdir("/sdcard/MCAI-OS",0775); mkdir("/sdcard/MCAI-OS/notes",0775);
    char name[40]; int number=1;
    do {std::snprintf(name,sizeof(name),"NOTE-%03d.txt",number++);char full[100];std::snprintf(full,sizeof(full),"/sdcard/MCAI-OS/notes/%s",name);struct stat st{};if(stat(full,&st)!=0)break;} while(number<1000);
    std::snprintf(note_filename,sizeof(note_filename),"/sdcard/MCAI-OS/notes/%s",name);
    note_buffer[0]=0;note_length=0;note_editing=true;note_scroll=0;save_note();refresh_notes();
    for(int i=0;i<note_count;i++)if(std::strcmp(note_files[i],name)==0){note_selected=i;break;}
}
bool eval_expr(const char*& p,double& out);
bool eval_factor(const char*& p,double& out) {
    while(*p==' ') ++p;
    if(*p=='-'){++p;if(!eval_factor(p,out))return false;out=-out;return true;}
    if(*p=='('){++p;if(!eval_expr(p,out))return false;while(*p==' ')++p;if(*p!=')')return false;++p;return true;}
    char* end=nullptr;out=std::strtod(p,&end);if(end==p)return false;p=end;return std::isfinite(out);
}
bool eval_term(const char*& p,double& out) {
    if(!eval_factor(p,out))return false;
    for(;;){while(*p==' ')++p;char op=*p;if(op!='*'&&op!='/')return true;++p;double rhs=0;if(!eval_factor(p,rhs))return false;if(op=='/'&&rhs==0)return false;out=op=='*'?out*rhs:out/rhs;}
}
bool eval_expr(const char*& p,double& out) {
    if(!eval_term(p,out))return false;
    for(;;){while(*p==' ')++p;char op=*p;if(op!='+'&&op!='-')return true;++p;double rhs=0;if(!eval_term(p,rhs))return false;out=op=='+'?out+rhs:out-rhs;}
}
void calculate() {
    const char* p=calc_buffer; double result=0;
    if(!eval_expr(p,result)){std::snprintf(calc_buffer,sizeof(calc_buffer),"INVALID / DIV0");calc_length=(int)std::strlen(calc_buffer);return;}
    while(*p==' ')++p;
    if(*p){std::snprintf(calc_buffer,sizeof(calc_buffer),"INVALID INPUT");calc_length=(int)std::strlen(calc_buffer);return;}
    std::snprintf(calc_buffer,sizeof(calc_buffer),"%.8g",result);
    calc_length=(int)std::strlen(calc_buffer);
}
void refresh_files() {
    file_count=0; file_selected=0;
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
void ble_gap_event(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if(event==ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
        if(param->scan_param_cmpl.status==ESP_BT_STATUS_SUCCESS) {
            ble_scan_active=true;
            esp_ble_gap_start_scanning(12);
        }
    } else if(event==ESP_GAP_BLE_SCAN_RESULT_EVT) {
        if(param->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_RES_EVT && ble_device_count<12) {
            char label[32]={};
            std::uint8_t name_len=0;
            std::uint8_t* name=esp_ble_resolve_adv_data(param->scan_rst.ble_adv,ESP_BLE_AD_TYPE_NAME_CMPL,&name_len);
            if(name&&name_len) {
                const size_t n=std::min((size_t)name_len,sizeof(label)-1);
                std::memcpy(label,name,n); label[n]=0;
            } else {
                std::snprintf(label,sizeof(label),"%02X:%02X:%02X:%02X:%02X:%02X",
                    param->scan_rst.bda[0],param->scan_rst.bda[1],param->scan_rst.bda[2],
                    param->scan_rst.bda[3],param->scan_rst.bda[4],param->scan_rst.bda[5]);
            }
            bool duplicate=false;
            for(int i=0;i<ble_device_count;i++)if(std::strcmp(ble_devices[i],label)==0)duplicate=true;
            if(!duplicate){std::snprintf(ble_devices[ble_device_count++],sizeof(ble_devices[0]),"%.31s",label);ui_dirty=true;}
        } else if(param->scan_rst.search_evt==ESP_GAP_SEARCH_INQ_CMPL_EVT) {
            ble_scan_active=false;ui_dirty=true;
        }
    } else if(event==ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT) {
        ble_scan_active=false;ui_dirty=true;
    }
}
bool start_ble_scan() {
    if(!ble_ready) {
        esp_err_t e=esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
        if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
        esp_bt_controller_config_t cfg=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
        e=esp_bt_controller_init(&cfg);
        if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
        e=esp_bt_controller_enable(ESP_BT_MODE_BLE);
        if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
        e=esp_bluedroid_init();
        if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
        e=esp_bluedroid_enable();
        if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
        if(esp_ble_gap_register_callback(ble_gap_event)!=ESP_OK)return false;
        ble_ready=true;
    }
    ble_device_count=0;ble_device_scroll=0;
    esp_ble_scan_params_t params={};
    params.scan_type=BLE_SCAN_TYPE_ACTIVE;
    params.own_addr_type=BLE_ADDR_TYPE_PUBLIC;
    params.scan_filter_policy=BLE_SCAN_FILTER_ALLOW_ALL;
    params.scan_interval=0x50;
    params.scan_window=0x30;
    params.scan_duplicate=BLE_SCAN_DUPLICATE_DISABLE;
    return esp_ble_gap_set_scan_params(&params)==ESP_OK;
}
void stop_ble_scan() {
    if(ble_ready&&ble_scan_active)esp_ble_gap_stop_scanning();
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
bool connect_wifi_from_sd() {
    if(wifi_started) {
        wifi_ap_record_t current{};
        if(esp_wifi_sta_get_ap_info(&current)==ESP_OK)return true;
        esp_wifi_connect();
        for(int i=0;i<80;i++){if(esp_wifi_sta_get_ap_info(&current)==ESP_OK)return true;vTaskDelay(pdMS_TO_TICKS(250));}
        std::snprintf(store_message,sizeof(store_message),"WIFI RECONNECT FAILED");return false;
    }
    mkdir("/sdcard/MCAI-OS",0775);
    FILE* f=std::fopen("/sdcard/MCAI-OS/wifi.cfg","r");
    if(!f){std::snprintf(store_message,sizeof(store_message),"ADD WIFI.CFG ON SD");return false;}
    char ssid[33]={},password[65]={};
    bool ok=std::fgets(ssid,sizeof(ssid),f)&&std::fgets(password,sizeof(password),f); std::fclose(f);
    auto trim=[](char* s){size_t n=std::strlen(s);while(n&&(s[n-1]=='\n'||s[n-1]=='\r'))s[--n]=0;};
    trim(ssid);trim(password);
    if(!ok||!ssid[0]){std::snprintf(store_message,sizeof(store_message),"INVALID WIFI.CFG");return false;}
    esp_err_t e=nvs_flash_init();
    if(e==ESP_ERR_NVS_NO_FREE_PAGES||e==ESP_ERR_NVS_NEW_VERSION_FOUND){nvs_flash_erase();e=nvs_flash_init();}
    if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    e=esp_netif_init();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    e=esp_event_loop_create_default();if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    e=esp_wifi_init(&init);if(e!=ESP_OK&&e!=ESP_ERR_INVALID_STATE)return false;
    wifi_config_t cfg={};
    if(std::strlen(ssid)>sizeof(cfg.sta.ssid)-1||std::strlen(password)>sizeof(cfg.sta.password)-1){
        std::snprintf(store_message,sizeof(store_message),"WIFI CREDENTIAL TOO LONG");return false;
    }
    std::memcpy(cfg.sta.ssid,ssid,std::strlen(ssid));
    std::memcpy(cfg.sta.password,password,std::strlen(password));
    cfg.sta.threshold.authmode=WIFI_AUTH_OPEN;
    if(esp_wifi_set_mode(WIFI_MODE_STA)!=ESP_OK||esp_wifi_set_config(WIFI_IF_STA,&cfg)!=ESP_OK||
       esp_wifi_start()!=ESP_OK||esp_wifi_connect()!=ESP_OK){std::snprintf(store_message,sizeof(store_message),"WIFI START FAILED");return false;}
    wifi_started=true;
    for(int i=0;i<80;i++){wifi_ap_record_t ap{};if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK)return true;vTaskDelay(pdMS_TO_TICKS(250));}
    std::snprintf(store_message,sizeof(store_message),"WIFI CONNECT TIMEOUT");return false;
}
void refresh_store() {
    store_count=0;store_selected=0;store_scroll=0;catalogue_loaded=false;catalogue_length=0;catalogue_buffer[0]=0;
    if(!connect_wifi_from_sd())return;
    esp_http_client_config_t cfg={};
    cfg.url="https://raw.githubusercontent.com/betabeeswax/MCAI-OS-APPS/main/catalogue.json";
    cfg.event_handler=catalogue_http_event;cfg.timeout_ms=15000;cfg.buffer_size=2048;cfg.crt_bundle_attach=esp_crt_bundle_attach;
    esp_http_client_handle_t client=esp_http_client_init(&cfg);
    if(!client){std::snprintf(store_message,sizeof(store_message),"HTTP INIT FAILED");return;}
    esp_err_t result=esp_http_client_perform(client);
    int status=esp_http_client_get_status_code(client);esp_http_client_cleanup(client);
    if(result!=ESP_OK||status!=200){std::snprintf(store_message,sizeof(store_message),"GITHUB FETCH FAILED");return;}
    cJSON* root=cJSON_Parse(catalogue_buffer);
    cJSON* apps=root?cJSON_GetObjectItemCaseSensitive(root,"apps"):nullptr;
    if(!cJSON_IsArray(apps)){if(root)cJSON_Delete(root);std::snprintf(store_message,sizeof(store_message),"BAD CATALOGUE JSON");return;}
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
        if(cJSON_IsObject(package)){
            cJSON* url=cJSON_GetObjectItemCaseSensitive(package,"url");
            cJSON* hash=cJSON_GetObjectItemCaseSensitive(package,"sha256");
            cJSON* format=cJSON_GetObjectItemCaseSensitive(package,"format");
            cJSON* size=cJSON_GetObjectItemCaseSensitive(package,"sizeBytes");
            if(cJSON_IsString(url)&&cJSON_IsString(hash)&&cJSON_IsString(format)&&
               std::strcmp(format->valuestring,"mcai-app-v1")==0&&cJSON_IsNumber(size)&&
               size->valuedouble>0&&size->valuedouble<=4096&&std::strncmp(url->valuestring,"https://",8)==0){
                std::snprintf(store_urls[store_count],256,"%s",url->valuestring);
                std::snprintf(store_hashes[store_count],65,"%s",hash->valuestring);
                store_sizes[store_count]=(size_t)size->valuedouble;
            }
        }
        ++store_count;
    }
    cJSON_Delete(root);catalogue_loaded=true;
    std::snprintf(store_message,sizeof(store_message),"GITHUB: %d APPS",store_count);
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
      package_running=true;active_app=13;
    }
    cJSON_Delete(root);return valid;
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
void open_selected_file() {
    if(file_count==0)return;
    char path[240];std::snprintf(path,sizeof(path),"%s/%s",current_dir,file_names[file_selected]);
    if(file_is_dir[file_selected]){std::snprintf(current_dir,sizeof(current_dir),"%.159s",path);refresh_files();return;}
    FILE* f=std::fopen(path,"r");if(!f)return;
    const size_t n=std::fread(viewer_buffer,1,sizeof(viewer_buffer)-1,f);viewer_buffer[n]=0;std::fclose(f);viewing_file=true;
}
void render_app() {
    const auto white=rgb565(244,248,255), muted=rgb565(150,166,190), accent=rgb565(87,231,190);
    const auto bg=dark_theme?rgb565(11,16,29):rgb565(235,241,247);
    const auto ink=dark_theme?white:rgb565(20,30,45);
    std::fill(std::begin(framebuffer),std::end(framebuffer),bg);
    draw_text("MCAI",8,7,1,accent);draw_text("OS",39,10,1,muted);
    if(active_app==13)draw_text(package_title,100,7,1,ink);else draw_text(kApps[active_app],100,7,1,ink);
    fill_rect(0,19,kWidth,1,rgb565(44,59,78));
    char line[64];
    switch(active_app) {
    case 0:
        if(viewing_file){draw_text("FILE VIEWER",8,27,1,accent);for(int i=0;i<5;i++){char row[40]={};std::strncpy(row,viewer_buffer+i*36,36);draw_text(row,8,43+i*13,1,ink);}draw_text("BACKSPACE CLOSE",8,112,1,muted);}
        else {draw_text(current_dir,8,25,1,accent);for(int i=0;i<5;i++){int ix=file_scroll+i;if(ix>=file_count)break;std::snprintf(line,sizeof(line),"%c %.20s",file_is_dir[ix]?'>':' ',file_names[ix]);draw_text(line,8,42+i*14,1,ix==file_selected?accent:ink);}draw_text("UP/DOWN SELECT ENTER OPEN",8,112,1,muted);}
        break;
    case 1:
        if(!note_editing){
            draw_text("MY NOTES",8,25,1,accent);
            for(int i=0;i<6;i++){int ix=note_scroll+i;if(ix>=note_count)break;draw_text(note_files[ix],12,42+i*12,1,ix==note_selected?accent:ink);}
            draw_text("ENTER OPEN  N NEW NOTE",8,113,1,muted);
            draw_text(sd_ready?"SAVED TO SD":"INSERT SD CARD",8,124,1,muted);
        } else {
            const char* note_title=std::strrchr(note_filename,'/');
            draw_text(note_title?note_title+1:note_filename,8,25,1,accent);
            for(int i=0;i<6;i++){int pos=(note_scroll+i)*35;if(pos>=note_length)break;char row[36]={};std::strncpy(row,note_buffer+pos,35);draw_text(row,8,40+i*12,1,ink);}
            draw_text("TYPE TO EDIT  BKSP DELETE",8,113,1,muted);
            draw_text("ESC SAVE AND BACK",8,124,1,muted);
        }
        break;
    case 2:
        draw_text("CALCULATOR",8,27,1,accent);draw_text(calc_buffer,8,49,2,ink);
        draw_text("NUMBERS + - * / ( )",8,88,1,muted);draw_text("ENTER = RESULT",8,105,1,muted);break;
    case 3:
        if(bluetooth_screen){
            draw_text("BLUETOOTH SCAN",8,25,1,accent);
            draw_text(ble_scan_active?"SCANNING...":"SCAN COMPLETE",8,39,1,muted);
            for(int i=0;i<5;i++){int ix=ble_device_scroll+i;if(ix>=ble_device_count)break;draw_text(ble_devices[ix],10,55+i*12,1,ix==ble_device_scroll?accent:ink);}
            if(ble_device_count==0)draw_text("NO DEVICES FOUND YET",8,68,1,ink);
            draw_text("ENTER RESCAN  ESC BACK",8,119,1,muted);
        } else if(wifi_setup_step==1||wifi_setup_step==2){
            draw_text(wifi_setup_step==1?"WIFI NETWORK NAME":"WIFI PASSWORD",8,29,1,accent);
            if(wifi_setup_step==1)draw_text(wifi_ssid_input,8,52,1,ink);
            else {char hidden[65]={};for(int i=0;i<(int)std::strlen(wifi_password_input)&&i<64;i++)hidden[i]='*';draw_text(hidden,8,52,1,ink);}
            draw_text("TYPE USING KEYBOARD",8,78,1,muted);
            draw_text(wifi_setup_step==1?"ENTER: NEXT":"ENTER: SAVE AND CONNECT",8,98,1,muted);
            draw_text("ESC: CANCEL  BKSP: DELETE",8,116,1,muted);
        } else {
            draw_text("SETTINGS",8,25,1,accent);
            const char* rows[]={"BRIGHTNESS","THEME","WI-FI SETUP","BLUETOOTH","SOUNDS"};
            for(int i=0;i<5;i++){
                char item[40];
                if(i==0)std::snprintf(item,sizeof(item),"BRIGHTNESS: %s",setting_brightness==0?"LOW":setting_brightness==1?"MED":"HIGH");
                else if(i==1)std::snprintf(item,sizeof(item),"THEME: %s",dark_theme?"DARK":"LIGHT");
                else if(i==4)std::snprintf(item,sizeof(item),"SOUNDS: %s",sound_enabled?"ON":"OFF");
                else std::snprintf(item,sizeof(item),"%s",rows[i]);
                draw_text(item,12,43+i*15,1,i==settings_selection?accent:ink);
            }
            draw_text("UP/DOWN SELECT  LEFT/RIGHT CHANGE",8,111,1,muted);
            draw_text("ENTER OPEN  ESC BACK",8,123,1,muted);
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
        for(int i=0;i<4;i++){int ix=store_scroll+i;if(ix>=store_count)break;char row[40];std::snprintf(row,sizeof(row),"%c %.20s %s",ix==store_selected?'>':' ',store_names[ix],store_builtin_index(store_ids[ix])>=0?"BUILT-IN":(package_installed(store_ids[ix])?"INSTALLED":(std::strcmp(store_status[ix],"available")==0?"AVAILABLE":(std::strcmp(store_status[ix],"experimental")==0?"EXPERIMENTAL":"PLANNED"))));draw_text(row,8,54+i*13,1,ix==store_selected?accent:ink);}
        draw_text(store_message,8,110,1,muted);draw_text("UP/DOWN ENTER INFO",8,122,1,muted);break;
    case 6: {
        std::time_t now=std::time(nullptr);std::tm tmv{};localtime_r(&now,&tmv);
        if(tmv.tm_year<120){draw_text("TIME NOT SYNCED",8,33,2,accent);draw_text("SET WIFI IN SETTINGS",8,67,1,ink);}
        else {std::snprintf(line,sizeof(line),"%02d:%02d:%02d",tmv.tm_hour,tmv.tm_min,tmv.tm_sec);draw_text("SYSTEM CLOCK",8,30,1,accent);draw_text(line,8,55,3,ink);}
        draw_text("ESC: BACK",8,111,1,muted);break;}
    case 7: {
        int left=timer_seconds;if(timer_running)left=(int)std::max<std::int64_t>(0,(timer_end_us-esp_timer_get_time()+999999)/1000000);
        std::snprintf(line,sizeof(line),"%02d:%02d",left/60,left%60);draw_text("COUNTDOWN TIMER",8,27,1,accent);draw_text(line,8,49,3,ink);
        draw_text(timer_running?"RUNNING":"PAUSED",8,91,1,muted);draw_text("UP/DOWN 10 SEC ENTER START",8,110,1,muted);break;}
    case 8:
        draw_text("KEYBOARD TEST",8,27,1,accent);std::snprintf(line,sizeof(line),"LAST KEY: %c",last_char);draw_text(line,8,49,1,ink);
        std::snprintf(line,sizeof(line),"KEY EVENTS: %lu",(unsigned long)keypress_count);draw_text(line,8,69,1,ink);
        draw_text("TYPE ANY KEY TO TEST",8,95,1,muted);break;
    case 9: {
        if(sd_ready){draw_text("SD CARD MOUNTED",8,35,1,accent);draw_text("FILESYSTEM FAT",8,54,1,ink);draw_text("USE FILE MANAGER",8,73,1,muted);}else draw_text("SD CARD NOT MOUNTED",8,43,1,ink);break;}
    case 10:
        draw_text(keyboard_ready?"KEYBOARD PASS":"KEYBOARD FAIL",8,30,1,ink);draw_text(sd_ready?"SD MOUNT PASS":"SD MOUNT FAIL",8,48,1,ink);
        std::snprintf(line,sizeof(line),"FREE HEAP %lu",(unsigned long)esp_get_free_heap_size());draw_text(line,8,66,1,ink);
        std::snprintf(line,sizeof(line),"UPTIME %lu SEC",(unsigned long)(esp_timer_get_time()/1000000));draw_text(line,8,84,1,ink);break;
    case 13: {
        char count[32];std::snprintf(count,sizeof(count),"%d",package_counter);
        draw_text(package_subtitle,8,30,1,muted);draw_text(count,8,55,3,accent);
        draw_text("ENTER: +1",8,94,1,ink);draw_text("BACKSPACE: STORE",8,111,1,muted);break;
    }
    case 11:
        draw_text("SNAKE - ARROWS TO MOVE",8,25,1,accent);
        if(!game_started){draw_text("ENTER TO START",8,58,1,ink);draw_text("EAT FOOD / AVOID WALLS",8,78,1,muted);}
        else {for(int i=0;i<snake_len;i++)fill_rect(16+snake_x[i]*9,38+snake_y[i]*8,7,6,accent);fill_rect(16+food_x*9,38+food_y*8,7,6,rgb565(255,100,100));std::snprintf(line,sizeof(line),"SCORE %d",game_score);draw_text(line,8,116,1,ink);if(game_over)draw_text("GAME OVER - ENTER",90,116,1,accent);}
        break;
    default:
        draw_text("MCAI-OS FOR CARDPUTER ADV",8,37,1,ink);draw_text("VERSION 0.2.0",8,57,1,accent);draw_text("BUILT-IN APP SUITE",8,77,1,muted);break;
    }
    fill_rect(0,122,kWidth,1,rgb565(44,59,78));draw_text("BACKSPACE: HOME",8,126,1,muted);
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel,0,0,kWidth,kHeight,framebuffer));
}

void render_home() {
    const auto bg=rgb565(11,16,29), card=rgb565(22,31,49), accent=rgb565(87,231,190);
    const auto white=rgb565(244,248,255), muted=rgb565(150,166,190), selected=rgb565(37,68,75);
    if(active_app>=0) { render_app(); return; }
    std::fill(std::begin(framebuffer),std::end(framebuffer),bg);
    fill_rect(0,0,kWidth,3,accent); draw_text("MCAI",8,7,2,accent); draw_text("OS",57,14,1,white);
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
    // Match M5Stack's Cardputer ADV TCA8418 setup: GPIO defaults, 7x8
    // matrix, clear stale FIFO/status, then enable keypad + GPIO interrupts.
    const std::uint8_t setup[][2] = {
        {0x23,0x00},{0x24,0x00},{0x25,0x00}, // GPIO direction: inputs
        {0x20,0xFF},{0x21,0xFF},{0x22,0xFF}, // GPI event mode
        {0x26,0x00},{0x27,0x00},{0x28,0x00}, // falling-edge interrupt mode
        {0x1A,0xFF},{0x1B,0xFF},{0x1C,0xFF}, // GPIO interrupts
        {0x1D,0x7F},{0x1E,0xFF},{0x1F,0x00}  // 7 rows x 8 columns
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
    keyboard_read(0x11,&stale); keyboard_read(0x12,&stale); keyboard_read(0x13,&stale);
    keyboard_write(0x02,0x03); // clear keypad + GPIO interrupt status
    if(keyboard_write(0x01,0x03)!=ESP_OK) {
        ESP_LOGW(kTag,"TCA8418 interrupt enable failed"); return;
    }
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
    cfg.sample_rate=16000;
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
    constexpr std::uint32_t rate=16000;
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

char decode_key(int row, int col) {
    static const unsigned char keys[4][14] = {
        {126,49,50,51,52,53,54,55,56,57,48,45,61,8},
        {9,113,119,101,114,116,121,117,105,111,112,91,93,92},
        {0,0,97,115,100,102,103,104,106,107,108,59,39,10},
        {0,0,0,122,120,99,118,98,110,109,44,46,47,32}
    };
    return keys[row][col] ? static_cast<char>(keys[row][col]) : 0;
}

void poll_keyboard() {
    if(!keyboard_ready)return;
    std::uint8_t count=0;if(keyboard_read(0x03,&count)!=ESP_OK)return;count&=0x0F;
    while(count--){
        std::uint8_t event=0;if(keyboard_read(0x04,&event)!=ESP_OK||event==0)break;
        if((event&0x80)==0)continue; // TCA8418: bit 7 = key press, 0 = release.
        const std::uint8_t key=event&0x7F;
        ui_dirty=true;
        if(audio_ready&&sound_enabled)play_tone(1250,10);
        const int ix=key>0?key-1:0, rr=ix/10, rc=ix%10;
        const int col=rr*2+(rc>3?1:0),row=(rc+4)%4;
        char ch=(row>=0&&row<4&&col>=0&&col<14)?decode_key(row,col):0;
        // The ADV's Aa key is Caps Lock; Fn changes the alternate key layer.
        if(key==KEY_CAPS){caps_lock=!caps_lock;ch=0;}
        else if(key==KEY_FN){fn_lock=!fn_lock;ch=0;}
        if(key==KEY_UP||key==KEY_DOWN||key==KEY_LEFT||key==KEY_RIGHT||
           key==KEY_ESC||key==KEY_BACKSPACE||key==KEY_ENTER) ch=0;
        if(ch>='a'&&ch<='z'&&caps_lock) ch=static_cast<char>(ch-'a'+'A');
        if(fn_lock){
            const char* normal="1234567890-=";
            const char* shifted="!@#$%^&*()_+";
            for(int i=0;normal[i];++i) if(ch==normal[i]) ch=shifted[i];
        }
        last_char=ch?ch:(key==KEY_UP?'^':key==KEY_DOWN?'v':key==KEY_LEFT?'<':key==KEY_RIGHT?'>':'?');++keypress_count;
        if(active_app<0){
            if(is_up(key)||is_left(key))selected_app=(selected_app+kAppCount-1)%kAppCount;
            else if(is_down(key)||is_right(key))selected_app=(selected_app+1)%kAppCount;
            else if(key==KEY_ENTER){active_app=selected_app;if(active_app==0){std::snprintf(current_dir,sizeof(current_dir),"/sdcard");refresh_files();}if(active_app==1){note_editing=false;refresh_notes();}if(active_app==5){refresh_store();}if(active_app==6){if(connect_wifi_from_sd())start_time_sync();}}
            continue;
        }
        if(active_app==0){
            if(viewing_file){if(is_back(key))viewing_file=false;else if(is_up(key)||is_left(key))file_scroll=std::max(0,file_scroll-1);else if(is_down(key)||is_right(key))++file_scroll;}
            else if(is_back(key)){if(std::strcmp(current_dir,"/sdcard")!=0){char* slash=std::strrchr(current_dir,'/');if(slash&&slash!=current_dir)*slash=0;else std::strcpy(current_dir,"/sdcard");refresh_files();}else active_app=-1;}
            else if(is_up(key)||is_left(key)){if(file_selected>0)--file_selected;if(file_selected<file_scroll)file_scroll=file_selected;}
            else if(is_down(key)||is_right(key)){if(file_selected+1<file_count)++file_selected;if(file_selected>=file_scroll+5)file_scroll=file_selected-4;}
            else if(key==KEY_ENTER)open_selected_file();
        } else if(active_app==1){
            if(!note_editing){
                if(is_up(key)||is_left(key)){if(note_selected>0)--note_selected;if(note_selected<note_scroll)note_scroll=note_selected;}
                else if(is_down(key)||is_right(key)){if(note_selected+1<note_count)++note_selected;if(note_selected>=note_scroll+6)note_scroll=note_selected-5;}
                else if(key==KEY_ENTER)open_note(note_selected);
                else if(ch=='n'||ch=='N')create_note();
                else if(key==KEY_BACKSPACE||key==KEY_ESC)active_app=-1;
            } else {
                if(key==KEY_ESC){save_note();note_editing=false;refresh_notes();}
                else if(key==KEY_BACKSPACE){if(note_length>0){--note_length;note_buffer[note_length]=0;save_note();}}
                else if(key==KEY_ENTER&&note_length<(int)sizeof(note_buffer)-2){note_buffer[note_length++]='\n';note_buffer[note_length]=0;note_scroll=std::max(0,note_length/35-5);save_note();}
                else if(ch>=32&&ch<127&&note_length<(int)sizeof(note_buffer)-2){note_buffer[note_length++]=ch;note_buffer[note_length]=0;note_scroll=std::max(0,note_length/35-5);save_note();}
            }
        } else if(active_app==2){
            if(key==KEY_ESC)active_app=-1;
            else if(key==KEY_BACKSPACE){if(calc_length>0)calc_buffer[--calc_length]=0;}
            else if(key==KEY_ENTER)calculate();
            else if(ch>=32&&ch<127&&calc_length<(int)sizeof(calc_buffer)-2){calc_buffer[calc_length++]=ch;calc_buffer[calc_length]=0;}
        } else if(active_app==3){
            if(bluetooth_screen){
                if(key==KEY_ESC||key==KEY_BACKSPACE){stop_ble_scan();bluetooth_screen=false;}
                else if(key==KEY_ENTER){stop_ble_scan();start_ble_scan();}
                else if(is_up(key)){ble_device_scroll=std::max(0,ble_device_scroll-1);}
                else if(is_down(key)){ble_device_scroll=std::min(std::max(0,ble_device_count-5),ble_device_scroll+1);}
            } else if(wifi_setup_step>0){
                if(key==KEY_ESC){wifi_setup_step=0;}
                else if(key==KEY_BACKSPACE){
                    if(wifi_setup_step==1&&std::strlen(wifi_ssid_input)>0)wifi_ssid_input[std::strlen(wifi_ssid_input)-1]=0;
                    else if(wifi_setup_step==2&&std::strlen(wifi_password_input)>0)wifi_password_input[std::strlen(wifi_password_input)-1]=0;
                } else if(key==KEY_ENTER){
                    if(wifi_setup_step==1){wifi_setup_step=2;}
                    else {
                        if(save_wifi_settings()){
                            wifi_setup_step=0;
                            if(connect_wifi_from_sd())start_time_sync();
                            std::snprintf(store_message,sizeof(store_message),"WIFI SETTINGS SAVED");
                        } else std::snprintf(store_message,sizeof(store_message),"WIFI SAVE FAILED");
                    }
                } else if(ch>=32&&ch<127){
                    if(wifi_setup_step==1&&std::strlen(wifi_ssid_input)<32){size_t n=std::strlen(wifi_ssid_input);wifi_ssid_input[n]=ch;wifi_ssid_input[n+1]=0;}
                    else if(wifi_setup_step==2&&std::strlen(wifi_password_input)<64){size_t n=std::strlen(wifi_password_input);wifi_password_input[n]=ch;wifi_password_input[n+1]=0;}
                }
            } else if(is_up(key)||is_down(key)){settings_selection=(settings_selection+(is_up(key)?4:1))%5;}
            else if(is_left(key)||is_right(key)){
                if(settings_selection==0){setting_brightness=std::max(0,std::min(2,setting_brightness+(is_right(key)?1:-1)));apply_brightness();save_settings();}
                else if(settings_selection==1){dark_theme=!dark_theme;save_settings();}
                else if(settings_selection==4){sound_enabled=!sound_enabled;save_settings();}
            } else if(key==KEY_ENTER){
                if(settings_selection==1){dark_theme=!dark_theme;save_settings();}
                else if(settings_selection==2){load_wifi_settings();wifi_setup_step=1;}
                else if(settings_selection==3){bluetooth_screen=true;if(!start_ble_scan())std::snprintf(store_message,sizeof(store_message),"BLE START FAILED");}
                else if(settings_selection==4){sound_enabled=!sound_enabled;save_settings();}
            } else if(key==KEY_ESC)active_app=-1;
        } else if(active_app==5){
            if(is_back(key))active_app=-1;
            else if(is_up(key)||is_left(key)){if(store_selected>0)--store_selected;if(store_selected<store_scroll)store_scroll=store_selected;}
            else if(is_down(key)||is_right(key)){if(store_selected+1<store_count)++store_selected;if(store_selected>=store_scroll+4)store_scroll=store_selected-3;}
            else if(key==KEY_ENTER){
                if(store_count<=0){std::snprintf(store_message,sizeof(store_message),"CATALOGUE EMPTY");}
                else {
                    const int builtin=store_builtin_index(store_ids[store_selected]);
                    if(builtin>=0){
                        active_app=builtin;
                        if(builtin==0){std::snprintf(current_dir,sizeof(current_dir),"/sdcard");refresh_files();}
                        if(builtin==1){note_editing=false;refresh_notes();}
                        if(builtin==5)refresh_store();
                        if(builtin==6&&connect_wifi_from_sd())start_time_sync();
                    } else if(package_installed(store_ids[store_selected])) {
                        if(!launch_installed_package(store_ids[store_selected]))std::snprintf(store_message,sizeof(store_message),"APP LOAD FAILED");
                    } else install_store_package();
                }
            }
        } else if(active_app==13){
            if(is_back(key)){package_running=false;active_app=5;}
            else if(key==KEY_ENTER){package_counter=std::min(999999,package_counter+package_increment);}
        } else if(active_app==6){
            if(key==KEY_ESC||key==KEY_BACKSPACE)active_app=-1;
        } else if(active_app==7){
            if(is_up(key)||is_left(key)){timer_seconds=std::max(0,timer_seconds-10);if(timer_running)timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;}
            else if(is_down(key)||is_right(key)){timer_seconds=std::min(5999,timer_seconds+10);if(timer_running)timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;}
            else if(key==KEY_ENTER){if(timer_running){timer_running=false;timer_seconds=std::max(0,(int)((timer_end_us-esp_timer_get_time()+999999)/1000000));}else{timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;timer_running=true;}}
        } else if(active_app==8){/* Every key press is counted above. */}
        else if(active_app==11){
            if(is_back(key)){active_app=-1;game_started=false;}
            else if(!game_started&&key==KEY_ENTER){game_started=true;game_over=false;snake_len=3;snake_x[0]=10;snake_y[0]=4;snake_x[1]=9;snake_y[1]=4;snake_x[2]=8;snake_y[2]=4;snake_dx=1;snake_dy=0;food_x=14;food_y=4;game_score=0;game_tick_us=esp_timer_get_time();}
            else if(is_up(key)&&snake_dy==0){snake_dx=0;snake_dy=-1;}
            else if(is_down(key)&&snake_dy==0){snake_dx=0;snake_dy=1;}
            else if(is_left(key)&&snake_dx==0){snake_dx=-1;snake_dy=0;}
            else if(is_right(key)&&snake_dx==0){snake_dx=1;snake_dy=0;}
            else if(game_over&&key==KEY_ENTER)game_started=false;
        }
        if(is_back(key)&&active_app!=0&&active_app!=1&&active_app!=2&&active_app!=5&&active_app!=6&&active_app!=11)active_app=-1;
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
    init_keyboard();init_audio();init_sd_card();load_settings();seed_store();apply_brightness();render_home();play_tone(660,90);
    for(;;){
        const int old_app=active_app,old_sel=selected_app;
        poll_keyboard();
        const bool boot_pressed=gpio_get_level(GPIO_NUM_0)==0;
        if(boot_pressed&&!boot_button_was_pressed){
            if(active_app>=0){if(active_app==1)save_note();active_app=-1;viewing_file=false;}
            else {active_app=selected_app;if(active_app==0){std::strcpy(current_dir,"/sdcard");refresh_files();}if(active_app==1){note_editing=false;refresh_notes();}if(active_app==5){seed_store();refresh_store();}}
        }
        boot_button_was_pressed=boot_pressed;
        const std::int64_t now=esp_timer_get_time();
        if(timer_running&&now>=timer_end_us){timer_running=false;timer_seconds=0;ui_dirty=true;play_tone(880,180);play_tone(1175,180);play_tone(1568,260);}
        if(active_app==11&&game_started&&!game_over&&now-game_tick_us>220000){
            game_tick_us=now;
            for(int i=snake_len-1;i>0;i--){snake_x[i]=snake_x[i-1];snake_y[i]=snake_y[i-1];}
            snake_x[0]+=snake_dx;snake_y[0]+=snake_dy;
            if(snake_x[0]<0||snake_x[0]>=23||snake_y[0]<0||snake_y[0]>=9)game_over=true;
            for(int i=1;i<snake_len;i++)if(snake_x[0]==snake_x[i]&&snake_y[0]==snake_y[i])game_over=true;
            if(snake_x[0]==food_x&&snake_y[0]==food_y){if(snake_len<47)++snake_len;++game_score;food_x=(food_x+7)%23;food_y=(food_y+3)%9;}
        }
        if(old_app!=active_app||old_sel!=selected_app||active_app==6||active_app==7||active_app==11||active_app==5||ui_dirty){render_home();ui_dirty=false;}
        if(active_app==1&&note_editing&&note_length>0&&note_length%24==0)save_note();
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}
