#include "spotify_api.h"
#include "spotify_auth.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "spotify_api";

#define PLAYBACK_URL "https://api.spotify.com/v1/me/player"
#define RESPONSE_BUFSZ 8192

typedef struct
{
    char *buf;
    int buf_size;
    int bytes_written;
} http_response_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (resp->bytes_written + evt->data_len < resp->buf_size - 1)
        {
            memcpy(resp->buf + resp->bytes_written, evt->data, evt->data_len);
            resp->bytes_written += evt->data_len;
        }
        else
        {
            ESP_LOGW(TAG, "Response buffer full, truncating");
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        resp->buf[resp->bytes_written] = '\0';
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t art_event_handler(esp_http_client_event_t *evt)
{
    http_response_t *resp = (http_response_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA)
    {
        if (resp->bytes_written + evt->data_len <= resp->buf_size)
        {
            memcpy(resp->buf + resp->bytes_written, evt->data, evt->data_len);
            resp->bytes_written += evt->data_len;
        }
        else
        {
            ESP_LOGW(TAG, "Album art buffer full, truncating");
        }
    }
    return ESP_OK;
}

esp_err_t spotify_api_get_playback(spotify_playback_t *out)
{
    char token[512] = {0};
    esp_err_t err = spotify_auth_get_token(token, sizeof(token));
    if (err != ESP_OK)
        return err;

    char auth_header[560];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);

    char *response_buf = calloc(RESPONSE_BUFSZ, 1);
    if (!response_buf)
        return ESP_ERR_NO_MEM;

    http_response_t resp = {
        .buf = response_buf,
        .buf_size = RESPONSE_BUFSZ,
        .bytes_written = 0,
    };

    esp_http_client_config_t config = {
        .url = PLAYBACK_URL,
        .method = HTTP_METHOD_GET,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Authorization", auth_header);

    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    // ESP-IDF returns ESP_ERR_NOT_SUPPORTED when it receives a 401 with
    // WWW-Authenticate: Bearer — it tries to handle the challenge internally
    // but doesn't support the Bearer scheme. The status code is still valid,
    // so fall through to the status checks below instead of treating it as a
    // hard failure.
    if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED)
    {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(response_buf);
        return ESP_FAIL;
    }

    if (status == 204)
    {
        ESP_LOGI(TAG, "No active playback");
        free(response_buf);
        return ESP_ERR_NOT_FOUND;
    }

    if (status == 401)
    {
        ESP_LOGW(TAG, "Token rejected by Spotify, forcing refresh");
        spotify_auth_invalidate();
        free(response_buf);
        return ESP_ERR_INVALID_STATE;
    }

    if (status != 200)
    {
        ESP_LOGE(TAG, "Unexpected HTTP status %d: %s", status, response_buf);
        free(response_buf);
        return ESP_FAIL;
    }

    cJSON *root = cJSON_Parse(response_buf);
    free(response_buf);
    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_FAIL;
    }

    cJSON *item = cJSON_GetObjectItem(root, "item");
    if (!cJSON_IsObject(item))
    {
        ESP_LOGE(TAG, "No 'item' in playback response");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    cJSON *album = cJSON_GetObjectItem(item, "album");
    cJSON *artists = cJSON_GetObjectItem(item, "artists");

    cJSON *images = cJSON_GetObjectItem(album, "images");
    cJSON *album_art_64 = cJSON_GetArrayItem(images, 2);
    cJSON *album_art_url = cJSON_GetObjectItem(album_art_64, "url");
    cJSON *album_art_h = cJSON_GetObjectItem(album_art_64, "height");
    cJSON *album_art_w = cJSON_GetObjectItem(album_art_64, "width");

    cJSON *first_artist = cJSON_GetArrayItem(artists, 0);
    cJSON *track_name = cJSON_GetObjectItem(item, "name");
    cJSON *album_name = cJSON_GetObjectItem(album, "name");
    cJSON *artist_name = cJSON_GetObjectItem(first_artist, "name");
    cJSON *duration = cJSON_GetObjectItem(item, "duration_ms");
    cJSON *progress = cJSON_GetObjectItem(root, "progress_ms");

    if (!cJSON_IsString(track_name) || !cJSON_IsString(album_name) ||
        !cJSON_IsString(artist_name) || !cJSON_IsNumber(duration) ||
        !cJSON_IsNumber(progress) || !cJSON_IsString(album_art_url) || !cJSON_IsNumber(album_art_h) || !cJSON_IsNumber(album_art_w))
    {
        ESP_LOGE(TAG, "Missing expected fields in playback JSON");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    out->is_playing = cJSON_IsTrue(cJSON_GetObjectItem(root, "is_playing"));
    out->progress_ms = progress->valueint;
    out->duration_ms = duration->valueint;
    strncpy(out->track_name, track_name->valuestring, sizeof(out->track_name) - 1);
    strncpy(out->album_name, album_name->valuestring, sizeof(out->album_name) - 1);
    strncpy(out->artist_name, artist_name->valuestring, sizeof(out->artist_name) - 1);
    strncpy(out->album_art.url, album_art_url->valuestring, sizeof(out->album_art.url) - 1);
    out->album_art.height = album_art_h->valueint;
    out->album_art.width = album_art_w->valueint;

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t spotify_api_fetch_album_art(const char *url, uint8_t **out_buf, size_t *out_len)
{
    char *response_buf = calloc(RESPONSE_BUFSZ, 1);
    if (!response_buf)
        return ESP_ERR_NO_MEM;

    http_response_t resp = {
        .buf = response_buf,
        .buf_size = RESPONSE_BUFSZ,
        .bytes_written = 0,
    };

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = art_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        free(response_buf);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Album art fetch failed: %s", esp_err_to_name(err));
        free(response_buf);
        return ESP_FAIL;
    }

    if (status != 200)
    {
        ESP_LOGE(TAG, "Album art fetch returned HTTP %d", status);
        free(response_buf);
        return ESP_FAIL;
    }

    // Shrink the allocation to exactly what was received, then hand ownership
    // to the caller. realloc to a smaller size never fails.
    *out_buf = (uint8_t *)realloc(response_buf, resp.bytes_written);
    *out_len = resp.bytes_written;
    return ESP_OK;
}