#pragma once
#include "esp_err.h"
#include <stdbool.h>

typedef struct
{
    char track_name[128];
    char artist_name[128];
    char album_name[128];
    int progress_ms;
    int duration_ms;
    bool is_playing;
} spotify_playback_t;

// Fetches current playback state. Returns ESP_ERR_NOT_FOUND if nothing is playing.
esp_err_t spotify_api_get_playback(spotify_playback_t *out);