#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/sdspi_host.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "nvs_flash.h"
#include "cJSON.h"
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
constexpr char kSdMountPoint[] = "/sdcard";

esp_lcd_panel_handle_t panel = nullptr;
sdmmc_card_t* sd_card = nullptr;
bool keyboard_ready = false;
bool sd_ready = false;
int selected_app = 0;
int active_app = -1;
int setting_brightness = 1;
char calc_buffer[48] = {};
int calc_length = 0;
int file_scroll = 0;
char note_buffer[512] = {};
int note_length = 0, note_scroll = 0;
char last_char = '-';
unsigned keypress_count = 0;
char current_dir[160] = "/sdcard";
char file_names[32][64] = {};
bool file_is_dir[32] = {};
int file_count = 0, file_selected = 0;
char viewer_buffer[700] = {};
bool viewing_file = false;
char store_names[106][64] = {}; char store_status[106][20] = {};
int store_count=0, store_selected=0, store_scroll=0;
char store_message[40]="ADD WIFI.CFG ON SD"; bool wifi_started=false, catalogue_loaded=false;
char catalogue_buffer[32768]={}; size_t catalogue_length=0;
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
    const std::uint32_t duty = setting_brightness==0 ? 900U : setting_brightness==1 ? 4096U : 8191U;
    ledc_set_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0,duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE,LEDC_CHANNEL_0);
}
void save_settings() {
    if(!sd_ready) return;
    mkdir("/sdcard/MCAI-OS",0775);
    FILE* f=std::fopen("/sdcard/MCAI-OS/settings.cfg","w");
    if(f){std::fprintf(f,"%d %d\n",setting_brightness,dark_theme?1:0);std::fclose(f);}
}
void load_settings() {
    if(!sd_ready) return;
    FILE* f=std::fopen("/sdcard/MCAI-OS/settings.cfg","r");
    if(f){int b=1,t=1;if(std::fscanf(f,"%d %d",&b,&t)>=1){setting_brightness=std::max(0,std::min(2,b));dark_theme=t!=0;}std::fclose(f);}
}
void save_note() {
    if(!sd_ready) return;
    mkdir("/sdcard/MCAI-OS",0775);
    FILE* f=std::fopen("/sdcard/MCAI-OS/notes.txt","w");
    if(f){std::fwrite(note_buffer,1,note_length,f);std::fclose(f);}
}
void load_note() {
    note_length=0; note_buffer[0]=0;
    if(!sd_ready) return;
    FILE* f=std::fopen("/sdcard/MCAI-OS/notes.txt","r");
    if(f){note_length=(int)std::fread(note_buffer,1,sizeof(note_buffer)-1,f);note_buffer[note_length]=0;std::fclose(f);}
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
    if(!eval_expr(p,result)){std::snprintf(calc_buffer,sizeof(calc_buffer),"INVALID / DIV0");return;}
    while(*p==' ')++p;if(*p){std::snprintf(calc_buffer,sizeof(calc_buffer),"INVALID INPUT");return;}
    std::snprintf(calc_buffer,sizeof(calc_buffer),"%.8g",result);
}
void refresh_files() {
    file_count=0; file_selected=0;
    DIR* d=opendir(current_dir); if(!d)return;
    struct dirent* e;
    while((e=readdir(d))!=nullptr && file_count<32) {
        if(e->d_name[0]=='.')continue;
        std::snprintf(file_names[file_count],sizeof(file_names[file_count]),"%s",e->d_name);
        char full[240];std::snprintf(full,sizeof(full),"%s/%s",current_dir,e->d_name);
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
    std::snprintf((char*)cfg.sta.ssid,sizeof(cfg.sta.ssid),"%s",ssid);
    std::snprintf((char*)cfg.sta.password,sizeof(cfg.sta.password),"%s",password);
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
        if(store_count>=106)break;
        cJSON* name=cJSON_GetObjectItemCaseSensitive(app,"name");
        cJSON* status_item=cJSON_GetObjectItemCaseSensitive(app,"status");
        if(!cJSON_IsString(name))continue;
        std::snprintf(store_names[store_count],64,"%s",name->valuestring);
        std::snprintf(store_status[store_count],20,"%s",cJSON_IsString(status_item)?status_item->valuestring:"unknown");
        ++store_count;
    }
    cJSON_Delete(root);catalogue_loaded=true;
    std::snprintf(store_message,sizeof(store_message),"GITHUB: %d APPS",store_count);
}
void install_store_package() {
    if(!catalogue_loaded||store_count==0)return;
    if(std::strcmp(store_status[store_selected],"available")!=0){std::snprintf(store_message,sizeof(store_message),"NOT RELEASED YET");return;}
    std::snprintf(store_message,sizeof(store_message),"PACKAGE NOT PUBLISHED");
}
void seed_store() { /* Store entries are fetched from the official GitHub catalogue. */ }
void open_selected_file() {
    if(file_count==0)return;
    char path[240];std::snprintf(path,sizeof(path),"%s/%s",current_dir,file_names[file_selected]);
    if(file_is_dir[file_selected]){std::snprintf(current_dir,sizeof(current_dir),"%s",path);refresh_files();return;}
    FILE* f=std::fopen(path,"r");if(!f)return;
    const size_t n=std::fread(viewer_buffer,1,sizeof(viewer_buffer)-1,f);viewer_buffer[n]=0;std::fclose(f);viewing_file=true;
}
void render_app() {
    const auto white=rgb565(244,248,255), muted=rgb565(150,166,190), accent=rgb565(87,231,190);
    const auto bg=dark_theme?rgb565(11,16,29):rgb565(235,241,247);
    const auto ink=dark_theme?white:rgb565(20,30,45);
    std::fill(std::begin(framebuffer),std::end(framebuffer),bg);
    draw_text("MCAI-OS",8,7,1,accent); draw_text(kApps[active_app],100,7,1,ink);
    fill_rect(0,19,kWidth,1,rgb565(44,59,78));
    char line[64];
    switch(active_app) {
    case 0:
        if(viewing_file){draw_text("FILE VIEWER",8,27,1,accent);for(int i=0;i<5;i++){char row[40]={};std::strncpy(row,viewer_buffer+i*36,36);draw_text(row,8,43+i*13,1,ink);}draw_text("BACKSPACE CLOSE",8,112,1,muted);}
        else {draw_text(current_dir,8,25,1,accent);for(int i=0;i<5;i++){int ix=file_scroll+i;if(ix>=file_count)break;std::snprintf(line,sizeof(line),"%c %.20s",file_is_dir[ix]?'>':' ',file_names[ix]);draw_text(line,8,42+i*14,1,ix==file_selected?accent:ink);}draw_text("UP/DOWN SELECT ENTER OPEN",8,112,1,muted);}
        break;
    case 1:
        draw_text("NOTES - AUTO SAVES",8,25,1,accent);
        for(int i=0;i<5;i++){int pos=(note_scroll+i)*35;if(pos>=note_length)break;char row[36]={};std::strncpy(row,note_buffer+pos,35);draw_text(row,8,43+i*13,1,ink);}
        draw_text(sd_ready?"SAVED ON SD":"NO SD - NOT SAVED",8,112,1,muted);break;
    case 2:
        draw_text("CALCULATOR",8,27,1,accent);draw_text(calc_buffer,8,49,2,ink);
        draw_text("NUMBERS + - * / ( )",8,88,1,muted);draw_text("ENTER = RESULT",8,105,1,muted);break;
    case 3:
        draw_text("DISPLAY BRIGHTNESS",8,28,1,ink);draw_text(setting_brightness==0?"LOW":setting_brightness==1?"MEDIUM":"HIGH",8,48,2,accent);
        draw_text("THEME",8,82,1,ink);draw_text(dark_theme?"DARK":"LIGHT",8,99,1,accent);draw_text("ARROWS CHANGE ENTER THEME",8,116,1,muted);break;
    case 4:
        std::snprintf(line,sizeof(line),"CPU ESP32-S3");draw_text(line,8,30,1,ink);
        std::snprintf(line,sizeof(line),"UPTIME %lu SEC",(unsigned long)(esp_timer_get_time()/1000000));draw_text(line,8,48,1,ink);
        std::snprintf(line,sizeof(line),"HEAP %lu KB",(unsigned long)(esp_get_free_heap_size()/1024));draw_text(line,8,66,1,ink);
        draw_text("M5STACK CARDPUTER ADV",8,84,1,accent);break;
    case 5:
        draw_text("GITHUB APP STORE",8,25,1,accent);
        draw_text(catalogue_loaded?"MCAI-OS-APPS / MAIN":"GITHUB CATALOGUE",8,39,1,ink);
        for(int i=0;i<4;i++){int ix=store_scroll+i;if(ix>=store_count)break;char row[40];std::snprintf(row,sizeof(row),"%c %.20s %s",ix==store_selected?'>':' ',store_names[ix],std::strcmp(store_status[ix],"available")==0?"AVAILABLE":"PLANNED");draw_text(row,8,54+i*13,1,ix==store_selected?accent:ink);}
        draw_text(store_message,8,110,1,muted);draw_text("UP/DOWN ENTER INFO",8,122,1,muted);break;
    case 6: {
        std::time_t now=std::time(nullptr);std::tm tmv{};localtime_r(&now,&tmv);
        if(tmv.tm_year<120){draw_text("CLOCK NOT SET",8,33,2,accent);draw_text("TYPE HHMMSS THEN ENTER",8,72,1,ink);draw_text(clock_input,8,91,2,ink);}
        else {std::snprintf(line,sizeof(line),"%02d:%02d:%02d",tmv.tm_hour,tmv.tm_min,tmv.tm_sec);draw_text("SYSTEM CLOCK",8,30,1,accent);draw_text(line,8,55,3,ink);}
        draw_text("ENTER SET TIME WHEN TYPED",8,111,1,muted);break;}
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
    case 11:
        draw_text("SNAKE - WASD TO MOVE",8,25,1,accent);
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
        const int ix=key>0?key-1:0, rr=ix/10, rc=ix%10;
        const int col=rr*2+(rc>3?1:0),row=(rc+4)%4;
        const char ch=(row>=0&&row<4&&col>=0&&col<14)?decode_key(row,col):0;
        last_char=ch?ch:'?';++keypress_count;
        if(active_app<0){
            if(key==53||key==44)selected_app=(selected_app+kAppCount-1)%kAppCount;
            else if(key==48||key==54)selected_app=(selected_app+1)%kAppCount;
            else if(key==67){active_app=selected_app;if(active_app==0){std::snprintf(current_dir,sizeof(current_dir),"/sdcard");refresh_files();}if(active_app==1)load_note();if(active_app==5){refresh_store();}}
            continue;
        }
        if(active_app==0){
            if(viewing_file){if(key==65)viewing_file=false;else if(key==53||key==44)file_scroll=std::max(0,file_scroll-1);else if(key==48||key==54)++file_scroll;}
            else if(key==65){if(std::strcmp(current_dir,"/sdcard")!=0){char* slash=std::strrchr(current_dir,'/');if(slash&&slash!=current_dir)*slash=0;else std::strcpy(current_dir,"/sdcard");refresh_files();}else active_app=-1;}
            else if(key==53||key==44){if(file_selected>0)--file_selected;if(file_selected<file_scroll)file_scroll=file_selected;}
            else if(key==48||key==54){if(file_selected+1<file_count)++file_selected;if(file_selected>=file_scroll+5)file_scroll=file_selected-4;}
            else if(key==67)open_selected_file();
        } else if(active_app==1){
            if(key==65){if(note_length>0){--note_length;note_buffer[note_length]=0;save_note();}}
            else if(key==67&&note_length<(int)sizeof(note_buffer)-2){note_buffer[note_length++]='\n';note_buffer[note_length]=0;save_note();}
            else if(ch>=32&&ch<127&&note_length<(int)sizeof(note_buffer)-2){note_buffer[note_length++]=ch;note_buffer[note_length]=0;save_note();}
        } else if(active_app==2){
            if(key==65){if(calc_length>0)calc_buffer[--calc_length]=0;}
            else if(key==67)calculate();
            else if(ch>=32&&ch<127&&calc_length<(int)sizeof(calc_buffer)-2){calc_buffer[calc_length++]=ch;calc_buffer[calc_length]=0;}
        } else if(active_app==3){
            if(key==53||key==44){setting_brightness=std::max(0,setting_brightness-1);apply_brightness();save_settings();}
            else if(key==48||key==54){setting_brightness=std::min(2,setting_brightness+1);apply_brightness();save_settings();}
            else if(key==67){dark_theme=!dark_theme;save_settings();}
        } else if(active_app==5){
            if(key==65)active_app=-1;
            else if(key==53||key==44){if(store_selected>0)--store_selected;if(store_selected<store_scroll)store_scroll=store_selected;}
            else if(key==48||key==54){if(store_selected+1<store_count)++store_selected;if(store_selected>=store_scroll+4)store_scroll=store_selected-3;}
            else if(key==67)install_store_package();
        } else if(active_app==6){
            if(key==65){if(clock_len>0)clock_input[--clock_len]=0;else active_app=-1;}
            else if(ch>='0'&&ch<='9'&&clock_len<6){clock_input[clock_len++]=ch;clock_input[clock_len]=0;}
            else if(key==67&&clock_len==6){int hh=(clock_input[0]-'0')*10+clock_input[1]-'0',mm=(clock_input[2]-'0')*10+clock_input[3]-'0',ss=(clock_input[4]-'0')*10+clock_input[5]-'0';if(hh<24&&mm<60&&ss<60){struct timeval tv{};tv.tv_sec=(std::time(nullptr)/86400)*86400+hh*3600+mm*60+ss;settimeofday(&tv,nullptr);clock_len=0;clock_input[0]=0;}}
        } else if(active_app==7){
            if(key==53||key==44){timer_seconds=std::max(0,timer_seconds-10);if(timer_running)timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;}
            else if(key==48||key==54){timer_seconds=std::min(5999,timer_seconds+10);if(timer_running)timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;}
            else if(key==67){if(timer_running){timer_running=false;timer_seconds=std::max(0,(int)((timer_end_us-esp_timer_get_time()+999999)/1000000));}else{timer_end_us=esp_timer_get_time()+(std::int64_t)timer_seconds*1000000;timer_running=true;}}
        } else if(active_app==8){/* Every key press is counted above. */}
        else if(active_app==11){
            if(key==65){active_app=-1;game_started=false;}
            else if(!game_started&&key==67){game_started=true;game_over=false;snake_len=3;snake_x[0]=10;snake_y[0]=4;snake_x[1]=9;snake_y[1]=4;snake_x[2]=8;snake_y[2]=4;snake_dx=1;snake_dy=0;food_x=14;food_y=4;game_score=0;game_tick_us=esp_timer_get_time();}
            else if(ch=='w'&&snake_dy==0){snake_dx=0;snake_dy=-1;}
            else if(ch=='s'&&snake_dy==0){snake_dx=0;snake_dy=1;}
            else if(ch=='a'&&snake_dx==0){snake_dx=-1;snake_dy=0;}
            else if(ch=='d'&&snake_dx==0){snake_dx=1;snake_dy=0;}
            else if(game_over&&key==67)game_started=false;
        }
        if(key==65&&active_app!=0&&active_app!=1&&active_app!=2&&active_app!=6&&active_app!=11)active_app=-1;
    }
    std::uint8_t status=0;if(keyboard_read(0x02,&status)==ESP_OK&&(status&0x01))keyboard_write(0x02,0x01);
}} // namespace

extern "C" void app_main(void) {
    ESP_LOGI(kTag,"Starting MCAI-OS Cardputer ADV");
    init_display();
    ledc_timer_config_t timer={};timer.speed_mode=LEDC_LOW_SPEED_MODE;timer.duty_resolution=LEDC_TIMER_13_BIT;timer.timer_num=LEDC_TIMER_0;timer.freq_hz=5000;timer.clk_cfg=LEDC_AUTO_CLK;ESP_ERROR_CHECK(ledc_timer_config(&timer));
    ledc_channel_config_t channel={};channel.gpio_num=kLcdBacklight;channel.speed_mode=LEDC_LOW_SPEED_MODE;channel.channel=LEDC_CHANNEL_0;channel.intr_type=LEDC_INTR_DISABLE;channel.timer_sel=LEDC_TIMER_0;channel.duty=4096;channel.hpoint=0;ESP_ERROR_CHECK(ledc_channel_config(&channel));
    init_keyboard();init_sd_card();load_settings();load_note();seed_store();apply_brightness();render_home();
    for(;;){
        const int old_app=active_app,old_sel=selected_app;
        poll_keyboard();
        const bool boot_pressed=gpio_get_level(GPIO_NUM_0)==0;
        if(boot_pressed&&!boot_button_was_pressed){
            if(active_app>=0){if(active_app==1)save_note();active_app=-1;viewing_file=false;}
            else {active_app=selected_app;if(active_app==0){std::strcpy(current_dir,"/sdcard");refresh_files();}if(active_app==1)load_note();if(active_app==5){seed_store();refresh_store();}}
        }
        boot_button_was_pressed=boot_pressed;
        const std::int64_t now=esp_timer_get_time();
        if(timer_running&&now>=timer_end_us){timer_running=false;timer_seconds=0;}
        if(active_app==11&&game_started&&!game_over&&now-game_tick_us>220000){
            game_tick_us=now;
            for(int i=snake_len-1;i>0;i--){snake_x[i]=snake_x[i-1];snake_y[i]=snake_y[i-1];}
            snake_x[0]+=snake_dx;snake_y[0]+=snake_dy;
            if(snake_x[0]<0||snake_x[0]>=23||snake_y[0]<0||snake_y[0]>=9)game_over=true;
            for(int i=1;i<snake_len;i++)if(snake_x[0]==snake_x[i]&&snake_y[0]==snake_y[i])game_over=true;
            if(snake_x[0]==food_x&&snake_y[0]==food_y){if(snake_len<47)++snake_len;++game_score;food_x=(food_x+7)%23;food_y=(food_y+3)%9;}
        }
        if(old_app!=active_app||old_sel!=selected_app||active_app==6||active_app==7||active_app==11||active_app==0||active_app==5||active_app==8)render_home();
        if(active_app==1&&note_length>0&&note_length%24==0)save_note();
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}
