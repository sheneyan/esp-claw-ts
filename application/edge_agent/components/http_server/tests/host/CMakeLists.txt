cmake_minimum_required(VERSION 3.16)

project(http_server_tailscale_request_tests C)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(HTTP_SERVER_TEST_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)

set(CJSON_SOURCE
    ../../../../managed_components/espressif__cjson/cJSON/cJSON.c
)

add_executable(test_http_server_tailscale_request
    test_http_server_tailscale_request.c
    ../../http_server_tailscale_request.c
    ${CJSON_SOURCE}
)

add_executable(test_http_server_tailscale_handlers
    test_http_server_tailscale_handlers.c
    ../../http_server_tailscale_api.c
    ../../http_server_tailscale_request.c
    ${CJSON_SOURCE}
)

if(CMAKE_C_COMPILER_ID STREQUAL "AppleClang")
    set_source_files_properties(${CJSON_SOURCE} PROPERTIES
        COMPILE_OPTIONS -Wno-deprecated-declarations
    )
endif()

set(HTTP_SERVER_TEST_TARGETS
    test_http_server_tailscale_request
    test_http_server_tailscale_handlers
)

foreach(test_target IN LISTS HTTP_SERVER_TEST_TARGETS)
    target_include_directories(${test_target} PRIVATE
        stubs
        ../..
        ../../include
        ../../../../managed_components/espressif__cjson/cJSON
    )
endforeach()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    foreach(test_target IN LISTS HTTP_SERVER_TEST_TARGETS)
        target_compile_options(${test_target} PRIVATE
            -Wall -Wextra -Werror -pedantic -UNDEBUG
        )
        if(HTTP_SERVER_TEST_SANITIZERS)
            target_compile_options(${test_target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer
            )
            target_link_options(${test_target} PRIVATE
                -fsanitize=address,undefined
            )
        endif()
    endforeach()
endif()

add_test(NAME http_server_tailscale_request
         COMMAND test_http_server_tailscale_request)
add_test(NAME http_server_tailscale_handlers
         COMMAND test_http_server_tailscale_handlers)
