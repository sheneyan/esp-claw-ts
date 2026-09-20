cmake_minimum_required(VERSION 3.16)
project(wifi_manager_ap_policy_tests C)

enable_testing()

add_executable(test_wifi_manager_ap_policy
    test_wifi_manager_ap_policy.c
    ../../wifi_manager_ap_policy.c
)
target_include_directories(test_wifi_manager_ap_policy PRIVATE ../../include)
target_compile_options(test_wifi_manager_ap_policy PRIVATE -Wall -Wextra -Werror)

add_test(NAME wifi_manager_ap_policy COMMAND test_wifi_manager_ap_policy)

add_executable(test_wifi_manager_profile_policy
    test_wifi_manager_profile_policy.c
    ../../wifi_manager_profile_policy.c
)
target_include_directories(test_wifi_manager_profile_policy PRIVATE
    ../../include
    ../../../wifi_profiles/include
    ../../../wifi_profiles/tests/host/stubs
)
target_compile_options(test_wifi_manager_profile_policy PRIVATE -Wall -Wextra -Werror)
add_test(NAME wifi_manager_profile_policy COMMAND test_wifi_manager_profile_policy)
