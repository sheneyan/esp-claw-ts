if(NOT DEFINED BOARD_DEFAULTS)
    message(FATAL_ERROR "BOARD_DEFAULTS is required")
endif()

file(READ "${BOARD_DEFAULTS}" board_defaults)
if(NOT board_defaults MATCHES "(^|\n)CONFIG_LWIP_TCP_MSS=1240(\n|$)")
    message(FATAL_ERROR
        "N16R8 TS-Claw board must set CONFIG_LWIP_TCP_MSS=1240")
endif()
