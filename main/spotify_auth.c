// Refresh token

#include "secrets.h"
#include "spotify_auth.h"
#include "nvs_config.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <time.h>
#include <stdlib.h>

static const char *TAG = "spotify_auth";

#define TOKEN_URL "https://accounts.spotify.com/api/token"
#define TOKEN_EXPIRY_PAD 60 // refresh 60 seconds before expiration
#define RESPONSE_BUF_SIZE 2048

static char s_access_token[512] = {0};
static int64_t s_expiry_unix = 0;

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
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "HTTP disconnected");
        break;
    default:
        break;
    }
    return ESP_OK;
}

static esp_err_t do_token_refresh(void)
{
    ESP_LOGI(TAG, "Refreshing access token...");

    char refresh_token[512] = {0};
    esp_err_t err = nvs_config_get_refresh_token(refresh_token, sizeof(refresh_token));
    if (err == ESP_ERR_NVS_NOT_FOUND || strlen(refresh_token) == 0)
    {
        ESP_LOGW(TAG, "No refresh token in NVS, falling back to secrets.h");
        strncpy(refresh_token, SPOTIFY_REFRESH_TOKEN, sizeof(refresh_token) - 1);
    }
    else if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "NVS read failed: %s", esp_err_to_name(err));
        return err;
    }

    if (strlen(refresh_token) == 0)
    {
        ESP_LOGE(TAG, "No refresh token available. Run get_refresh_token.py.");
        return ESP_ERR_INVALID_STATE;
    }

    // Build the post body
    char post_body[600] = {0};
    snprintf(post_body, sizeof(post_body),
             "grant_type=refresh_token&refresh_token=%s", refresh_token);

    char *response_buf = calloc(RESPONSE_BUF_SIZE, 1);
    if (!response_buf)
    {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        return ESP_ERR_NO_MEM;
    }

    http_response_t resp = {
        .buf = response_buf,
        .buf_size = RESPONSE_BUF_SIZE,
        .bytes_written = 0,
    };

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = TOKEN_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        free(response_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/x-www-form-urlencoded");
    esp_http_client_set_header(client, "Authorization", "Basic " SPOTIFY_CLIENT_B64);
    esp_http_client_set_post_field(client, post_body, strlen(post_body));

    // Perform the request
    err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        free(response_buf);
        return ESP_FAIL;
    }

    if (status != 200)
    {
        ESP_LOGE(TAG, "Token endpoint returned HTTP %d: %s", status, response_buf);
        free(response_buf);
        return ESP_FAIL;
    }

    // Parse the JSON response
    cJSON *root = cJSON_Parse(response_buf);
    free(response_buf);

    if (!root)
    {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return ESP_FAIL;
    }

    cJSON *access_token_json = cJSON_GetObjectItem(root, "access_token");
    cJSON *expires_in_json = cJSON_GetObjectItem(root, "expires_in");

    if (!cJSON_IsString(access_token_json) || !cJSON_IsNumber(expires_in_json))
    {
        ESP_LOGE(TAG, "Unexpected JSON structure");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    // Store new access token
    strncpy(s_access_token, access_token_json->valuestring,
            sizeof(s_access_token) - 1);

    int expires_in = expires_in_json->valueint;
    s_expiry_unix = (int64_t)time(NULL) + expires_in - TOKEN_EXPIRY_PAD;

    // Cache in NVS
    nvs_config_set_access_token(s_access_token, s_expiry_unix);

    ESP_LOGI(TAG, "Access token refreshed, valid for %ds", expires_in);

    // Handle refresh token rotation
    cJSON *new_refresh = cJSON_GetObjectItem(root, "refresh_token");
    if (cJSON_IsString(new_refresh) && strlen(new_refresh->valuestring) > 0)
    {
        ESP_LOGI(TAG, "Spotify rotated the refresh token, saving to NVS");
        nvs_config_set_refresh_token(new_refresh->valuestring);
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t spotify_auth_init(void)
{
    int64_t cached_expiry = 0;
    esp_err_t err = nvs_config_get_access_token(
        s_access_token, sizeof(s_access_token), &cached_expiry);

    if (err == ESP_OK && (int64_t)time(NULL) < cached_expiry)
    {
        ESP_LOGI(TAG, "Using cached access token from NVS");
        s_expiry_unix = cached_expiry;
        return ESP_OK;
    }

    // No valid cached token — do a full refresh
    return do_token_refresh();
}

esp_err_t spotify_auth_get_token(char *buf, size_t buf_len)
{
    if (s_access_token[0] == '\0' || (int64_t)time(NULL) >= s_expiry_unix)
    {
        esp_err_t err = do_token_refresh();
        if (err != ESP_OK)
            return err;
    }

    strncpy(buf, s_access_token, buf_len - 1);
    buf[buf_len - 1] = '\0';
    return ESP_OK;
}

void spotify_auth_invalidate(void)
{
    s_access_token[0] = '\0';
    s_expiry_unix = 0;
}