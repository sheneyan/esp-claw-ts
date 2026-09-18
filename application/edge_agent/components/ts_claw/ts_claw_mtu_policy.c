#include "ts_claw_mtu_policy.h"

#include <stddef.h>

#include "lwip/err.h"
#include "lwip/tcpip.h"

typedef struct {
    struct netif *netif;
} ts_claw_mtu_apply_t;

static void apply_mtu_on_tcpip(void *ctx)
{
    ts_claw_mtu_apply_t *apply = ctx;
    apply->netif->mtu = TS_CLAW_WG_MTU;
}

esp_err_t ts_claw_apply_wg_mtu(struct netif *wg_netif)
{
    if (wg_netif == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ts_claw_mtu_apply_t apply = {.netif = wg_netif};
    if (tcpip_callback_with_block(apply_mtu_on_tcpip, &apply, 1u) != ERR_OK) {
        return ESP_FAIL;
    }

    return wg_netif->mtu == TS_CLAW_WG_MTU ? ESP_OK : ESP_FAIL;
}
