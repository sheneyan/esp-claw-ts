set(EDGE_AGENT_MAIN_SRCS
    "app_fs.c"
    "edge_agent_version.c"
    "main.c"
)
set(EDGE_AGENT_MAIN_PRIV_REQUIRES "")
if(CONFIG_ESP_BOARD_ESP32_S3_N16R8_TS_CLAW)
    list(APPEND EDGE_AGENT_MAIN_SRCS
        "provision_button.c"
        "provision_button_policy.c"
        "tailscale_persistence.c"
    )
    list(APPEND EDGE_AGENT_MAIN_PRIV_REQUIRES
        cap_tailscale
        esp_driver_gpio
        settings
        tailscale_service
        ts_claw
    )
endif()

idf_component_register(
    SRCS ${EDGE_AGENT_MAIN_SRCS}
    INCLUDE_DIRS "."
    PRIV_REQUIRES ${EDGE_AGENT_MAIN_PRIV_REQUIRES}
)
