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

add_executable(test_tailscale_persistence
    test_tailscale_persistence.c
    ../../tailscale_persistence.c
)
target_include_directories(test_tailscale_persistence PRIVATE
    stubs
    ../..
)
target_compile_features(test_tailscale_persistence PRIVATE c_std_11)
target_compile_options(test_tailscale_persistence PRIVATE
    -Wall -Wextra -Werror -UNDEBUG
)
add_test(NAME tailscale_persistence COMMAND test_tailscale_persistence)

add_executable(test_config_persistence
    test_config_persistence.c
    ../../config_persistence.c
)
target_include_directories(test_config_persistence PRIVATE
    stubs
    ../..
)
target_compile_features(test_config_persistence PRIVATE c_std_11)
target_compile_options(test_config_persistence PRIVATE
    -Wall -Wextra -Werror -UNDEBUG
)
add_test(NAME config_persistence COMMAND test_config_persistence)
