#pragma once

#include <pthread.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"

typedef pthread_mutex_t *SemaphoreHandle_t;

extern int freertos_test_fail_next_mutex_create;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    if (freertos_test_fail_next_mutex_create) {
        freertos_test_fail_next_mutex_create = 0;
        return NULL;
    }
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
    return pthread_mutex_trylock(mutex) == 0 ? pdTRUE : pdFALSE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{
    return pthread_mutex_unlock(mutex) == 0 ? pdTRUE : pdFALSE;
}

static inline void vSemaphoreDelete(SemaphoreHandle_t mutex)
{
    if (mutex != NULL) {
        (void)pthread_mutex_destroy(mutex);
        free(mutex);
    }
}
