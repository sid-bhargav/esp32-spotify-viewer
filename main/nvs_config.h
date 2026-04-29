#pragma once

#include "esp_err.h"

#define NVS_NAMESPACE "spotify"

// Keys stored in NVS
#define NVS_KEY_REFRESH_TK "refresh_token"
#define NVS_KEY_ACCESS_TK "access_token"
#define NVS_KEY_TOKEN_EXP "token_expiry" // unix timestamp when access token expires

esp_err_t nvs_config_init(void);

// Write once at first boot (or via provisioning)
esp_err_t nvs_config_set_refresh_token(const char *token);

esp_err_t nvs_config_get_refresh_token(char *buf, size_t buf_len);
esp_err_t nvs_config_set_access_token(const char *token, int64_t expiry_unix);
esp_err_t nvs_config_get_access_token(char *buf, size_t buf_len, int64_t *expiry_unix);