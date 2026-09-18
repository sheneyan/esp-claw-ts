#pragma once

#include "esp_err.h"
#include "lwip/netif.h"

#define TS_CLAW_WG_MTU 1280u

esp_err_t ts_claw_apply_wg_mtu(struct netif *wg_netif);
