#include <stdio.h>
#include "main.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_config.h"
#include "spotify_auth.h"
#include "spotify_api.h"
#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// HTTPS + TLS + cJSON needs ~12KB; 16KB gives comfortable headroom
#define SPOTIFY_TASK_STACK_SIZE (16 * 1024)
#define POLL_INTERVAL_MS 5000

static const char *TAG = "spotify_player";

static void display_render_playback(const spotify_playback_t *p)
{
    display_clear(COLOR_BLACK);
    display_draw_text(2, 4, p->track_name, COLOR_WHITE, COLOR_BLACK);
    display_draw_text(2, 14, p->artist_name, COLOR_GRAY, COLOR_BLACK);
    display_draw_text(2, 24, p->album_name, COLOR_GRAY, COLOR_BLACK);
}

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
            display_render_playback(&playback);
        }
        else if (err == ESP_ERR_NOT_FOUND)
        {
            ESP_LOGI(TAG, "Nothing playing");
            display_clear(COLOR_BLACK);
        }
        else if (err == ESP_ERR_INVALID_STATE)
        {
            // Token was rejected — spotify_auth_invalidate() has already been
            // called, so the next spotify_auth_get_token() will refresh.
            // Skip the normal delay so the retry happens immediately.
            ESP_LOGW(TAG, "Token expired, retrying after refresh...");
            continue;
        }
        else
        {
            ESP_LOGE(TAG, "get_playback failed: %s", esp_err_to_name(err));
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}

static void display_test(void)
{
    ESP_LOGI(TAG, "Display test starting...");

    ESP_ERROR_CHECK(display_init());

    // Cycle through solid colors to verify the panel is alive
    const struct
    {
        uint32_t color;
        const char *name;
    } colors[] = {
        {COLOR_RED, "red"},
        {COLOR_GREEN, "green"},
        {COLOR_BLUE, "blue"},
        {COLOR_WHITE, "white"},
        {COLOR_BLACK, "black"},
    };
    for (int i = 0; i < sizeof(colors) / sizeof(colors[0]); i++)
    {
        ESP_LOGI(TAG, "Fill: %s", colors[i].name);
        display_clear(colors[i].color);
        vTaskDelay(pdMS_TO_TICKS(600));
    }

    // Draw a small grid of colored rectangles
    display_clear(COLOR_BLACK);
    display_fill_rect(0, 0, 64, 80, COLOR_RED);
    display_fill_rect(64, 0, 64, 80, COLOR_GREEN);
    display_fill_rect(0, 80, 64, 80, COLOR_BLUE);
    display_fill_rect(64, 80, 64, 80, COLOR_YELLOW);
    vTaskDelay(pdMS_TO_TICKS(1000));

    // Draw a single pixel in each corner
    display_draw_pixel(0, 0, COLOR_WHITE);
    display_draw_pixel(DISPLAY_WIDTH - 1, 0, COLOR_WHITE);
    display_draw_pixel(0, DISPLAY_HEIGHT - 1, COLOR_WHITE);
    display_draw_pixel(DISPLAY_WIDTH - 1, DISPLAY_HEIGHT - 1, COLOR_WHITE);
    vTaskDelay(pdMS_TO_TICKS(500));

    ESP_LOGI(TAG, "Display test done");
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

    display_test();

    ESP_LOGI(TAG, "Connecting to WiFi...");
    wifi_init_sta();

    xTaskCreate(spotify_task, "spotify", SPOTIFY_TASK_STACK_SIZE, NULL, 5, NULL);
}
