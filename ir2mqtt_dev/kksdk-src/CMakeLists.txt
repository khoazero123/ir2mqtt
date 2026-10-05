cmake_minimum_required(VERSION 3.20)

project(kksdk_host_package
    VERSION 0.1.0
    LANGUAGES C CXX)

option(KKSDK_BUILD_HOST_TESTS "Build host smoke tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(KKSDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(KKSDK_LIB "${KKSDK_ROOT}/libkksdk")
set(LUAJAVA_ROOT "${KKSDK_ROOT}/libluajava")
set(LUA_SRC "${LUAJAVA_ROOT}/lua-5.4.6/src")

file(GLOB KKSDK_LEGACY_CODEC_SOURCES CONFIGURE_DEPENDS
    "${KKSDK_LIB}/legacy_codec_*.cpp")

add_library(kksdk_host SHARED
    "${KKSDK_LIB}/codehelper_jni.cpp"
    "${KKSDK_LIB}/codehelper_encode_engine.cpp"
    "${KKSDK_LIB}/irdevice_jni.cpp"
    "${KKSDK_LIB}/kk_body.c"
    "${KKSDK_LIB}/streamhelper_transform.cpp"
    "${KKSDK_LIB}/streamhelper_legacy_lzma.cpp"
    "${KKSDK_LIB}/kksdk_host_api.cpp"
    ${KKSDK_LEGACY_CODEC_SOURCES})

target_include_directories(kksdk_host
    PUBLIC
        "$<BUILD_INTERFACE:${KKSDK_ROOT}/include>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${KKSDK_LIB}")

target_compile_definitions(kksdk_host
    PRIVATE
        KKSDK_HOST_CORE_ONLY=1
        KKSDK_ENABLE_OEM_LZMA_REFERENCE=0
        KKSDK_ENFORCE_PACKAGE_VERIFY=0)

if(MSVC)
    target_compile_definitions(kksdk_host PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    set_target_properties(kksdk_host PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

set(LUA_CORE_SOURCES
    lapi.c lauxlib.c lbaselib.c lcode.c lcorolib.c lctype.c
    ldblib.c ldebug.c ldo.c ldump.c lfunc.c lgc.c linit.c
    liolib.c llex.c lmathlib.c lmem.c loadlib.c lobject.c
    lopcodes.c loslib.c lparser.c lstate.c lstring.c lstrlib.c
    ltable.c ltablib.c ltm.c lundump.c lutf8lib.c lvm.c lzio.c)

list(TRANSFORM LUA_CORE_SOURCES PREPEND "${LUA_SRC}/")

add_library(luajava_host SHARED
    "${LUAJAVA_ROOT}/luajava_core.c"
    ${LUA_CORE_SOURCES})

target_include_directories(luajava_host
    PUBLIC
        "$<BUILD_INTERFACE:${LUAJAVA_ROOT}>"
        "$<BUILD_INTERFACE:${LUA_SRC}>"
        "$<INSTALL_INTERFACE:include>"
    PRIVATE
        "${LUAJAVA_ROOT}"
        "${LUA_SRC}")

if(WIN32)
    target_compile_definitions(luajava_host PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_definitions(luajava_host PRIVATE LUA_USE_LINUX)
    target_link_libraries(luajava_host PRIVATE m dl)
endif()

if(MSVC)
    set_target_properties(luajava_host PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

if(KKSDK_BUILD_HOST_TESTS)
    enable_testing()
    add_executable(kksdk_host_smoke
        "${CMAKE_CURRENT_SOURCE_DIR}/tools/host_smoke/kksdk_host_smoke.cpp")
    target_link_libraries(kksdk_host_smoke PRIVATE kksdk_host luajava_host)
    target_include_directories(kksdk_host_smoke PRIVATE
        "${KKSDK_ROOT}/include"
        "${LUAJAVA_ROOT}")
    add_test(NAME kksdk_host_smoke COMMAND kksdk_host_smoke)
endif()

install(TARGETS kksdk_host luajava_host
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib)

install(FILES
    "${KKSDK_ROOT}/include/kksdk/kksdk_host.h"
    "${KKSDK_ROOT}/include/kksdk/kk_crypto.h"
    DESTINATION include/kksdk)

install(FILES
    "${LUAJAVA_ROOT}/luajava_core.h"
    DESTINATION include/luajava)
