#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation;
    bool accept;
    bool pending;
    bool success;
} ts_claw_exit_probe_publication_t;

void ts_claw_exit_probe_publication_init(
    ts_claw_exit_probe_publication_t *state);
void ts_claw_exit_probe_publication_set_accept(
    ts_claw_exit_probe_publication_t *state, bool accept);
uint32_t ts_claw_exit_probe_publication_begin(
    const ts_claw_exit_probe_publication_t *state, bool handle_matches);
bool ts_claw_exit_probe_publication_complete(
    ts_claw_exit_probe_publication_t *state,
    uint32_t generation,
    bool success);
bool ts_claw_exit_probe_publication_take(
    ts_claw_exit_probe_publication_t *state, bool *success);
