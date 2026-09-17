cmake_minimum_required(VERSION 3.16)

project(ts_claw_policy_host_tests C)

enable_testing()

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

add_executable(test_ts_claw_route_hook
    test_ts_claw_route_hook.c
    ../../ts_claw_route_hook.c
)

target_include_directories(test_ts_claw_route_hook PRIVATE
    stubs
    ../..
    ../../include
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_ts_claw_route_hook PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(test_ts_claw_diagnostics
    test_ts_claw_diagnostics.c
    ../../ts_claw_diagnostics.c
)

target_include_directories(test_ts_claw_diagnostics PRIVATE
    ../..
    ../../include
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_ts_claw_diagnostics PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_executable(test_ts_claw_runtime_control
    test_ts_claw_runtime_control.c
    ../../ts_claw_runtime_control.c
)

target_include_directories(test_ts_claw_runtime_control PRIVATE ../..)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_ts_claw_runtime_control PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

add_test(NAME ts_claw_route_hook COMMAND test_ts_claw_route_hook)
add_test(NAME ts_claw_policy COMMAND test_ts_claw_policy)
add_test(NAME ts_claw_diagnostics COMMAND test_ts_claw_diagnostics)
add_test(NAME ts_claw_runtime_control COMMAND test_ts_claw_runtime_control)
