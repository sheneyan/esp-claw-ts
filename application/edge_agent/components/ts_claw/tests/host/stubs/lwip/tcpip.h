#pragma once

#include "lwip/err.h"

typedef unsigned char u8_t;
typedef void (*tcpip_callback_fn)(void *ctx);

err_t tcpip_callback_with_block(tcpip_callback_fn fn, void *ctx, u8_t block);
