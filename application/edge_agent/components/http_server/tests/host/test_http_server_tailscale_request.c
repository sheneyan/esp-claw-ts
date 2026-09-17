/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "http_server_tailscale_request.h"

static int failures;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,          \
                    #condition);                                                \
            ++failures;                                                         \
        }                                                                       \
    } while (0)

static void check_set_ok(const char *body, const char *expected)
{
    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN] = {0};
    http_server_tailscale_request_result_t result =
        http_server_tailscale_parse_set_request(body, strlen(body), selector);

    CHECK(result == HTTP_SERVER_TAILSCALE_REQUEST_OK);
    CHECK(strcmp(selector, expected) == 0);
}

static void check_set_invalid(const char *body)
{
    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN];
    memset(selector, 'x', sizeof(selector));
    http_server_tailscale_request_result_t result =
        http_server_tailscale_parse_set_request(body, strlen(body), selector);

    CHECK(result == HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    CHECK(selector[0] == '\0');
}

static void check_set_invalid_bytes(const char *body, size_t body_length)
{
    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN];
    memset(selector, 'x', sizeof(selector));
    http_server_tailscale_request_result_t result =
        http_server_tailscale_parse_set_request(body, body_length, selector);

    CHECK(result == HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    CHECK(selector[0] == '\0');
}

static void test_set_request_contract(void)
{
    check_set_ok("{\"node\":\"100.104.62.56\"}", "100.104.62.56");
    check_set_ok("{\"node\":\"  exit.example.ts.net  \"}",
                 "exit.example.ts.net");

    check_set_invalid("{}");
    check_set_invalid("{\"node\":null}");
    check_set_invalid("{\"node\":42}");
    check_set_invalid("{\"node\":\"\"}");
    check_set_invalid("{\"node\":\" \\t\\r\\n \"}");
    check_set_invalid("{\"node\":\"exit\",\"extra\":true}");
    check_set_invalid("{\"node\":\"one\",\"node\":\"two\"}");
    check_set_invalid("{\"node\":\"exit\",\"user_confirmed\":true}");
    check_set_invalid("{\"node\":\"exit\"");
    check_set_invalid("{\"node\":\"exit\"} trailing");
    check_set_invalid("[\"exit\"]");
    check_set_invalid("{\"node\\u0000suffix\":\"exit\"}");
    check_set_invalid("{\"node\":\"exit\\u0000suffix\"}");

    const char literal_nul[] = "{\"node\":\"exit\"}\0trailing";
    char selector[HTTP_SERVER_TAILSCALE_SELECTOR_LEN];
    memset(selector, 'x', sizeof(selector));
    CHECK(http_server_tailscale_parse_set_request(
              literal_nul, sizeof(literal_nul) - 1u, selector) ==
          HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    CHECK(selector[0] == '\0');

    const char terminal_nul[] = "{\"node\":\"exit\"}\0";
    CHECK(http_server_tailscale_parse_set_request(
              terminal_nul, sizeof(terminal_nul) - 1u, selector) ==
          HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    CHECK(selector[0] == '\0');
}

static void test_strict_json_lexical_contract(void)
{
    const char raw_newline[] = "{\"node\":\"bad\nnode\"}";
    const char raw_tab[] = "{\"node\":\"bad\tnode\"}";
    const char raw_control[] = {
        '{', '"', 'n', 'o', 'd', 'e', '"', ':', '"', 'a', 0x01, 'b', '"', '}'
    };
    const char trailing_control[] = {'{', '}', 0x01};
    const char leading_vertical_tab[] = {0x0b, '{', '}'};
    const char trailing_form_feed[] = {'{', '}', 0x0c};

    check_set_invalid_bytes(raw_newline, sizeof(raw_newline) - 1u);
    check_set_invalid_bytes(raw_tab, sizeof(raw_tab) - 1u);
    check_set_invalid_bytes(raw_control, sizeof(raw_control));
    CHECK(http_server_tailscale_parse_empty_request(
              trailing_control, sizeof(trailing_control)) ==
          HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    CHECK(http_server_tailscale_parse_empty_request(
              leading_vertical_tab, sizeof(leading_vertical_tab)) ==
          HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    CHECK(http_server_tailscale_parse_empty_request(
              trailing_form_feed, sizeof(trailing_form_feed)) ==
          HTTP_SERVER_TAILSCALE_REQUEST_INVALID);

    const char allowed_outer_whitespace[] = " \t\r\n{}\n\t ";
    CHECK(http_server_tailscale_parse_empty_request(
              allowed_outer_whitespace,
              sizeof(allowed_outer_whitespace) - 1u) ==
          HTTP_SERVER_TAILSCALE_REQUEST_OK);

    check_set_invalid("{\"node\":\"bad\\qescape\"}");
    check_set_invalid("{\"node\":\"bad\\u12g4\"}");
    check_set_invalid("{\"node\":\"bad\\u123\"}");

    check_set_ok("{\"node\":\"quote\\\"slash\\\\node\"}",
                 "quote\"slash\\node");
    check_set_ok("{\"node\":\"line\\nnode\"}", "line\nnode");
    check_set_ok("{\"node\":\"literal\\\\u0000\"}", "literal\\u0000");
}

static void test_set_request_length_boundaries(void)
{
    char body[HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX + 2u];
    char expected[HTTP_SERVER_TAILSCALE_SELECTOR_LEN];

    memset(expected, 'a', sizeof(expected) - 1u);
    expected[sizeof(expected) - 1u] = '\0';
    snprintf(body, sizeof(body), "{\"node\":\"%s\"}", expected);
    check_set_ok(body, expected);

    char oversized[HTTP_SERVER_TAILSCALE_SELECTOR_LEN + 1u];
    memset(oversized, 'b', sizeof(oversized) - 1u);
    oversized[sizeof(oversized) - 1u] = '\0';
    snprintf(body, sizeof(body), "{\"node\":\"%s\"}", oversized);
    check_set_invalid(body);

    snprintf(body, sizeof(body), "{\"node\":\" %s \"}", expected);
    check_set_ok(body, expected);

    const size_t json_length = strlen(body);
    const size_t padding = HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX - json_length;
    memmove(body + padding, body, json_length + 1u);
    memset(body, ' ', padding);
    CHECK(strlen(body) == HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX);
    check_set_ok(body, expected);

    body[HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX] = ' ';
    body[HTTP_SERVER_TAILSCALE_REQUEST_BODY_MAX + 1u] = '\0';
    check_set_invalid(body);
}

static void test_empty_action_request_contract(void)
{
    CHECK(http_server_tailscale_parse_empty_request(NULL, 0u) ==
          HTTP_SERVER_TAILSCALE_REQUEST_OK);
    CHECK(http_server_tailscale_parse_empty_request("", 0u) ==
          HTTP_SERVER_TAILSCALE_REQUEST_OK);
    CHECK(http_server_tailscale_parse_empty_request("{}", 2u) ==
          HTTP_SERVER_TAILSCALE_REQUEST_OK);
    CHECK(http_server_tailscale_parse_empty_request(" { } \n", 6u) ==
          HTTP_SERVER_TAILSCALE_REQUEST_OK);

    const char terminal_nul[] = "{}\0";
    CHECK(http_server_tailscale_parse_empty_request(
              terminal_nul, sizeof(terminal_nul) - 1u) ==
          HTTP_SERVER_TAILSCALE_REQUEST_INVALID);

    const char *invalid[] = {
        " ",
        "{\"extra\":true}",
        "{\"extra\":1,\"extra\":2}",
        "{",
        "{} trailing",
        "[]",
        "{\"x\\u0000y\":null}",
    };
    for (size_t i = 0u; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(http_server_tailscale_parse_empty_request(invalid[i],
                                                         strlen(invalid[i])) ==
              HTTP_SERVER_TAILSCALE_REQUEST_INVALID);
    }
}

static void test_http_status_mapping(void)
{
    struct mapping {
        const char *error;
        http_server_tailscale_http_status_t expected;
    } cases[] = {
        {"ok", 200},
        {"invalid_node", 400},
        {"not_enabled", 503},
        {"not_connected", 503},
        {"ambiguous_node", 409},
        {"node_not_found", 409},
        {"node_offline", 409},
        {"not_exit_node", 409},
        {"switch_timeout", 503},
        {"busy", 409},
        {"runtime_apply_failed", 503},
        {"persistence_failed", 500},
        {"rollback_failed", 500},
        {"reconnect_failed", 503},
        {"unknown", 500},
        {"", 500},
    };

    for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        http_server_tailscale_operation_t operation = {0};
        operation.ok = strcmp(cases[i].error, "ok") == 0;
        snprintf(operation.error, sizeof(operation.error), "%s", cases[i].error);
        CHECK(http_server_tailscale_operation_http_status(&operation, ESP_OK) ==
              cases[i].expected);
    }

    http_server_tailscale_operation_t operation = {.ok = true};
    snprintf(operation.error, sizeof(operation.error), "ok");
    CHECK(http_server_tailscale_operation_http_status(&operation, ESP_FAIL) == 500);
    CHECK(http_server_tailscale_operation_http_status(NULL, ESP_OK) == 500);
}

static void test_response_rendering_is_complete_and_escaped(void)
{
    http_server_tailscale_operation_t operation = {0};
    operation.ok = false;
    snprintf(operation.error, sizeof(operation.error), "rollback_failed");
    snprintf(operation.message, sizeof(operation.message),
             "quote=\" slash=\\ newline=\n");
    snprintf(operation.selected_ip, sizeof(operation.selected_ip),
             "100.104.62.56");
    memset(operation.selected_hostname, 'h',
           sizeof(operation.selected_hostname) - 1u);
    operation.selected_hostname[sizeof(operation.selected_hostname) - 1u] = '\0';
    snprintf(operation.exit_state, sizeof(operation.exit_state), "fallback");
    snprintf(operation.egress, sizeof(operation.egress), "sta");
    operation.persisted = false;
    operation.rollback_attempted = true;
    operation.rollback_recovered = false;

    char *payload = http_server_tailscale_render_operation_json(&operation);
    CHECK(payload != NULL);
    if (!payload) {
        return;
    }

    cJSON *root = cJSON_Parse(payload);
    CHECK(cJSON_IsObject(root));
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(root, "ok")));
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
                     root, "error")), "rollback_failed") == 0);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
                     root, "message")), operation.message) == 0);
    CHECK(strcmp(cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
                     root, "selected_hostname")), operation.selected_hostname) == 0);
    CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(
        root, "rollback_attempted")));
    CHECK(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(
        root, "rollback_recovered")));
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "auth_key") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "login_server") == NULL);
    CHECK(cJSON_GetObjectItemCaseSensitive(root, "public_key") == NULL);

    cJSON_Delete(root);
    free(payload);
}

int main(void)
{
    test_set_request_contract();
    test_strict_json_lexical_contract();
    test_set_request_length_boundaries();
    test_empty_action_request_contract();
    test_http_status_mapping();
    test_response_rendering_is_complete_and_escaped();

    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("http_server_tailscale_request tests passed");
    return 0;
}
