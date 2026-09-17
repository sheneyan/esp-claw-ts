set(_MICROLINK_PATCH_FILE
    "${CMAKE_CURRENT_LIST_DIR}/../../third_party/patches/microlink-upstream-bind.patch")

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
endfunction()

function(_microlink_assert_exact_unpatched_source source_dir)
    file(SHA256 "${source_dir}/src/ml_stun.c" _stun_sha256)
    file(SHA256 "${source_dir}/src/ml_coord.c" _coord_sha256)
    if(NOT _stun_sha256 STREQUAL
           "762f7a5eaa6d4b6e1f8be6c4595a8e81893bcadc7c11a3e374d74b7a79894039" OR
       NOT _coord_sha256 STREQUAL
           "b5ea85a275c8f5e51ec33d25efad67808cf5de1a39dec7199ff1d1ebcafdc89c")
        message(FATAL_ERROR
            "MicroLink sources do not match the exact expected unpatched ml_stun.c/ml_coord.c revisions: ${source_dir}")
    endif()
endfunction()

function(microlink_apply_upstream_bind_patch source_dir)
    if(NOT EXISTS "${source_dir}/src/ml_stun.c" OR
       NOT EXISTS "${source_dir}/src/ml_coord.c")
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

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${_MICROLINK_SOURCE_PARENT}"
                "${_MICROLINK_GIT_EXECUTABLE}" apply --reverse --check --no-index
                --unidiff-zero --ignore-space-change
                "${_MICROLINK_PATCH_FILE}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _reverse_check_result
        OUTPUT_VARIABLE _reverse_check_stdout
        ERROR_VARIABLE _reverse_check_stderr
    )
    if(_reverse_check_result EQUAL 0)
        _microlink_assert_upstream_bind_patch("${source_dir}")
        message(STATUS "MicroLink upstream-bind patch already applied and verified at ${source_dir}")
        return()
    endif()

    _microlink_assert_exact_unpatched_source("${source_dir}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${_MICROLINK_SOURCE_PARENT}"
                "${_MICROLINK_GIT_EXECUTABLE}" apply --check --no-index
                --unidiff-zero --ignore-space-change
                "${_MICROLINK_PATCH_FILE}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _apply_check_result
        OUTPUT_VARIABLE _apply_check_stdout
        ERROR_VARIABLE _apply_check_stderr
    )
    if(NOT _apply_check_result EQUAL 0)
        message(FATAL_ERROR
            "MicroLink exact sources failed the upstream-bind patch check:\n${_apply_check_stdout}${_apply_check_stderr}\n"
            "Reverse check:\n${_reverse_check_stdout}${_reverse_check_stderr}")
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
            "MicroLink upstream-bind patch failed after a successful check:\n${_apply_stdout}${_apply_stderr}")
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
