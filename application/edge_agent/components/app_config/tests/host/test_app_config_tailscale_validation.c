#include "app_config_tailscale_validation.h"

#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition); \
        s_failures++; \
    } \
} while (0)

static app_config_tailscale_view_t valid_config(void)
{
    return (app_config_tailscale_view_t) {
        .enabled = "true",
        .auth_key = "tskey-auth-super-secret",
        .hostname = "ts-claw",
        .login_server = "https://login.tailscale.com",
        .exit_node = "100.70.80.90",
        .max_peers = "16",
    };
}

static bool validate(const app_config_tailscale_view_t *config, char *message, size_t message_size)
{
    if (message && message_size > 0) {
        memset(message, 0xa5, message_size);
    }
    return app_config_tailscale_validate(config, message, message_size);
}

static void test_defaults_and_disabled_are_valid(void)
{
    char message[128];
    app_config_tailscale_view_t defaults = {
        .enabled = "false",
        .auth_key = "",
        .hostname = "",
        .login_server = "",
        .exit_node = "",
        .max_peers = "16",
    };
    app_config_tailscale_view_t disabled_nulls = {
        .enabled = "0",
    };

    CHECK(validate(&defaults, message, sizeof(message)));
    CHECK(message[0] == '\0');
    CHECK(validate(&disabled_nulls, message, sizeof(message)));
}

static void test_enabled_token_is_strict(void)
{
    char message[128];
    app_config_tailscale_view_t config = valid_config();
    const char *valid_tokens[] = {"true", "false", "1", "0"};
    const char *invalid_tokens[] = {"", "TRUE", "yes", "2", " true"};

    for (size_t i = 0; i < sizeof(valid_tokens) / sizeof(valid_tokens[0]); i++) {
        config.enabled = valid_tokens[i];
        CHECK(validate(&config, message, sizeof(message)));
    }
    for (size_t i = 0; i < sizeof(invalid_tokens) / sizeof(invalid_tokens[0]); i++) {
        config.enabled = invalid_tokens[i];
        CHECK(!validate(&config, message, sizeof(message)));
    }
    config.enabled = NULL;
    CHECK(!validate(&config, message, sizeof(message)));
}

static void test_peer_limits(void)
{
    char message[128];
    app_config_tailscale_view_t config = valid_config();

    config.max_peers = "0";
    CHECK(!validate(&config, message, sizeof(message)));
    config.max_peers = "1";
    CHECK(validate(&config, message, sizeof(message)));
    config.max_peers = "16";
    CHECK(validate(&config, message, sizeof(message)));
    config.max_peers = "64";
    CHECK(validate(&config, message, sizeof(message)));
    config.max_peers = "65";
    CHECK(!validate(&config, message, sizeof(message)));
    config.max_peers = "1x";
    CHECK(!validate(&config, message, sizeof(message)));
}

static void test_exit_node_must_be_cgnat_ipv4(void)
{
    char message[128];
    app_config_tailscale_view_t config = valid_config();
    const char *valid_nodes[] = {"", "100.64.0.0", "100.70.80.90", "100.127.255.255"};
    const char *invalid_nodes[] = {
        "100.63.255.255", "100.128.0.0", "192.168.1.1", "100.64.0.256", "100.64.0", "host"
    };

    for (size_t i = 0; i < sizeof(valid_nodes) / sizeof(valid_nodes[0]); i++) {
        config.exit_node = valid_nodes[i];
        CHECK(validate(&config, message, sizeof(message)));
    }
    for (size_t i = 0; i < sizeof(invalid_nodes) / sizeof(invalid_nodes[0]); i++) {
        config.exit_node = invalid_nodes[i];
        CHECK(!validate(&config, message, sizeof(message)));
    }
}

static void test_enabled_hostname_length(void)
{
    char message[128];
    char hostname_63[64];
    char hostname_64[65];
    app_config_tailscale_view_t config = valid_config();

    memset(hostname_63, 'a', sizeof(hostname_63) - 1);
    hostname_63[sizeof(hostname_63) - 1] = '\0';
    memset(hostname_64, 'b', sizeof(hostname_64) - 1);
    hostname_64[sizeof(hostname_64) - 1] = '\0';

    config.hostname = "";
    CHECK(!validate(&config, message, sizeof(message)));
    config.hostname = "a";
    CHECK(validate(&config, message, sizeof(message)));
    config.hostname = hostname_63;
    CHECK(validate(&config, message, sizeof(message)));
    config.hostname = hostname_64;
    CHECK(!validate(&config, message, sizeof(message)));
}

static void test_login_server_supported_forms(void)
{
    char message[128];
    app_config_tailscale_view_t config = valid_config();
    const char *valid_servers[] = {
        "", "login.example.com", "login.example.com:8443",
        "http://login.example.com", "https://login.example.com:8443"
    };
    const char *invalid_servers[] = {
        "://login.example.com", "http://", "https://", "ftp://login.example.com",
        "login.example.com/path", "https://login.example.com/path", "login example.com",
        " login.example.com", "login.example.com?query=1", "login.example.com#fragment"
    };

    for (size_t i = 0; i < sizeof(valid_servers) / sizeof(valid_servers[0]); i++) {
        config.login_server = valid_servers[i];
        CHECK(validate(&config, message, sizeof(message)));
    }
    for (size_t i = 0; i < sizeof(invalid_servers) / sizeof(invalid_servers[0]); i++) {
        config.login_server = invalid_servers[i];
        CHECK(!validate(&config, message, sizeof(message)));
    }
}

static void test_null_fields_are_safe_and_messages_do_not_leak_keys(void)
{
    char message[128];
    app_config_tailscale_view_t config = valid_config();

    CHECK(!validate(NULL, message, sizeof(message)));
    CHECK(message[0] != '\0');

    config.hostname = NULL;
    CHECK(!validate(&config, message, sizeof(message)));
    CHECK(strstr(message, "tskey-auth-super-secret") == NULL);

    config = valid_config();
    config.max_peers = NULL;
    CHECK(!validate(&config, message, sizeof(message)));
    CHECK(strstr(message, "tskey-auth-super-secret") == NULL);

    config = valid_config();
    config.auth_key = NULL;
    CHECK(validate(&config, NULL, 0));
}

int main(void)
{
    test_defaults_and_disabled_are_valid();
    test_enabled_token_is_strict();
    test_peer_limits();
    test_exit_node_must_be_cgnat_ipv4();
    test_enabled_hostname_length();
    test_login_server_supported_forms();
    test_null_fields_are_safe_and_messages_do_not_leak_keys();

    if (s_failures != 0) {
        fprintf(stderr, "%d test assertion(s) failed\n", s_failures);
        return 1;
    }
    puts("all app_config Tailscale validation tests passed");
    return 0;
}
