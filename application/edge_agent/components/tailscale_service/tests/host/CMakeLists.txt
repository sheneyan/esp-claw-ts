cmake_minimum_required(VERSION 3.16)
project(tailscale_service_host_tests C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

find_package(Threads REQUIRED)

add_executable(test_tailscale_service
    test_tailscale_service.c
    ../../tailscale_service.c
)
target_include_directories(test_tailscale_service PRIVATE
    stubs
    ../../include
    ../../../ts_claw/include
    ../../../ts_claw
)
target_link_libraries(test_tailscale_service PRIVATE Threads::Threads)
target_compile_definitions(test_tailscale_service PRIVATE TAILSCALE_SERVICE_HOST_TEST)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(test_tailscale_service PRIVATE
        -Wall -Wextra -Werror -pedantic -UNDEBUG
    )
endif()

add_test(NAME tailscale_service COMMAND test_tailscale_service)
