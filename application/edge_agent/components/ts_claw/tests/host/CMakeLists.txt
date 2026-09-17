cmake_minimum_required(VERSION 3.16)

project(ts_claw_policy_host_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(test_ts_claw_policy
    test_ts_claw_policy.c
    ../../ts_claw_policy.c
)

target_include_directories(test_ts_claw_policy PRIVATE ../../include)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_ts_claw_policy PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
