#include "cap_tailscale_contract.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#define TEST_CHECK(condition) assert(condition)

static void test_read_and_mutation_validation(void)
{
    TEST_CHECK(cap_tailscale_validate_read_args(0) == ESP_OK);
    TEST_CHECK(cap_tailscale_validate_read_args(1) == ESP_ERR_INVALID_ARG);

    TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 0) == ESP_OK);
    TEST_CHECK(cap_tailscale_validate_mutation_args(true, false, 0) == ESP_ERR_INVALID_STATE);
    TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 1) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_args(false, true, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_args(true, true, 2) == ESP_ERR_INVALID_ARG);

    TEST_CHECK(cap_tailscale_validate_mutation_confirmation(false, true, true, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_confirmation(true, false, true, 0) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_validate_mutation_confirmation(true, true, false, 0) == ESP_ERR_INVALID_STATE);
}

static void test_selector_normalization_and_cgnat_range(void)
{
    char selector[CAP_TAILSCALE_IP_LEN];
    char too_long[CAP_TAILSCALE_IP_LEN + 1];
    char oversized[CAP_TAILSCALE_HOSTNAME_LEN + 1];
    char hostname_selector[CAP_TAILSCALE_HOSTNAME_LEN];

    TEST_CHECK(cap_tailscale_normalize_selector(NULL, selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector(" \t ", selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("  100.64.0.1\t", selector, sizeof(selector)) == ESP_OK);
    TEST_CHECK(strcmp(selector, "100.64.0.1") == 0);

    memset(too_long, '1', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(too_long, selector, sizeof(selector)) == ESP_ERR_INVALID_ARG);
    memset(oversized, 'a', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    TEST_CHECK(cap_tailscale_normalize_selector(oversized, hostname_selector, sizeof(hostname_selector)) == ESP_ERR_INVALID_ARG);
    TEST_CHECK(cap_tailscale_normalize_selector("100.64.0.1", selector, 1) == ESP_ERR_INVALID_ARG);

    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.64.0.1"));
    TEST_CHECK(cap_tailscale_selector_is_cgnat("100.127.255.254"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.128.0.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("192.168.1.1"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0"));
    TEST_CHECK(!cap_tailscale_selector_is_cgnat("100.64.0.1x"));
}

static void test_count_bounds(void)
{
    TEST_CHECK(cap_tailscale_bound_exit_node_count(0) == 0);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(CAP_TAILSCALE_MAX_EXIT_NODES) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_exit_node_count(CAP_TAILSCALE_MAX_EXIT_NODES + 1) == CAP_TAILSCALE_MAX_EXIT_NODES);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(0) == 0);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(CAP_TAILSCALE_MAX_DERP_RTTS) == CAP_TAILSCALE_MAX_DERP_RTTS);
    TEST_CHECK(cap_tailscale_bound_derp_rtt_count(CAP_TAILSCALE_MAX_DERP_RTTS + 1) == CAP_TAILSCALE_MAX_DERP_RTTS);
}

int main(void)
{
    test_read_and_mutation_validation();
    test_selector_normalization_and_cgnat_range();
    test_count_bounds();

    puts("cap_tailscale_contract: all tests passed");
    return 0;
}
