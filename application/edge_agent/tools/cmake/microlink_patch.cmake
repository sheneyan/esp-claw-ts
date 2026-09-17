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
                "${_MICROLINK_GIT_EXECUTABLE}" apply --check --no-index --ignore-space-change
                "${_MICROLINK_PATCH_FILE}"
        WORKING_DIRECTORY "${source_dir}"
        RESULT_VARIABLE _apply_check_result
        OUTPUT_VARIABLE _apply_check_stdout
        ERROR_VARIABLE _apply_check_stderr
    )
    if(_apply_check_result EQUAL 0)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env
                    "GIT_CEILING_DIRECTORIES=${_MICROLINK_SOURCE_PARENT}"
                    "${_MICROLINK_GIT_EXECUTABLE}" apply --no-index --ignore-space-change
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
        return()
    endif()

    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env
                "GIT_CEILING_DIRECTORIES=${_MICROLINK_SOURCE_PARENT}"
                "${_MICROLINK_GIT_EXECUTABLE}" apply --reverse --check --no-index --ignore-space-change
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

    message(FATAL_ERROR
        "MicroLink sources do not match the exact expected upstream-bind patch state.\n"
        "Apply check:\n${_apply_check_stdout}${_apply_check_stderr}\n"
        "Reverse check:\n${_reverse_check_stdout}${_reverse_check_stderr}")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT DEFINED MICROLINK_SOURCE_DIR OR MICROLINK_SOURCE_DIR STREQUAL "")
        message(FATAL_ERROR "Pass -DMICROLINK_SOURCE_DIR=<component-dir>")
    endif()
    microlink_apply_upstream_bind_patch("${MICROLINK_SOURCE_DIR}")
endif()
