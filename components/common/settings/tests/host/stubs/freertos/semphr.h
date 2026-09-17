#pragma once

#include <pthread.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"

typedef pthread_mutex_t *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    SemaphoreHandle_t mutex = malloc(sizeof(*mutex));
    if (mutex == NULL || pthread_mutex_init(mutex, NULL) != 0) {
        free(mutex);
        return NULL;
    }
    return mutex;
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex, TickType_t ticks)
{
    (void)ticks;
    return pthread_mutex_lock(mutex) == 0 ? pdTRUE : 0;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    return pthread_mutex_unlock(mutex) == 0 ? pdTRUE : 0;
}
