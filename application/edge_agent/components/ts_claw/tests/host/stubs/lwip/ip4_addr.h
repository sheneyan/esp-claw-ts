#pragma once

#include <stdint.h>

typedef struct {
    uint32_t addr;
} ip4_addr_t;

#define ip4_addr_get_u32(address) ((address)->addr)

static inline uint32_t lwip_ntohl(uint32_t value)
{
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return __builtin_bswap32(value);
#else
    return value;
#endif
}

static inline uint32_t lwip_htonl(uint32_t value)
{
    return lwip_ntohl(value);
}
