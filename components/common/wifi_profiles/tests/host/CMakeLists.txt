cmake_minimum_required(VERSION 3.16)
project(wifi_profiles_host_tests C)

enable_testing()

add_executable(test_wifi_profiles
    test_wifi_profiles.c
    ../../wifi_profiles.c
)
target_include_directories(test_wifi_profiles PRIVATE
    ../../include
    stubs
)
add_test(NAME wifi_profiles COMMAND test_wifi_profiles)
