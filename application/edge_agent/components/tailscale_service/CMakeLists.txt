get_filename_component(_TAILSCALE_SERVICE_BOARD_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/../gen_bmgr_codes/board_manager.defaults" ABSOLUTE)
set(_TAILSCALE_SERVICE_BOARD_MARKER "")
if(EXISTS "${_TAILSCALE_SERVICE_BOARD_DEFAULTS}")
    file(STRINGS "${_TAILSCALE_SERVICE_BOARD_DEFAULTS}" _TAILSCALE_SERVICE_BOARD_MARKER
        REGEX "^CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW=y$")
endif()

if(_TAILSCALE_SERVICE_BOARD_MARKER)
    idf_component_register(
        SRCS "tailscale_service.c"
        INCLUDE_DIRS "include"
        REQUIRES ts_claw
    )
else()
    idf_component_register()
endif()
