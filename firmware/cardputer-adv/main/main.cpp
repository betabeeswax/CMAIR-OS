#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdint>

namespace {
constexpr char kTag[] = "CMAIR-OS";
constexpr std::uint32_t kHealthIntervalMs = 5000;
}

extern "C" void app_main(void) {
    ESP_LOGI(kTag, "CMAIR-OS Cardputer ADV firmware starting (development foundation)");
    ESP_LOGI(kTag, "Board-specific display, keyboard, battery and microSD drivers are not enabled yet.");
    ESP_LOGI(kTag, "Verify the exact Cardputer ADV hardware revision before adding pin mappings.");

    for (;;) {
        const auto uptime_us = esp_timer_get_time();
        ESP_LOGI(kTag, "firmware heartbeat; uptime=%lld ms",
                 static_cast<long long>(uptime_us / 1000));
        vTaskDelay(pdMS_TO_TICKS(kHealthIntervalMs));
    }
}
