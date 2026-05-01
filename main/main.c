#include <stdio.h>
#include "main.h"
#include "wifi_station.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs_config.h"
#include "spotify_auth.h"
#include "spotify_api.h"
#include "display.h"
#include "tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// HTTPS + TLS + cJSON needs ~12KB; 16KB gives comfortable headroom
#define SPOTIFY_TASK_STACK_SIZE (16 * 1024)
#define POLL_INTERVAL_MS 5000

#define MAX_LINE_LENGTH 21

static const char *TAG = "spotify_player";
static char last_art_url[128];

#define TJPGD_WORK_SZ 3100

typedef struct
{
    const uint8_t *data;
    size_t len;
    size_t pos;
    uint8_t *out_buf;
    uint16_t out_width;
} jpeg_ctx_t;

static size_t tjpgd_input_cb(JDEC *jd, uint8_t *buf, size_t len)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)jd->device;
    size_t remaining = ctx->len - ctx->pos;
    if (len > remaining)
        len = remaining;
    if (buf)
        memcpy(buf, ctx->data + ctx->pos, len);
    ctx->pos += len;
    return len;
}

static int tjpgd_output_cb(JDEC *jd, void *bitmap, JRECT *rect)
{
    jpeg_ctx_t *ctx = (jpeg_ctx_t *)jd->device;
    uint8_t *src = (uint8_t *)bitmap;
    uint16_t rect_w = rect->right - rect->left + 1;
    uint16_t rect_h = rect->bottom - rect->top + 1;

    for (uint16_t row = 0; row < rect_h; row++)
    {
        uint8_t *dst = ctx->out_buf + ((rect->top + row) * ctx->out_width + rect->left) * 3;
        uint8_t *s = src + row * rect_w * 3;
        for (uint16_t col = 0; col < rect_w; col++, dst += 3, s += 3)
        {
            dst[0] = s[2]; // B
            dst[1] = s[1]; // G
            dst[2] = s[0]; // R
        }
    }
    return 1;
}

// Decodes a JPEG into a heap-allocated RGB888 buffer. Returns NULL on failure.
// Caller must free() the returned buffer.
static uint8_t *jpeg_decode(const uint8_t *jpeg_buf, size_t jpeg_len, uint16_t *out_w, uint16_t *out_h)
{
    uint8_t work[TJPGD_WORK_SZ];
    JDEC jdec;
    jpeg_ctx_t ctx = {.data = jpeg_buf, .len = jpeg_len, .pos = 0};

    if (jd_prepare(&jdec, tjpgd_input_cb, work, sizeof(work), &ctx) != JDR_OK)
    {
        ESP_LOGE(TAG, "JPEG prepare failed");
        return NULL;
    }

    *out_w = jdec.width;
    *out_h = jdec.height;

    ctx.out_buf = malloc(jdec.width * jdec.height * 3);
    if (!ctx.out_buf)
    {
        ESP_LOGE(TAG, "No memory for decoded JPEG");
        return NULL;
    }
    ctx.out_width = jdec.width;

    if (jd_decomp(&jdec, tjpgd_output_cb, 0) != JDR_OK)
    {
        ESP_LOGE(TAG, "JPEG decode failed");
        free(ctx.out_buf);
        return NULL;
    }

    return ctx.out_buf;
}

static void display_render_playback(const spotify_playback_t *p)
{
    if (strcmp(last_art_url, p->album_art.url) != 0)
    {
        display_clear(COLOR_BLACK);

        uint8_t *jpeg_buf = NULL;
        size_t jpeg_len = 0;

        if (spotify_api_fetch_album_art(p->album_art.url, &jpeg_buf, &jpeg_len) == ESP_OK)
        {
            uint16_t img_w = 0, img_h = 0;
            uint8_t *rgb_buf = jpeg_decode(jpeg_buf, jpeg_len, &img_w, &img_h);
            free(jpeg_buf);

            if (rgb_buf)
            {
                display_draw_bitmap(0, 0, img_w, img_h, rgb_buf);
                free(rgb_buf);
            }
        }
        else
        {
            ESP_LOGE(TAG, "Error pulling album art from server.");
        }

        display_draw_text(2, 68, p->track_name, COLOR_GREEN, COLOR_BLACK);
        display_draw_text(2, 78, p->artist_name, COLOR_WHITE, COLOR_BLACK);
        display_draw_text(2, 88, p->album_name, COLOR_GRAY, COLOR_BLACK);
    }
    strcpy(last_art_url, p->album_art.url);
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
            ESP_LOGI(TAG, "%s - %s (%s) [%d/%d ms] art url: %s",
                     playback.track_name,
                     playback.artist_name,
                     playback.album_name,
                     playback.progress_ms,
                     playback.duration_ms,
                     playback.album_art.url);
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
