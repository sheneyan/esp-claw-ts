#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    int unused;
} claw_cap_call_context_t;

typedef enum {
    CLAW_CAP_KIND_CALLABLE = 0,
} claw_cap_kind_t;

#define CLAW_CAP_FLAG_CALLABLE_BY_LLM 1U

typedef esp_err_t (*claw_cap_execute_fn)(const char *input_json,
                                         const claw_cap_call_context_t *ctx,
                                         char *output,
                                         size_t output_size);

typedef struct {
    const char *id;
    const char *name;
    const char *family;
    const char *description;
    claw_cap_kind_t kind;
    uint32_t cap_flags;
    const char *input_schema_json;
    claw_cap_execute_fn execute;
} claw_cap_descriptor_t;

typedef struct {
    const char *group_id;
    const claw_cap_descriptor_t *descriptors;
    size_t descriptor_count;
} claw_cap_group_t;

bool claw_cap_group_exists(const char *group_id);
esp_err_t claw_cap_register_group(const claw_cap_group_t *group);
