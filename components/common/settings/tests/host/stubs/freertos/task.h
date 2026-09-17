#pragma once

typedef void *TaskHandle_t;

static inline TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
    static _Thread_local int marker;
    return &marker;
}
