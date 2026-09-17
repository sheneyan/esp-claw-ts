get_filename_component(_TS_CLAW_BOARD_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../gen_bmgr_codes/board_manager.defaults" ABSOLUTE)
set(_TS_CLAW_BOARD_MARKER "")
if(EXISTS "${_TS_CLAW_BOARD_DEFAULTS}")
    file(STRINGS "${_TS_CLAW_BOARD_DEFAULTS}" _TS_CLAW_BOARD_MARKER
        REGEX "^CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW=y$")
endif()

if(_TS_CLAW_BOARD_MARKER)
    idf_component_register(
        SRCS
            "ts_claw.c"
            "ts_claw_diagnostics.c"
            "ts_claw_policy.c"
            "ts_claw_route_hook.c"
        INCLUDE_DIRS "include"
        PRIV_INCLUDE_DIRS "."
        REQUIRES
            esp_netif
            heap
            lwip
            microlink
        WHOLE_ARCHIVE
    )

    # Task5 intentionally does not start TS-Claw from main.c yet. Keep the public
    # lifecycle entry point in the image so this component and its transport
    # dependencies are link-validated before the later configuration wiring.
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-u ts_claw_init"
        "-Wl,--wrap=ip4_route_src_hook"
    )
else()
    idf_component_register()
endif()
