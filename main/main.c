#include <stdio.h>
#include "main.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_config.h"
#include "spotify_auth.h"
#include "spotify_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// HTTPS + TLS + cJSON needs ~12KB; 16KB gives comfortable headroom
#define SPOTIFY_TASK_STACK_SIZE (16 * 1024)
#define POLL_INTERVAL_MS 5000

static const char *TAG = "spotify_player";

static void spotify_task(void *arg)
{
    ESP_LOGI(TAG, "Initializing Spotify auth...");
    esp_err_t err = spotify_auth_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "spotify_auth_init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Entering playback polling loop");
    while (1)
    {
        spotify_playback_t playback = {0};
        err = spotify_api_get_playback(&playback);

        if (err == ESP_OK)
        {
            ESP_LOGI(TAG, "%s - %s (%s) [%d/%d ms]",
                     playback.track_name,
                     playback.artist_name,
                     playback.album_name,
                     playback.progress_ms,
                     playback.duration_ms);
        }
        else if (err == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGI(TAG, "Nothing playing");
        }
        else
        {
            ESP_LOGE(TAG, "get_playback failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(nvs_config_init());

    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_init_sta();

    xTaskCreate(spotify_task, "spotify", SPOTIFY_TASK_STACK_SIZE, NULL, 5, NULL);
}
