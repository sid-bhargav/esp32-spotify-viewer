#include "nvs_config.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "nvs_config";

static nvs_handle_t s_nvs_handle;
static bool s_initialized = false;

esp_err_t nvs_config_init(void)
{
    if (s_initialized)
    {
        return ESP_OK;
    }

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return err;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "NVS namespace '%s' opened", NVS_NAMESPACE);
    return ESP_OK;
}

// Guard against buffer overflow when getting a string from the nvs
static esp_err_t get_string(const char *key, char *buf, size_t buf_len)
{
    if (!s_initialized)
    {
        ESP_LOGE(TAG, "nvs_config not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    size_t required_len = 0;
    esp_err_t err = nvs_get_str(s_nvs_handle, key, NULL, &required_len);
    if (err != ESP_OK)
    {
        return err;
    }

    if (required_len > buf_len)
    {
        ESP_LOGE(TAG, "Buffer too small for key '%s': need %d, have %d",
                 key, (int)required_len, (int)buf_len);
        return ESP_ERR_INVALID_SIZE;
    }

    err = nvs_get_str(s_nvs_handle, key, buf, &required_len);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_get_str failed for key '%s': %s",
                 key, esp_err_to_name(err));
    }
    return err;
}

// Wraps nvs_set_str + nvs_commit
static esp_err_t set_string(const char *key, const char *value)
{
    if (!s_initialized)
    {
        ESP_LOGE(TAG, "nvs_config not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_set_str(s_nvs_handle, key, value);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_set_str failed for key '%s': %s",
                 key, esp_err_to_name(err));
        return err;
    }

    err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_commit failed: '%s", esp_err_to_name(err));
    }
    return err;
}

// Refresh Token
esp_err_t nvs_config_set_refresh_token(const char *token)
{
    ESP_LOGI(TAG, "Saving refresh token");
    return set_string(NVS_KEY_REFRESH_TK, token);
}

esp_err_t nvs_config_get_refresh_token(char *buf, size_t buf_len)
{
    esp_err_t err = get_string(NVS_KEY_REFRESH_TK, buf, buf_len);
    if (err == ESP_ERR_NVS_NOT_FOUND)
    {
        ESP_LOGW(TAG, "No refresh token stored. Flash secrets.h first.");
    }
    return err;
}

// Access Token
esp_err_t nvs_config_set_access_token(const char *token, int64_t expiry_unix)
{
    ESP_LOGI(TAG, "Saving access token (expires at %lld)", expiry_unix);

    esp_err_t err = set_string(NVS_KEY_ACCESS_TK, token);
    if (err != ESP_OK)
        return err;

    err = nvs_set_i64(s_nvs_handle, NVS_KEY_TOKEN_EXP, expiry_unix);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_set_i64 failed %s", esp_err_to_name(err));
        return err;
    }

    return nvs_commit(s_nvs_handle);
}

esp_err_t nvs_config_get_access_token(char *buf, size_t buf_len, int64_t *expiry_unix)
{
    esp_err_t err = get_string(NVS_KEY_ACCESS_TK, buf, buf_len);
    if (err != ESP_OK)
        return err;

    err = nvs_get_i64(s_nvs_handle, NVS_KEY_TOKEN_EXP, expiry_unix);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "nvs_get_i64 failed: %s", esp_err_to_name(err));
    }
    return err;
}