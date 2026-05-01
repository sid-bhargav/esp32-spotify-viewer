#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct
{
    char url[128];
    int height;
    int width;
} album_art_t;

typedef struct
{
    char track_name[128];
    char artist_name[128];
    char album_name[128];
    int progress_ms;
    int duration_ms;
    bool is_playing;
    album_art_t album_art;
} spotify_playback_t;

// Fetches current playback state. Returns ESP_ERR_NOT_FOUND if nothing is playing.
esp_err_t spotify_api_get_playback(spotify_playback_t *out);
// Fetches album art JPEG from url. Allocates *out_buf on success — caller must free().
esp_err_t spotify_api_fetch_album_art(const char *url, uint8_t **out_buf, size_t *out_len);