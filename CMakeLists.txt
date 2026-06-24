cmake_minimum_required(VERSION 3.14)
project(wow_optimize LANGUAGES C CXX)

# ============================================================================
# Whole-program / link-time optimization (LTO) for the DLL.
#   -DWO_ENABLE_LTO=ON  ->  /GL (whole-program) + /LTCG (link-time codegen)
# Default OFF: LTO re-generates code across the whole DLL and can shift the
# exact codegen we are currently stabilizing crashes against. Turn it on only
# after the build is confirmed stable in-game. Marginal win on a ~340KB DLL
# next to wow.exe (which we cannot recompile), but free once stable.
#
# PROFILE-GUIDED OPTIMIZATION (PGO) is a manual 2-phase process because it
# needs YOUR gameplay profile (there is no profile to bake in from here):
#   1. Configure with -DWO_ENABLE_LTO=ON, then link instrumented:
#        add  /GENPROFILE  to the linker (set WO_PGO=GEN below), build, and
#        play a representative session (login -> world -> a raid). This writes
#        wow_optimize!1.pgc next to the DLL.
#   2. Re-link optimized with  /USEPROFILE  (set WO_PGO=USE), which folds the
#        .pgc counts back in. Expect single-digit % on hot DLL paths only.
# ============================================================================
option(WO_ENABLE_LTO "Enable /GL + /LTCG whole-program optimization for wow_optimize" OFF)
set(WO_PGO "OFF" CACHE STRING "PGO phase for wow_optimize: OFF | GEN | USE")

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

# MinHook — lightweight function hooking library
FetchContent_Declare(
    minhook
    GIT_REPOSITORY https://github.com/TsudaKageyu/minhook.git
    GIT_TAG        master
)
FetchContent_MakeAvailable(minhook)

# mimalloc — Microsoft's high-performance allocator
FetchContent_Declare(
    mimalloc
    GIT_REPOSITORY https://github.com/microsoft/mimalloc.git
    GIT_TAG        v3.3.2
)
set(MI_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(MI_BUILD_TESTS  OFF CACHE BOOL "" FORCE)
set(MI_OVERRIDE     OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mimalloc)

# === Main optimization DLL ===
add_library(wow_optimize SHARED
    src/dllmain.cpp
    src/crt_mem_fastpath.cpp
    src/lua_optimize.cpp
    src/lua_optimize.h
    src/combatlog_optimize.cpp
    src/combatlog_optimize.h
    src/combatlog_buffer.cpp
    src/combatlog_buffer.h
    src/addon_dispatcher.cpp
    src/addon_dispatcher.h
    src/mpq_prefetch.cpp
    src/mpq_prefetch.h
    src/obj_vis_cache.cpp
    src/obj_vis_cache.h
    src/nameplate_batch.cpp
    src/nameplate_batch.h
    src/ui_cache.cpp
    src/ui_cache.h
    src/version.h
    src/api_cache.cpp
    src/api_cache.h
    src/lua_fastpath.cpp
    src/lua_fastpath.h
    src/lua_internals.cpp
    src/lua_internals.h
    src/crash_dumper.cpp
    src/render_null_guard.cpp
    src/cvar_watchdog.cpp
    src/cvar_watchdog.h
    src/lua_precall_cache.cpp
    src/lua_precall_cache.h
    src/lua_table_fast.cpp
    src/lua_table_fast.h
    src/lua_hget_fast.cpp
    src/lua_hget_fast.h
    src/lua_checknumber_fast.cpp
    src/lua_checknumber_fast.h
    src/lua_checkstring_fast.cpp
    src/lua_checkstring_fast.h
    src/lua_optnumber_fast.cpp
    src/lua_optnumber_fast.h
    src/lua_optstring_fast.cpp
    src/lua_optstring_fast.h
    src/lua_tolstring_fast.cpp
    src/lua_tolstring_fast.h
    src/lua_argcheck_fast.cpp
    src/lua_argcheck_fast.h
    src/lua_typename_fast.cpp
    src/lua_typename_fast.h
    src/lua_getlocal_fast.cpp
    src/lua_getlocal_fast.h
    src/lua_setlocal_fast.cpp
    src/lua_setlocal_fast.h
    src/lua_setupvalue_fast.cpp
    src/lua_setupvalue_fast.h
    src/lua_getinfo_fast.cpp
    src/lua_getinfo_fast.h
    src/lua_error_fast.cpp
    src/lua_error_fast.h
    src/lua_lessthan_fast.cpp
    src/lua_lessthan_fast.h
    src/lua_gc_fast.cpp
    src/lua_gc_fast.h
    src/lua_xpcall_fast.cpp
    src/lua_xpcall_fast.h
    src/lua_getmetafield_fast.cpp
    src/lua_getmetafield_fast.h
    src/lua_where_fast.cpp
    src/lua_where_fast.h
    src/lua_settable_fast.cpp
    src/lua_settable_fast.h
    src/lua_pushfstring_fast.cpp
    src/lua_pushfstring_fast.h
    src/lua_gettable_fast.cpp
    src/lua_gettable_fast.h
    src/lua_concat_fast.cpp
    src/lua_concat_fast.h
    src/lua_rawseti_fast.cpp
    src/lua_rawseti_fast.h
    src/lua_setfield_fast.cpp
    src/lua_setfield_fast.h
    src/render_null_guard.h
    src/cvar_null_guard.cpp
    src/cvar_null_guard.h
    src/d3d_evict_patch.cpp
    src/d3d_evict_patch.h
    src/strncmp_null_guard.cpp
    src/strncmp_null_guard.h
    src/frame_throttle.cpp
    src/frame_throttle.h
    src/ui_frame_batch.cpp
    src/ui_frame_batch.h
    src/lua_vm_cache.cpp
    src/lua_vm_cache.h
    src/tooltip_cache.cpp
    src/tooltip_cache.h
    src/lua_settable_cache.cpp
    src/lua_settable_cache.h
    src/regex_cache.cpp
    src/regex_cache.h
    src/trig_lut.cpp
    src/trig_lut.h
    src/data_caches.cpp
    src/data_caches.h
    src/compute_caches.cpp
    src/compute_caches.h
    src/spell_cache.cpp
    src/spell_cache.h
    src/lua_bytecode_cache.cpp
    src/lua_bytecode_cache.h
    src/addon_preload.cpp
    src/addon_preload.h
    src/strstr_fast.cpp
    src/strstr_fast.h
    src/strtod_fast.cpp
    src/strtod_fast.h
    src/crt_char_fast.cpp
    src/crt_char_fast.h
    src/crt_wchar_fast.cpp
    src/crt_wchar_fast.h
    src/tls_cache.cpp
    src/tls_cache.h
    src/stream_cache.cpp
    src/stream_cache.h
    src/lua_this_cache.cpp
    src/lua_this_cache.h
    src/io_cache.cpp
    src/io_cache.h
    src/lua_global_cache.cpp
    src/lua_global_cache.h
    src/hot_functions.cpp
    src/hot_functions.h
    src/crt_memcpy_fast.cpp
    src/crt_memcpy_fast.h
    src/frame_script_dispatch.cpp
    src/frame_script_dispatch.h
    src/fast_strncmp.cpp
    src/fast_strncmp.h
    src/crt_free_hook.cpp
    src/lock_tuning.cpp
    src/texcache_tuning.cpp
    src/crt_free_hook.h
    src/aligned_alloc_cache.cpp
    src/aligned_alloc_cache.h
    src/datastore_fastpath.cpp
    src/datastore_fastpath.h
    src/string_ops_fast.cpp
    src/string_ops_fast.h
    src/lua_tonumber_cache.cpp
    src/lua_tonumber_cache.h
    src/lua_checknumber_cache.cpp
    src/lua_checknumber_cache.h
    src/lua_pushstring_cache.cpp
    src/lua_pushstring_cache.h
    src/object_accessor_cache.cpp
    src/object_accessor_cache.h
    src/format_validator_cache.cpp
    src/format_validator_cache.h
    src/heap_compactor.cpp
    src/heap_compactor.h
    src/memory_pressure_governor.cpp
    src/memory_pressure_governor.h
    src/lua_tonumber_fast.cpp
    src/lua_pushnumber_fast.cpp
    src/gettime_fast.cpp
    src/lua_pushvalue_fast.cpp
    src/render_state_dedup.cpp
    src/event_name_hash.cpp
    src/cdatastore_batch.cpp
    src/lua_tonumber_fast.h
    src/strcat_fast.cpp
    src/strcat_fast.h
    src/script_handler_cache.cpp
    src/script_handler_cache.h
    src/dbc_lookup_cache.cpp
    src/dbc_lookup_cache.h
    src/event_dispatch_cache.cpp
    src/event_dispatch_cache.h
    src/event_name_cache.cpp
    src/event_name_cache.h
    src/lua_getstr_inline.cpp
    src/lua_rawgeti_inline.cpp
    src/lua_rawget_inline.cpp
    src/lua_toboolean_inline.cpp
    src/lua_objlen_inline.cpp
    src/lua_getstr_inline.h
    src/lua_rawget_inline.h
    src/lua_toboolean_inline.h
    src/lua_gettable_safety.cpp
    src/lua_gettable_safety.h
    src/lua_newkey_safety.cpp
    src/lua_newkey_safety.h
    src/lua_vm_engine.cpp
    src/lua_vm_engine.h
    src/lua_vm_phase3.cpp
    src/lua_vm_phase3.h
    src/lua_gettable_cache.cpp
    src/lua_gettable_cache.h
    src/saved_vars_async.cpp
    src/saved_vars_async.h
    src/lua_bytecode_pre_compiler.cpp
    src/lua_bytecode_pre_compiler.h
    src/hook_prefetch.cpp
    src/hook_prefetch.h
    src/hot_patch.cpp
    src/hot_patch.h
    src/infra_patch.cpp
    src/infra_patch.h
    src/wow_opt_hooks.cpp
    src/wow_opt_hooks.h
    src/wow_perf_hooks.cpp
    src/wow_perf_hooks.h
    src/wow_extended_hooks.cpp
    src/wow_extended_hooks.h
    src/wow_subsystem_hooks.cpp
    src/wow_subsystem_hooks.h
    src/wow_memory_opt.cpp
    src/wow_memory_opt.h
    src/wow_source_opt.cpp
    src/wow_source_opt.h
    src/tls_object_cache.cpp
    src/tls_object_cache.h
    src/hooks_render.cpp
    src/hooks_render.h
    src/hooks_simd.cpp
    src/hooks_simd.h
    src/hooks_logic.cpp
    src/hooks_logic.h
    src/hooks_memory.cpp
    src/hooks_memory.h
    src/hooks_async.cpp
    src/hooks_async.h
    src/d3d9_state_manager.cpp
    src/d3d9_state_manager.h
    src/event_coalescer.cpp
    src/event_coalescer.h
    src/luaS_newlstr_sse2.cpp
    src/luaS_newlstr_sse2.h
    src/version_checker.cpp
    src/version_checker.h
    src/unitaura_fastpath.cpp
    src/unitaura_fastpath.h
    src/network_guid_sse2.cpp
    src/network_guid_sse2.h
    src/matrix_copy_sse2.cpp
    src/matrix_copy_sse2.h
    src/lua_numconv_fast.cpp
    src/lua_numconv_fast.h
    src/lua_stack_fast.cpp
    src/lua_stack_fast.h
    src/ui_accessor_fast.cpp
    src/ui_accessor_fast.h
    src/font_metrics_fast.cpp
    src/font_metrics_fast.h
    src/sampling_profiler.cpp

    src/sampling_profiler.h
)

target_link_libraries(wow_optimize PRIVATE
    minhook
    mimalloc-static
    psapi
    ws2_32
    version
    winhttp
)

target_include_directories(wow_optimize PRIVATE
    ${minhook_SOURCE_DIR}/include
    ${mimalloc_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(wow_optimize PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_definitions(wow_optimize PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
)

# --- LTO / PGO (opt-in; see header block) ---
if(WO_ENABLE_LTO AND MSVC)
    target_compile_options(wow_optimize PRIVATE $<$<CONFIG:Release>:/GL>)
    if(WO_PGO STREQUAL "GEN")
        target_link_options(wow_optimize PRIVATE $<$<CONFIG:Release>:/LTCG:PGINSTRUMENT /GENPROFILE>)
        message(STATUS "wow_optimize: LTO + PGO INSTRUMENT (play a session to emit .pgc, then reconfigure WO_PGO=USE)")
    elseif(WO_PGO STREQUAL "USE")
        target_link_options(wow_optimize PRIVATE $<$<CONFIG:Release>:/LTCG:PGOPTIMIZE /USEPROFILE>)
        message(STATUS "wow_optimize: LTO + PGO OPTIMIZE (folding in collected .pgc profile)")
    else()
        target_link_options(wow_optimize PRIVATE $<$<CONFIG:Release>:/LTCG>)
        message(STATUS "wow_optimize: LTO (/GL + /LTCG) enabled")
    endif()
endif()

# === Proxy DLL (version.dll) — auto-loader ===
add_library(version SHARED
    src/version_proxy.cpp
    src/version_exports.def
)

set_target_properties(version PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "version"
)

target_compile_definitions(version PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# === Loader EXE (wow_loader.exe) — works with any Wow.exe ===
add_executable(wow_loader WIN32
    src/wow_loader.cpp
)

set_target_properties(wow_loader PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    OUTPUT_NAME "wow_loader"
)

target_compile_definitions(wow_loader PRIVATE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)