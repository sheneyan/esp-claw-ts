#pragma once

#include "lwip/ip4_addr.h"

#include <stdint.h>

typedef uint8_t u8_t;

enum {
    IPADDR_TYPE_V4 = 0,
    IPADDR_TYPE_V6 = 6,
};

typedef struct {
    uint8_t type;
    union {
        ip4_addr_t ip4;
    } u_addr;
} ip_addr_t;

#define IP_IS_V4(address) ((address)->type == IPADDR_TYPE_V4)
#define ip_2_ip4(address) (&(address)->u_addr.ip4)
