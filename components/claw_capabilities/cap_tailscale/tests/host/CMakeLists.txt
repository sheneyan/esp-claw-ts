cmake_minimum_required(VERSION 3.16)
project(cap_tailscale_contract_tests C)

enable_testing()

add_executable(test_cap_tailscale_contract
    test_cap_tailscale_contract.c
    ../../src/cap_tailscale_contract.c
)
target_include_directories(test_cap_tailscale_contract PRIVATE
    stubs
    ../../include
    ../../private_include
)
target_compile_options(test_cap_tailscale_contract PRIVATE -Wall -Wextra -Werror)

add_test(NAME cap_tailscale_contract COMMAND test_cap_tailscale_contract)
