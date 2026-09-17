#pragma once

#include "esp_err.h"

typedef struct app_claw_config {
    char source_wifi[64];
} app_claw_config_t;

esp_err_t app_claw_update_config(const app_claw_config_t *config);
