set(_MICROLINK_PATCH_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/patches/microlink-upstream-bind.patch")
set(_MICROLINK_ORIGINAL_STUN_SHA256
    "762f7a5eaa6d4b6e1f8be6c4595a8e81893bcadc7c11a3e374d74b7a79894039")
set(_MICROLINK_ORIGINAL_COORD_SHA256
    "b5ea85a275c8f5e51ec33d25efad67808cf5de1a39dec7199ff1d1ebcafdc89c")
set(_MICROLINK_ORIGINAL_CORE_SHA256
    "a2bb6a02be34e8f6e03f2ba5b78472c2cd5472edd7d2f6e3158a836fcbb4a0b2")
set(_MICROLINK_ORIGINAL_PEER_NVS_SHA256
    "6529d00c8765d83ee5279b35d41d055944111a4b6d8ec1731fd0f630801190d0")
set(_MICROLINK_PATCHED_STUN_SHA256
    "9f2bf58ce17251401e3613e61cc4507219f19447087874a905732f864b09b316")
set(_MICROLINK_PATCHED_COORD_SHA256
    "9762c30a0c37cd5f24354f4f95dc49270dc8eeeebf2c758cc69530810b56e9d6")
set(_MICROLINK_PATCHED_CORE_SHA256
    "4b8025b111659bcd943573feb5d2768fee64f33574181b0491988d6344035abc")
set(_MICROLINK_PATCHED_PEER_NVS_SHA256
    "7388ac0b2cabf84f91645716fcc443e9ecf8685e07bbdd5a01c24b54eb505fb5")

function(_microlink_assert_upstream_bind_patch source_dir)
    file(READ "${source_dir}/src/ml_stun.c" _stun_source)
    string(REGEX MATCHALL
        "ml_bind_sock_to_upstream\\(ml, ml->stun_sock6?\\)"
        _stun_bind_matches
        "${_stun_source}"
    )
    list(LENGTH _stun_bind_matches _stun_bind_count)
    if(_stun_bind_count LESS 2)
        message(FATAL_ERROR
            "MicroLink patched copy is missing dedicated IPv4/IPv6 STUN upstream binds: ${source_dir}")
    endif()

    file(READ "${source_dir}/src/ml_coord.c" _coord_source)
    string(FIND "${_coord_source}" "static struct ifreq *ml_upstream_ifreq" _helper_offset)
    string(REGEX MATCHALL
        "\\.if_name[ \t]*=[ \t]*ml_upstream_ifreq\\("
        _tls_bind_matches
        "${_coord_source}"
    )
    list(LENGTH _tls_bind_matches _tls_bind_count)
    if(_helper_offset EQUAL -1 OR _tls_bind_count LESS 2)
        message(FATAL_ERROR
            "MicroLink patched copy is missing the upstream TLS helper or both HTTPS if_name bindings: ${source_dir}")
    endif()

    file(READ "${source_dir}/src/microlink.c" _core_source)
    foreach(_reset_marker IN ITEMS
            "esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);"
            "if (err == ESP_OK) err = nvs_commit(nvs);"
            "err = ml_peer_nvs_init();"
            "err = ml_peer_nvs_clear();")
        string(FIND "${_core_source}" "${_reset_marker}" _reset_marker_offset)
        if(_reset_marker_offset EQUAL -1)
            message(FATAL_ERROR
                "MicroLink patched copy is missing factory-reset error handling: ${_reset_marker}")
        endif()
    endforeach()
    string(FIND "${_core_source}" "esp_err_t microlink_factory_reset(void)" _reset_start)
    string(FIND "${_core_source}" " * Public API" _reset_end)
    if(_reset_start EQUAL -1 OR _reset_end LESS _reset_start)
        message(FATAL_ERROR
            "MicroLink patched copy is missing the factory-reset function boundary: ${source_dir}")
    endif()
    math(EXPR _reset_length "${_reset_end} - ${_reset_start}")
    string(SUBSTRING "${_core_source}" ${_reset_start} ${_reset_length} _reset_source)
    string(REGEX MATCHALL "if \\(err != ESP_OK\\) return err;"
        _reset_error_checks "${_reset_source}")
    list(LENGTH _reset_error_checks _reset_error_check_count)
    if(_reset_error_check_count LESS 4)
        message(FATAL_ERROR
            "MicroLink patched copy is missing factory-reset error checks: ${source_dir}")
    endif()

    file(READ "${source_dir}/src/ml_peer_nvs.c" _peer_nvs_source)
    string(FIND "${_peer_nvs_source}" "err = nvs_commit(s_nvs);" _peer_commit_offset)
    string(FIND "${_peer_nvs_source}"
        "err = nvs_commit(s_nvs);\n    if (err != ESP_OK) return err;"
        _peer_commit_check_offset)
    string(FIND "${_peer_nvs_source}"
        "if (s_table) memset(s_table, 0, sizeof(peer_nvs_table_t));"
        _peer_clear_offset)
    if(_peer_commit_offset EQUAL -1 OR _peer_commit_check_offset EQUAL -1 OR
       _peer_clear_offset LESS _peer_commit_offset)
        message(FATAL_ERROR
            "MicroLink patched copy clears the peer table before a successful NVS commit: ${source_dir}")
    endif()
endfunction()

function(microlink_apply_upstream_bind_patch source_dir)
    if(NOT EXISTS "${source_dir}/src/ml_stun.c" OR
       NOT EXISTS "${source_dir}/src/ml_coord.c" OR
       NOT EXISTS "${source_dir}/src/microlink.c" OR
       NOT EXISTS "${source_dir}/src/ml_peer_nvs.c")
        message(FATAL_ERROR
            "MicroLink upstream-bind patch requires exact component sources at: ${source_dir}")
    endif()
    if(NOT EXISTS "${_MICROLINK_PATCH_FILE}")
        message(FATAL_ERROR "MicroLink patch file is missing: ${_MICROLINK_PATCH_FILE}")
    endif()

    find_program(_MICROLINK_GIT_EXECUTABLE git)
    if(NOT _MICROLINK_GIT_EXECUTABLE)
        message(FATAL_ERROR "git is required to apply the MicroLink upstream-bind patch")
    endif()
    get_filename_component(_MICROLINK_SOURCE_PARENT "${source_dir}" DIRECTORY)

    file(SHA256 "${source_dir}/src/ml_stun.c" _stun_sha256)
    file(SHA256 "${source_dir}/src/ml_coord.c" _coord_sha256)
    file(SHA256 "${source_dir}/src/microlink.c" _core_sha256)
    file(SHA256 "${source_dir}/src/ml_peer_nvs.c" _peer_nvs_sha256)
    if("${_stun_sha256}" STREQUAL "${_MICROLINK_PATCHED_STUN_SHA256}" AND
       "${_coord_sha256}" STREQUAL "${_MICROLINK_PATCHED_COORD_SHA256}" AND
       "${_core_sha256}" STREQUAL "${_MICROLINK_PATCHED_CORE_SHA256}" AND
       "${_peer_nvs_sha256}" STREQUAL "${_MICROLINK_PATCHED_PEER_NVS_SHA256}")
        _microlink_assert_upstream_bind_patch("${source_dir}")
        message(STATUS "MicroLink upstream-bind patch already applied and verified at ${source_dir}")
        return()
    endif()
    if(NOT "${_stun_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_STUN_SHA256}" OR
       NOT "${_coord_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_COORD_SHA256}" OR
       NOT "${_core_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_CORE_SHA256}" OR
       NOT "${_peer_nvs_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_PEER_NVS_SHA256}")
        message(FATAL_ERROR
            "MicroLink sources are neither the exact original nor patched revisions at ${source_dir}.\n"
            "Actual ml_stun.c SHA256: ${_stun_sha256}\n"
            "Actual ml_coord.c SHA256: ${_coord_sha256}\n"
            "Actual microlink.c SHA256: ${_core_sha256}\n"
            "Actual ml_peer_nvs.c SHA256: ${_peer_nvs_sha256}")
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${_MICROLINK_SOURCE_PARENT}"
                "${_MICROLINK_GIT_EXECUTABLE}" apply --no-index
                --unidiff-zero --ignore-space-change
                "${_MICROLINK_PATCH_FILE}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _apply_result
        OUTPUT_VARIABLE _apply_stdout
        ERROR_VARIABLE _apply_stderr
    )
    if(NOT _apply_result EQUAL 0)
        message(FATAL_ERROR
            "MicroLink upstream-bind patch failed:\n${_apply_stdout}${_apply_stderr}")
    endif()
    file(SHA256 "${source_dir}/src/ml_stun.c" _stun_sha256)
    file(SHA256 "${source_dir}/src/ml_coord.c" _coord_sha256)
    file(SHA256 "${source_dir}/src/microlink.c" _core_sha256)
    file(SHA256 "${source_dir}/src/ml_peer_nvs.c" _peer_nvs_sha256)
    if(NOT "${_stun_sha256}" STREQUAL "${_MICROLINK_PATCHED_STUN_SHA256}" OR
       NOT "${_coord_sha256}" STREQUAL "${_MICROLINK_PATCHED_COORD_SHA256}" OR
       NOT "${_core_sha256}" STREQUAL "${_MICROLINK_PATCHED_CORE_SHA256}" OR
       NOT "${_peer_nvs_sha256}" STREQUAL "${_MICROLINK_PATCHED_PEER_NVS_SHA256}")
        message(FATAL_ERROR
            "MicroLink patch output does not match the exact expected patched revisions at ${source_dir}.\n"
            "Actual ml_stun.c SHA256: ${_stun_sha256}\n"
            "Actual ml_coord.c SHA256: ${_coord_sha256}\n"
            "Actual microlink.c SHA256: ${_core_sha256}\n"
            "Actual ml_peer_nvs.c SHA256: ${_peer_nvs_sha256}")
    endif()
    _microlink_assert_upstream_bind_patch("${source_dir}")
    message(STATUS "Applied and verified MicroLink upstream-bind patch at ${source_dir}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT DEFINED MICROLINK_SOURCE_DIR OR MICROLINK_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "Pass -DMICROLINK_SOURCE_DIR=<component-dir>")
    endif()
    microlink_apply_upstream_bind_patch("${MICROLINK_SOURCE_DIR}")
endif()
