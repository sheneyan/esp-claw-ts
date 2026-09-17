idf_component_register(
    SRCS
        "ts_claw.c"
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
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u ts_claw_init")
