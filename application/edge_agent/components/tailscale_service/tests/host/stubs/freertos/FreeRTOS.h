#pragma once

#include <stdint.h>

typedef int BaseType_t;
typedef uint32_t TickType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
