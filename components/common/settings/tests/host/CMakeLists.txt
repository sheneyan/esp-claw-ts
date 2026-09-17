cmake_minimum_required(VERSION 3.16)
project(settings_store_batch_tests C)

enable_testing()

find_package(Threads REQUIRED)

add_executable(test_settings_store_batch
    test_settings_store_batch.c
    ../../settings_store.c
)
target_include_directories(test_settings_store_batch PRIVATE
    stubs
    ../../include
)
target_link_libraries(test_settings_store_batch PRIVATE Threads::Threads)
target_compile_features(test_settings_store_batch PRIVATE c_std_11)
target_compile_options(test_settings_store_batch PRIVATE
    -Wall -Wextra -Werror -pedantic -UNDEBUG
)

add_test(NAME settings_store_batch COMMAND test_settings_store_batch)
