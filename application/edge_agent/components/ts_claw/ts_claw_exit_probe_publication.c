#include "ts_claw_exit_probe_publication.h"

#include <stddef.h>

void ts_claw_exit_probe_publication_init(
    ts_claw_exit_probe_publication_t *state)
{
    if (state == NULL) {
        return;
    }
    *state = (ts_claw_exit_probe_publication_t){
        .generation = 1u,
    };
}

void ts_claw_exit_probe_publication_set_accept(
    ts_claw_exit_probe_publication_t *state, bool accept)
{
    if (state == NULL) {
        return;
    }
    if (!accept) {
        state->generation++;
        if (state->generation == 0u) {
            state->generation = 1u;
        }
        state->pending = false;
    }
    state->accept = accept;
}

uint32_t ts_claw_exit_probe_publication_begin(
    const ts_claw_exit_probe_publication_t *state, bool handle_matches)
{
    return state != NULL && state->accept && handle_matches
               ? state->generation
               : 0u;
}

bool ts_claw_exit_probe_publication_complete(
    ts_claw_exit_probe_publication_t *state,
    uint32_t generation,
    bool success)
{
    if (state == NULL || generation == 0u || !state->accept ||
        generation != state->generation) {
        return false;
    }
    state->pending = true;
    state->success = success;
    return true;
}

bool ts_claw_exit_probe_publication_take(
    ts_claw_exit_probe_publication_t *state, bool *success)
{
    if (state == NULL || success == NULL || !state->pending) {
        return false;
    }
    *success = state->success;
    state->pending = false;
    return true;
}
