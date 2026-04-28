#include <stdio.h>
#include "main.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SCL_GPIO 4
#define SDA_GPIO 3

static const char *TAG = "spotify player";

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();

    while (1)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        // gpio_dump_io_configuration(stdout, (1ULL << 4) | (1ULL << 3));
    }
}
