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
set(_MICROLINK_ORIGINAL_WG_MGR_SHA256
    "a9cd0e603c0d701a7048e1e6823f90d58b1c2a84719afbe29d724f5bd3b82311")
set(_MICROLINK_ORIGINAL_PUBLIC_HEADER_SHA256
    "ead0147b2616242457d1371b89fc7266558b4c7d8816789df263172e33676b7f")
set(_MICROLINK_ORIGINAL_INTERNAL_HEADER_SHA256
    "b29ea6435f97bab9cdfc197c94fd0b5ef44801cbf33be4f09f288ab90b6534a2")
set(_MICROLINK_PATCHED_STUN_SHA256
    "9f2bf58ce17251401e3613e61cc4507219f19447087874a905732f864b09b316")
set(_MICROLINK_PATCHED_COORD_SHA256
    "c37bb9e222a83d2c092488b9cb71b67dbef3efec4378b1a29fea895a0ec9339f")
set(_MICROLINK_PATCHED_CORE_SHA256
    "543abb26b4cb34819ddd19e136beec59efe30ef7defd7b6468033b886da95b21")
set(_MICROLINK_PATCHED_PEER_NVS_SHA256
    "7388ac0b2cabf84f91645716fcc443e9ecf8685e07bbdd5a01c24b54eb505fb5")
set(_MICROLINK_PATCHED_WG_MGR_SHA256
    "ee2492296b52d3dc1778b7a581d078f4473246a205b9c1ff69dccc7dc4ec8617")
set(_MICROLINK_PATCHED_PUBLIC_HEADER_SHA256
    "42b6d15f884ab1888840e220bdd8389935b80aff0b48e70ed6cbfe9b9d480bda")
set(_MICROLINK_PATCHED_INTERNAL_HEADER_SHA256
    "7b3343d57644581274c4fc9fa40168b49add8af411b32b3dc0686d728936d70d")

function(_microlink_assert_upstream_bind_patch source_dir)
    file(READ "${source_dir}/include/microlink.h" _public_header_source)
    string(FIND "${_public_header_source}"
        "struct netif *microlink_get_wg_netif(const microlink_t *ml);"
        _wg_getter_declaration_offset)
    string(REGEX MATCHALL "microlink_get_wg_netif"
        _wg_getter_declarations "${_public_header_source}")
    list(LENGTH _wg_getter_declarations _wg_getter_declaration_count)
    if(_wg_getter_declaration_offset EQUAL -1 OR
       NOT _wg_getter_declaration_count EQUAL 1)
        message(FATAL_ERROR
            "MicroLink patched copy must expose exactly one public WG netif getter: ${source_dir}")
    endif()
    string(FIND "${_public_header_source}"
        "bool microlink_selected_exit_ready(const microlink_t *ml);"
        _exit_ready_declaration_offset)
    string(REGEX MATCHALL "microlink_selected_exit_ready"
        _exit_ready_declarations "${_public_header_source}")
    list(LENGTH _exit_ready_declarations _exit_ready_declaration_count)
    if(_exit_ready_declaration_offset EQUAL -1 OR
       NOT _exit_ready_declaration_count EQUAL 1)
        message(FATAL_ERROR
            "MicroLink patched copy must expose exactly one public selected-exit readiness getter: ${source_dir}")
    endif()

    file(READ "${source_dir}/include/microlink_internal.h" _internal_header_source)
    foreach(_readiness_field IN ITEMS "exit_route_attached" "selected_exit_ready")
        string(REGEX MATCHALL "${_readiness_field}"
            _readiness_field_matches "${_internal_header_source}")
        list(LENGTH _readiness_field_matches _readiness_field_count)
        if(NOT _readiness_field_count EQUAL 1)
            message(FATAL_ERROR
                "MicroLink patched copy must define exactly one ${_readiness_field} field: ${source_dir}")
        endif()
    endforeach()
    foreach(_snapshot_field IN ITEMS
            "SemaphoreHandle_t peer_snapshot_lock;"
            "microlink_peer_info_t peer_snapshot[ML_MAX_PEERS];"
            "int peer_snapshot_count;"
            "int peer_snapshot_online;")
        string(FIND "${_internal_header_source}" "${_snapshot_field}"
            _snapshot_field_offset)
        if(_snapshot_field_offset EQUAL -1)
            message(FATAL_ERROR
                "MicroLink patched copy is missing peer snapshot state: ${_snapshot_field}")
        endif()
    endforeach()

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
    string(REGEX MATCHALL
        "__atomic_load_n\\(&ml->upstream_netif, __ATOMIC_ACQUIRE\\)"
        _upstream_atomic_loads
        "${_coord_source}"
    )
    list(LENGTH _upstream_atomic_loads _upstream_atomic_load_count)
    string(REGEX MATCHALL "upstream_netif" _coord_upstream_accesses "${_coord_source}")
    list(LENGTH _coord_upstream_accesses _coord_upstream_access_count)
    if(NOT _upstream_atomic_load_count EQUAL 2 OR
       NOT _coord_upstream_access_count EQUAL 2)
        message(FATAL_ERROR
            "MicroLink patched copy must use exactly two acquire loads for upstream netif reads: ${source_dir}")
    endif()
    string(FIND "${_coord_source}"
        "ml->config.netcheck_override_enabled ? ml_netcheck_pick_best_derp(ml) : 0"
        _disabled_netcheck_skip_offset)
    if(_disabled_netcheck_skip_offset EQUAL -1)
        message(FATAL_ERROR
            "MicroLink patched copy must skip DERP netcheck when override is disabled: ${source_dir}")
    endif()

    file(READ "${source_dir}/src/ml_wg_mgr.c" _wg_mgr_source)
    string(FIND "${_wg_mgr_source}"
        "__atomic_store_n(&ml->upstream_netif, (void *)upstream, __ATOMIC_RELEASE)"
        _upstream_atomic_store_offset)
    string(REGEX MATCHALL "upstream_netif" _wg_upstream_accesses "${_wg_mgr_source}")
    list(LENGTH _wg_upstream_accesses _wg_upstream_access_count)
    if(_upstream_atomic_store_offset EQUAL -1 OR
       NOT _wg_upstream_access_count EQUAL 1)
        message(FATAL_ERROR
            "MicroLink patched copy must use one release store for the upstream netif write: ${source_dir}")
    endif()
    string(REGEX MATCHALL
        "__atomic_load_n\\(&ml->wg_netif, __ATOMIC_ACQUIRE\\)"
        _wg_netif_atomic_loads
        "${_wg_mgr_source}"
    )
    list(LENGTH _wg_netif_atomic_loads _wg_netif_atomic_load_count)
    string(REGEX MATCHALL
        "__atomic_store_n\\(&ml->wg_netif, [^;]+, __ATOMIC_RELEASE\\)"
        _wg_netif_atomic_stores
        "${_wg_mgr_source}"
    )
    list(LENGTH _wg_netif_atomic_stores _wg_netif_atomic_store_count)
    string(FIND "${_wg_mgr_source}"
        "__atomic_store_n(&ml->wg_netif, (void *)netif, __ATOMIC_RELEASE)"
        _wg_publish_offset)
    string(FIND "${_wg_mgr_source}"
        "__atomic_store_n(&ml->wg_netif, NULL, __ATOMIC_RELEASE)"
        _wg_clear_offset)
    if(NOT _wg_netif_atomic_load_count EQUAL 2 OR
       NOT _wg_netif_atomic_store_count EQUAL 2 OR
       _wg_publish_offset EQUAL -1 OR _wg_clear_offset LESS _wg_publish_offset)
        message(FATAL_ERROR
            "MicroLink patched copy must acquire-load the WG netif getter/teardown and release-store publish/clear: ${source_dir}")
    endif()
    string(FIND "${_wg_mgr_source}"
        "add->added = netif_add(add->netif" _netif_add_offset)
    string(FIND "${_wg_mgr_source}"
        "netif->next = netif_list" _manual_netif_insert_offset)
    if(_netif_add_offset EQUAL -1 OR NOT _manual_netif_insert_offset EQUAL -1)
        message(FATAL_ERROR
            "MicroLink patched copy must register the WG netif through netif_add on the TCPIP thread: ${source_dir}")
    endif()
    string(REGEX MATCHALL
        "__atomic_load_n\\(&ml->selected_exit_ready, __ATOMIC_ACQUIRE\\)"
        _exit_ready_atomic_loads
        "${_wg_mgr_source}"
    )
    list(LENGTH _exit_ready_atomic_loads _exit_ready_atomic_load_count)
    string(REGEX MATCHALL
        "__atomic_store_n\\(&ml->selected_exit_ready, [^;]+, __ATOMIC_RELEASE\\)"
        _exit_ready_atomic_stores
        "${_wg_mgr_source}"
    )
    list(LENGTH _exit_ready_atomic_stores _exit_ready_atomic_store_count)
    if(NOT _exit_ready_atomic_load_count EQUAL 1 OR
       NOT _exit_ready_atomic_store_count EQUAL 3)
        message(FATAL_ERROR
            "MicroLink patched copy must acquire-load selected-exit readiness and release-store publication/start/teardown: ${source_dir}")
    endif()
    foreach(_readiness_marker IN ITEMS
            "peer->active && peer->vpn_ip == ml->config.exit_node_ip"
            "peer->online && peer->is_exit_node && peer->wg_peer_index >= 0"
            "peer->exit_route_attached"
            "p->exit_route_attached = (r == ERR_OK);"
            "ml->peers[idx].exit_route_attached = false;"
            "publish_selected_exit_ready(ml);")
        string(FIND "${_wg_mgr_source}" "${_readiness_marker}" _readiness_marker_offset)
        if(_readiness_marker_offset EQUAL -1)
            message(FATAL_ERROR
                "MicroLink patched copy is missing selected-exit readiness marker: ${_readiness_marker}")
        endif()
    endforeach()
    string(REGEX MATCHALL "publish_peer_snapshot\\(ml\\)"
        _snapshot_publish_calls "${_wg_mgr_source}")
    list(LENGTH _snapshot_publish_calls _snapshot_publish_call_count)
    if(NOT _snapshot_publish_call_count EQUAL 6)
        message(FATAL_ERROR
            "MicroLink patched copy must publish peer snapshots after preload and all public peer-state changes: ${source_dir}")
    endif()
    foreach(_snapshot_marker IN ITEMS
            "static void publish_peer_snapshot(microlink_t *ml)"
            "xSemaphoreTake(ml->peer_snapshot_lock, portMAX_DELAY);"
            "microlink_peer_info_t *info = &ml->peer_snapshot[i];"
            "xSemaphoreGive(ml->peer_snapshot_lock);")
        string(FIND "${_wg_mgr_source}" "${_snapshot_marker}" _snapshot_marker_offset)
        if(_snapshot_marker_offset EQUAL -1)
            message(FATAL_ERROR
                "MicroLink patched copy is missing peer snapshot publication marker: ${_snapshot_marker}")
        endif()
    endforeach()

    file(READ "${source_dir}/src/microlink.c" _core_source)
    foreach(_snapshot_reader_marker IN ITEMS
            "ml->peer_snapshot_lock = xSemaphoreCreateMutex();"
            "*info = ml->peer_snapshot[index];"
            "out->peer_count = ml->peer_snapshot_count;"
            "out->peer_online = ml->peer_snapshot_online;"
            "if (ml->peer_snapshot_lock) vSemaphoreDelete(ml->peer_snapshot_lock);")
        string(FIND "${_core_source}" "${_snapshot_reader_marker}"
            _snapshot_reader_marker_offset)
        if(_snapshot_reader_marker_offset EQUAL -1)
            message(FATAL_ERROR
                "MicroLink patched copy is missing peer snapshot reader/lifecycle marker: ${_snapshot_reader_marker}")
        endif()
    endforeach()
    string(FIND "${_core_source}" "Load or generate persistent keys"
        _key_load_failure_offset)
    if(_key_load_failure_offset EQUAL -1)
        message(FATAL_ERROR
            "MicroLink patched copy is missing the key-load failure path: ${source_dir}")
    endif()
    string(SUBSTRING "${_core_source}" ${_key_load_failure_offset} 384
        _key_load_failure_block)
    string(FIND "${_key_load_failure_block}"
        "vSemaphoreDelete(ml->peer_snapshot_lock);" _key_load_snapshot_delete_offset)
    string(FIND "${_key_load_failure_block}" "free(ml);"
        _key_load_context_free_offset)
    if(_key_load_snapshot_delete_offset EQUAL -1 OR
       _key_load_context_free_offset EQUAL -1 OR
       _key_load_snapshot_delete_offset GREATER _key_load_context_free_offset)
        message(FATAL_ERROR
            "MicroLink patched copy must delete the peer snapshot lock before freeing the context after key-load failure: ${source_dir}")
    endif()
    string(FIND "${_core_source}" "Failed to create event group"
        _event_failure_offset)
    if(_event_failure_offset EQUAL -1)
        message(FATAL_ERROR
            "MicroLink patched copy is missing the event-group failure cleanup path: ${source_dir}")
    endif()
    string(SUBSTRING "${_core_source}" ${_event_failure_offset} 256
        _event_failure_block)
    string(FIND "${_event_failure_block}"
        "vSemaphoreDelete(ml->peer_snapshot_lock);" _snapshot_delete_offset)
    string(FIND "${_event_failure_block}" "free(ml);" _context_free_offset)
    if(_snapshot_delete_offset EQUAL -1 OR _context_free_offset EQUAL -1 OR
       _snapshot_delete_offset GREATER _context_free_offset)
        message(FATAL_ERROR
            "MicroLink patched copy must delete the peer snapshot lock before freeing its context: ${source_dir}")
    endif()
    string(FIND "${_core_source}"
        "int left = __atomic_load_n(&ml->tasks_alive, __ATOMIC_SEQ_CST);"
        _stop_wait_result_offset)
    if(_stop_wait_result_offset EQUAL -1)
        message(FATAL_ERROR
            "MicroLink patched copy is missing the stop task-wait result: ${source_dir}")
    endif()
    string(SUBSTRING "${_core_source}" ${_stop_wait_result_offset} 768
        _stop_wait_result_block)
    string(FIND "${_stop_wait_result_block}" "ml->stop_incomplete = true;"
        _stop_incomplete_set_offset)
    string(FIND "${_stop_wait_result_block}" "return ESP_ERR_TIMEOUT;"
        _stop_timeout_return_offset)
    string(FIND "${_stop_wait_result_block}" "ml->stop_incomplete = false;"
        _stop_incomplete_clear_offset)
    if(_stop_incomplete_set_offset EQUAL -1 OR
       _stop_timeout_return_offset EQUAL -1 OR
       _stop_incomplete_clear_offset EQUAL -1 OR
       _stop_incomplete_set_offset GREATER _stop_timeout_return_offset OR
       _stop_timeout_return_offset GREATER _stop_incomplete_clear_offset)
        message(FATAL_ERROR
            "MicroLink patched stop must return timeout for live tasks and clear stop_incomplete after a completed retry: ${source_dir}")
    endif()
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
    if(NOT EXISTS "${source_dir}/include/microlink.h" OR
       NOT EXISTS "${source_dir}/include/microlink_internal.h" OR
       NOT EXISTS "${source_dir}/src/ml_stun.c" OR
       NOT EXISTS "${source_dir}/src/ml_coord.c" OR
       NOT EXISTS "${source_dir}/src/microlink.c" OR
       NOT EXISTS "${source_dir}/src/ml_peer_nvs.c" OR
       NOT EXISTS "${source_dir}/src/ml_wg_mgr.c")
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
    file(SHA256 "${source_dir}/src/ml_wg_mgr.c" _wg_mgr_sha256)
    file(SHA256 "${source_dir}/include/microlink.h" _public_header_sha256)
    file(SHA256 "${source_dir}/include/microlink_internal.h" _internal_header_sha256)
    if("${_public_header_sha256}" STREQUAL "${_MICROLINK_PATCHED_PUBLIC_HEADER_SHA256}" AND
       "${_internal_header_sha256}" STREQUAL "${_MICROLINK_PATCHED_INTERNAL_HEADER_SHA256}" AND
       "${_stun_sha256}" STREQUAL "${_MICROLINK_PATCHED_STUN_SHA256}" AND
       "${_coord_sha256}" STREQUAL "${_MICROLINK_PATCHED_COORD_SHA256}" AND
       "${_core_sha256}" STREQUAL "${_MICROLINK_PATCHED_CORE_SHA256}" AND
       "${_peer_nvs_sha256}" STREQUAL "${_MICROLINK_PATCHED_PEER_NVS_SHA256}" AND
       "${_wg_mgr_sha256}" STREQUAL "${_MICROLINK_PATCHED_WG_MGR_SHA256}")
        _microlink_assert_upstream_bind_patch("${source_dir}")
        message(STATUS "MicroLink upstream-bind patch already applied and verified at ${source_dir}")
        return()
    endif()
    if(NOT "${_public_header_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_PUBLIC_HEADER_SHA256}" OR
       NOT "${_internal_header_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_INTERNAL_HEADER_SHA256}" OR
       NOT "${_stun_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_STUN_SHA256}" OR
       NOT "${_coord_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_COORD_SHA256}" OR
       NOT "${_core_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_CORE_SHA256}" OR
       NOT "${_peer_nvs_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_PEER_NVS_SHA256}" OR
       NOT "${_wg_mgr_sha256}" STREQUAL "${_MICROLINK_ORIGINAL_WG_MGR_SHA256}")
        message(FATAL_ERROR
            "MicroLink sources are neither the exact original nor patched revisions at ${source_dir}.\n"
            "Actual include/microlink.h SHA256: ${_public_header_sha256}\n"
            "Actual include/microlink_internal.h SHA256: ${_internal_header_sha256}\n"
            "Actual ml_stun.c SHA256: ${_stun_sha256}\n"
            "Actual ml_coord.c SHA256: ${_coord_sha256}\n"
            "Actual microlink.c SHA256: ${_core_sha256}\n"
            "Actual ml_peer_nvs.c SHA256: ${_peer_nvs_sha256}\n"
            "Actual ml_wg_mgr.c SHA256: ${_wg_mgr_sha256}")
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
    file(SHA256 "${source_dir}/src/ml_wg_mgr.c" _wg_mgr_sha256)
    file(SHA256 "${source_dir}/include/microlink.h" _public_header_sha256)
    file(SHA256 "${source_dir}/include/microlink_internal.h" _internal_header_sha256)
    if(NOT "${_public_header_sha256}" STREQUAL "${_MICROLINK_PATCHED_PUBLIC_HEADER_SHA256}" OR
       NOT "${_internal_header_sha256}" STREQUAL "${_MICROLINK_PATCHED_INTERNAL_HEADER_SHA256}" OR
       NOT "${_stun_sha256}" STREQUAL "${_MICROLINK_PATCHED_STUN_SHA256}" OR
       NOT "${_coord_sha256}" STREQUAL "${_MICROLINK_PATCHED_COORD_SHA256}" OR
       NOT "${_core_sha256}" STREQUAL "${_MICROLINK_PATCHED_CORE_SHA256}" OR
       NOT "${_peer_nvs_sha256}" STREQUAL "${_MICROLINK_PATCHED_PEER_NVS_SHA256}" OR
       NOT "${_wg_mgr_sha256}" STREQUAL "${_MICROLINK_PATCHED_WG_MGR_SHA256}")
        message(FATAL_ERROR
            "MicroLink patch output does not match the exact expected patched revisions at ${source_dir}.\n"
            "Actual include/microlink.h SHA256: ${_public_header_sha256}\n"
            "Actual include/microlink_internal.h SHA256: ${_internal_header_sha256}\n"
            "Actual ml_stun.c SHA256: ${_stun_sha256}\n"
            "Actual ml_coord.c SHA256: ${_coord_sha256}\n"
            "Actual microlink.c SHA256: ${_core_sha256}\n"
            "Actual ml_peer_nvs.c SHA256: ${_peer_nvs_sha256}\n"
            "Actual ml_wg_mgr.c SHA256: ${_wg_mgr_sha256}")
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
