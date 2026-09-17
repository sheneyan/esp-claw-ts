cmake_minimum_required(VERSION 3.16)

project(app_config_tailscale_validation_tests C)

enable_testing()

add_executable(test_app_config_tailscale_validation
    test_app_config_tailscale_validation.c
    ../../app_config_tailscale_validation.c
)

target_include_directories(test_app_config_tailscale_validation PRIVATE ../../include)
target_compile_features(test_app_config_tailscale_validation PRIVATE c_std_11)
target_compile_options(test_app_config_tailscale_validation PRIVATE -Wall -Wextra -Werror)

add_test(
    NAME app_config_tailscale_validation
    COMMAND test_app_config_tailscale_validation
)

add_executable(test_app_config_storage
    test_app_config_storage.c
    ../../app_config.c
    ../../app_config_tailscale_validation.c
)
target_include_directories(test_app_config_storage PRIVATE
    stubs
    ../../include
    ../../../../../../components/common/app_claw/include
)
target_compile_features(test_app_config_storage PRIVATE c_std_11)
target_compile_options(test_app_config_storage PRIVATE
    -Wall -Wextra -Werror -UNDEBUG
)

add_test(NAME app_config_storage COMMAND test_app_config_storage)
