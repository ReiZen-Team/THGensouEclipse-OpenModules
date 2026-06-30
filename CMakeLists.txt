cmake_minimum_required(VERSION 3.18)
project(OpenModules LANGUAGES CXX)

# ══════════════════════════════════════════════════════════════════════════════
#  OpenModules SDK  —  build system for dynamic (.wasm) modules
#  See docs/en.md / docs/vi.md for the full toolchain & build reference.
# ══════════════════════════════════════════════════════════════════════════════

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS        OFF)

option(KMOD_BUILD_EXAMPLE  "Build the bundled HelloWorld example module" OFF)
option(KMOD_SIGN_MODULES   "Sign .wasm files (requires sign.py)" ON)
option(KMOD_STRIP_MODULES  "Strip debug info / name section from built .wasm modules" ON)

# ── VM module toolchain (wasm32 via wasi-sdk / system clang) ────────────────────
set(WASI_SDK     "" CACHE PATH     "Path to a full wasi-sdk install prefix (contains bin/clang++)")
set(WASI_CLANGXX "" CACHE FILEPATH "clang++ able to target wasm32 (defaults to wasi-sdk's, else system clang++)")
set(WASI_SYSROOT "" CACHE PATH     "wasi-sysroot (auto from WASI_SDK; required with a system clang++)")
set(WAMRC        "" CACHE FILEPATH "Path to the WAMR 'wamrc' AOT compiler (optional)")
set(WAMRC_TARGET "aarch64v8" CACHE STRING "wamrc --target for the device AOT build")
set(WASI_TARGET  "wasm32-wasi" CACHE STRING "clang --target triple (e.g. wasm32-wasi, wasm32-wasip1)")
set(WASI_EH      "noeh" CACHE STRING "exception variant subdir in a multi-variant sysroot: eh | noeh")

if(WASI_SDK AND EXISTS "${WASI_SDK}/bin/clang++")
    set(WASI_CLANGXX "${WASI_SDK}/bin/clang++")
    if(NOT WASI_SYSROOT)
        if(EXISTS "${WASI_SDK}/share/wasi-sysroot")
            set(WASI_SYSROOT "${WASI_SDK}/share/wasi-sysroot")
        elseif(EXISTS "${WASI_SDK}/share/wasi-sysroot/include")
            set(WASI_SYSROOT "${WASI_SDK}/share/wasi-sysroot")
        endif()
    endif()
endif()
if(NOT WASI_CLANGXX)
    find_program(WASI_CLANGXX NAMES clang++)
endif()
if(NOT WASI_CLANGXX)
    message(FATAL_ERROR
        "No wasm-capable clang++ found.\n"
        "  Desktop : -DWASI_SDK=/opt/wasi-sdk\n"
        "  Termux  : pkg install clang lld, then\n"
        "            -DWASI_CLANGXX=$(command -v clang++) -DWASI_SYSROOT=\$HOME/wasi/wasi-sysroot")
endif()
if(WASI_SYSROOT)
    set(WASI_SYSROOT_FLAG "--sysroot=${WASI_SYSROOT}")
else()
    set(WASI_SYSROOT_FLAG "")
    message(WARNING "WASI_SYSROOT not set — relying on ${WASI_CLANGXX} having a default wasm sysroot.")
endif()

set(WASI_EXTRA_FLAGS "")
if(WASI_SYSROOT)
    execute_process(COMMAND ${WASI_CLANGXX} -print-resource-dir
                    OUTPUT_VARIABLE WASI_RESOURCE_DIR
                    OUTPUT_STRIP_TRAILING_WHITESPACE)

    list(APPEND WASI_EXTRA_FLAGS "-nostdinc")

    foreach(_d
        "${WASI_SYSROOT}/include/${WASI_TARGET}/${WASI_EH}/c++/v1"
        "${WASI_SYSROOT}/include/${WASI_TARGET}/c++/v1"
        "${WASI_SYSROOT}/include/c++/v1")
        if(EXISTS "${_d}")
            list(APPEND WASI_EXTRA_FLAGS "-isystem" "${_d}")
            break()
        endif()
    endforeach()

    if(WASI_RESOURCE_DIR AND EXISTS "${WASI_RESOURCE_DIR}/include")
        list(APPEND WASI_EXTRA_FLAGS "-isystem" "${WASI_RESOURCE_DIR}/include")
    endif()

    foreach(_d
        "${WASI_SYSROOT}/include/${WASI_TARGET}"
        "${WASI_SYSROOT}/include")
        if(EXISTS "${_d}")
            list(APPEND WASI_EXTRA_FLAGS "-isystem" "${_d}")
            break()
        endif()
    endforeach()

    foreach(_l
        "${WASI_SYSROOT}/lib/${WASI_TARGET}/${WASI_EH}"
        "${WASI_SYSROOT}/lib/${WASI_TARGET}")
        if(EXISTS "${_l}")
            list(APPEND WASI_EXTRA_FLAGS "-L${_l}")
        endif()
    endforeach()
endif()
message(STATUS "VM module toolchain: ${WASI_CLANGXX}  sysroot='${WASI_SYSROOT}'  target=${WASI_TARGET}")

# ── Symbol visibility ─────────────────────────────────────────────────────────
set(CMAKE_C_VISIBILITY_PRESET       hidden)
set(CMAKE_CXX_VISIBILITY_PRESET     hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN YES)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
find_package(nlohmann_json 3.9 QUIET)
if(NOT nlohmann_json_FOUND)
    find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp
        PATHS /usr/include /usr/local/include
              $ENV{NLOHMANN_JSON_DIR}/include)
    if(NLOHMANN_JSON_INCLUDE_DIR)
        add_library(nlohmann_json::nlohmann_json INTERFACE IMPORTED)
        target_include_directories(nlohmann_json::nlohmann_json
            INTERFACE "${NLOHMANN_JSON_INCLUDE_DIR}")
        message(STATUS "nlohmann/json: ${NLOHMANN_JSON_INCLUDE_DIR} (manual)")
    else()
        message(FATAL_ERROR
            "nlohmann/json not found.\n"
            "  Debian/Ubuntu : sudo apt install nlohmann-json3-dev\n"
            "  Homebrew      : brew install nlohmann-json\n"
            "  Manual        : set NLOHMANN_JSON_DIR to the install prefix")
    endif()
endif()

# ── Python 3 + sign.py ────────────────────────────────────────────────────────
find_package(Python3 COMPONENTS Interpreter QUIET)

set(_SIGN_PY "${CMAKE_CURRENT_SOURCE_DIR}/tools/sign.py")
if(KMOD_SIGN_MODULES AND NOT EXISTS "${_SIGN_PY}")
    message(WARNING
        "sign.py not found at ${_SIGN_PY}.\n"
        "Modules will be built but NOT signed — the host kernel will refuse to load them.\n"
        "Set KMOD_SIGN_MODULES=OFF to suppress this warning.")
    set(_SIGN_PY "")
endif()

set(_HAVE_SIGN_KEY FALSE)
if(DEFINED ENV{KMOD_SIGNING_KEY} AND NOT "$ENV{KMOD_SIGNING_KEY}" STREQUAL "")
    set(_HAVE_SIGN_KEY TRUE)
endif()

# ── KMOD_VERSION string ───────────────────────────────────────────────────────
if(NOT DEFINED KMOD_VERSION_STRING)
    set(KMOD_VERSION_STRING "1.0.0")
endif()

# ══════════════════════════════════════════════════════════════════════════════
#  add_kmod(TARGET_NAME  SOURCE_FILE  [extra_sources...])
# ══════════════════════════════════════════════════════════════════════════════
if(NOT NLOHMANN_JSON_INCLUDE_DIR AND TARGET nlohmann_json::nlohmann_json)
    get_target_property(NLOHMANN_JSON_INCLUDE_DIR nlohmann_json::nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
endif()

function(add_kmod TARGET_NAME SOURCE_FILE)
    set(_wasm "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.wasm")
    set(_srcs ${SOURCE_FILE} ${ARGN})

    set(_strip_flag "")
    if(KMOD_STRIP_MODULES)
        set(_strip_flag "-Wl,--strip-all")
    endif()

    set(_inc "-I${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(NLOHMANN_JSON_INCLUDE_DIR)
        foreach(_n ${NLOHMANN_JSON_INCLUDE_DIR})
            list(APPEND _inc "-idirafter" "${_n}")
        endforeach()
    endif()

    add_custom_command(
        OUTPUT  ${_wasm}
        COMMAND ${CMAKE_COMMAND} -E env
                --unset=CPATH --unset=C_INCLUDE_PATH --unset=CPLUS_INCLUDE_PATH
                ${WASI_CLANGXX}
                --no-default-config
                --target=${WASI_TARGET} -mexec-model=reactor
                ${WASI_SYSROOT_FLAG}
                ${WASI_EXTRA_FLAGS}
                -std=c++17 -Oz -fno-rtti
                -fno-exceptions -DJSON_NOEXCEPTION
                -DKMOD_VERSION_STRING="${KMOD_VERSION_STRING}"
                ${_inc}
                ${_strip_flag}
                -Wl,--no-entry
                -Wl,--allow-undefined
                -Wl,--export=__kmod_info
                -Wl,--export=on_load
                -Wl,--export=on_unload
                -Wl,--export=__kmod_dispatch
                -Wl,--export=malloc
                -Wl,--export=free
                -o ${_wasm} ${_srcs}
        DEPENDS ${_srcs}
        COMMENT "Building WASM module ${TARGET_NAME}.wasm"
        VERBATIM
    )

    set(_final ${_wasm})

    if(WAMRC AND EXISTS "${WAMRC}")
        set(_aot "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.aot")
        add_custom_command(
            OUTPUT  ${_aot}
            COMMAND ${WAMRC} --target=${WAMRC_TARGET} -o ${_aot} ${_wasm}
            DEPENDS ${_wasm}
            COMMENT "AOT-compiling ${TARGET_NAME}.aot (${WAMRC_TARGET})"
            VERBATIM
        )
        set(_final ${_aot})
    endif()

    if(KMOD_SIGN_MODULES AND Python3_FOUND AND _SIGN_PY AND _HAVE_SIGN_KEY)
        add_custom_command(
            OUTPUT  ${_final}.signed
            COMMAND ${Python3_EXECUTABLE} "${_SIGN_PY}" ${_final}
            COMMAND ${CMAKE_COMMAND} -E touch ${_final}.signed
            DEPENDS ${_final}
            COMMENT "Signing ${TARGET_NAME}"
            VERBATIM
        )
        add_custom_target(${TARGET_NAME} ALL DEPENDS ${_final}.signed)
    else()
        add_custom_target(${TARGET_NAME} ALL DEPENDS ${_final})
        if(KMOD_SIGN_MODULES AND NOT _HAVE_SIGN_KEY)
            message(WARNING "${TARGET_NAME}: KMOD_SIGN_MODULES=ON but no signing key — set the KMOD_SIGNING_KEY env var. Building UNSIGNED; the host will reject it.")
        elseif(KMOD_SIGN_MODULES)
            message(WARNING "${TARGET_NAME}: sign.py/Python3 unavailable — module unsigned; host will reject it.")
        endif()
    endif()

    message(STATUS "add_kmod: ${TARGET_NAME} -> ${TARGET_NAME}.wasm")
endfunction()

# ══════════════════════════════════════════════════════════════════════════════
#  Example module
# ══════════════════════════════════════════════════════════════════════════════
if(KMOD_BUILD_EXAMPLE)
    add_kmod(HelloWorld "${CMAKE_CURRENT_SOURCE_DIR}/example/HelloWorld.cpp")
endif()

# ── Auto-scan: every .cpp in Modules/ is built as <name>.wasm ──────────────────
file(GLOB KMOD_MODULE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Modules/*.cpp")
foreach(_mod_src ${KMOD_MODULE_SOURCES})
    get_filename_component(_mod_name "${_mod_src}" NAME_WE)
    add_kmod(${_mod_name} "${_mod_src}")
endforeach()
