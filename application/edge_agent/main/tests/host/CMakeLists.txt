cmake_minimum_required(VERSION 3.16)
project(provision_button_policy_tests C)

enable_testing()

add_executable(test_provision_button_policy
    test_provision_button_policy.c
    ../../provision_button_policy.c
)
target_include_directories(test_provision_button_policy PRIVATE ../..)
target_compile_options(test_provision_button_policy PRIVATE -Wall -Wextra -Werror)

add_test(NAME provision_button_policy COMMAND test_provision_button_policy)
