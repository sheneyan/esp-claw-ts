#pragma once

#include "lwip/ip_addr.h"

#define DNS_MAX_SERVERS 8

const ip_addr_t *dns_getserver(u8_t index);
