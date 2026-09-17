cmake_minimum_required(VERSION 3.16)

project(cmd_wifi_config_update_host_tests C)

enable_testing()

add_executable(test_cmd_wifi_config_update
    test_cmd_wifi_config_update.c
    ../../cmd_wifi_config_update.c
)
target_include_directories(test_cmd_wifi_config_update PRIVATE
    stubs
    ../..
)
target_compile_features(test_cmd_wifi_config_update PRIVATE c_std_11)
target_compile_definitions(test_cmd_wifi_config_update PRIVATE
    CMD_WIFI_CONFIG_UPDATE_HOST_TEST
)
target_compile_options(test_cmd_wifi_config_update PRIVATE
    -Wall -Wextra -Werror -pedantic -UNDEBUG
)

add_test(NAME cmd_wifi_config_update COMMAND test_cmd_wifi_config_update)
