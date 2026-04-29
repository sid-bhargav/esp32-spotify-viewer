#pragma once
#include "esp_err.h"

esp_err_t spotify_auth_init(void);

esp_err_t spotify_auth_get_token(char *buf, size_t buf_len);