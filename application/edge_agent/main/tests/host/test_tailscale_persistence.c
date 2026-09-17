#include "tailscale_persistence.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

static int s_failures;
static int s_load_calls;
static int s_validate_calls;
static int s_targeted_save_calls;
static esp_err_t s_load_result;
static esp_err_t s_validate_result;
static esp_err_t s_targeted_save_result;
static char s_saved_value[16];

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
        ++s_failures; \
    } \
} while (0)

static void reset_fake(void)
{
    s_load_calls = 0;
    s_validate_calls = 0;
    s_targeted_save_calls = 0;
    s_load_result = ESP_OK;
    s_validate_result = ESP_OK;
    s_targeted_save_result = ESP_OK;
    s_saved_value[0] = '\0';
}

esp_err_t app_config_load(app_config_t *config)
{
    ++s_load_calls;
    if (s_load_result == ESP_OK) {
        memset(config, 0, sizeof(*config));
        (void)snprintf(config->unrelated_secret,
                       sizeof(config->unrelated_secret), "must-not-be-saved");
        (void)snprintf(config->tailscale_exit_node,
                       sizeof(config->tailscale_exit_node), "100.64.0.1");
    }
    return s_load_result;
}

esp_err_t app_config_validate_tailscale(const app_config_t *config,
                                        char *message,
                                        size_t message_size)
{
    ++s_validate_calls;
    CHECK(config != NULL);
    CHECK(strcmp(config->tailscale_exit_node, "100.64.0.2") == 0 ||
          strcmp(config->tailscale_exit_node, "") == 0);
    if (message != NULL && message_size > 0u) {
        message[0] = '\0';
    }
    return s_validate_result;
}

esp_err_t app_config_save_tailscale_exit_node(const char *value)
{
    ++s_targeted_save_calls;
    (void)snprintf(s_saved_value, sizeof(s_saved_value), "%s", value);
    return s_targeted_save_result;
}

static void test_success_uses_only_targeted_save(void)
{
    reset_fake();
    CHECK(main_tailscale_save_persisted_exit("100.64.0.2", NULL) == ESP_OK);
    CHECK(s_load_calls == 1);
    CHECK(s_validate_calls == 1);
    CHECK(s_targeted_save_calls == 1);
    CHECK(strcmp(s_saved_value, "100.64.0.2") == 0);

    reset_fake();
    CHECK(main_tailscale_save_persisted_exit("", NULL) == ESP_OK);
    CHECK(s_targeted_save_calls == 1);
    CHECK(strcmp(s_saved_value, "") == 0);
}

static void test_failures_stop_before_targeted_save(void)
{
    reset_fake();
    s_load_result = ESP_FAIL;
    CHECK(main_tailscale_save_persisted_exit("100.64.0.2", NULL) == ESP_FAIL);
    CHECK(s_validate_calls == 0);
    CHECK(s_targeted_save_calls == 0);

    reset_fake();
    s_validate_result = ESP_ERR_INVALID_ARG;
    CHECK(main_tailscale_save_persisted_exit("100.64.0.2", NULL) == ESP_ERR_INVALID_ARG);
    CHECK(s_validate_calls == 1);
    CHECK(s_targeted_save_calls == 0);

    reset_fake();
    s_targeted_save_result = ESP_FAIL;
    CHECK(main_tailscale_save_persisted_exit("100.64.0.2", NULL) == ESP_FAIL);
    CHECK(s_targeted_save_calls == 1);
}

static void test_null_value_is_rejected_without_loading(void)
{
    reset_fake();
    CHECK(main_tailscale_save_persisted_exit(NULL, NULL) == ESP_ERR_INVALID_ARG);
    CHECK(s_load_calls == 0);
    CHECK(s_validate_calls == 0);
    CHECK(s_targeted_save_calls == 0);
}

int main(void)
{
    test_success_uses_only_targeted_save();
    test_failures_stop_before_targeted_save();
    test_null_value_is_rejected_without_loading();

    if (s_failures != 0) {
        fprintf(stderr, "%d check(s) failed\n", s_failures);
        return 1;
    }
    puts("Tailscale persistence adapter tests passed");
    return 0;
}
