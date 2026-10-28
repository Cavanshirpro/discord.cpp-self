# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
# Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
cmake_minimum_required(VERSION 3.25)
project(discord_cpp_self VERSION 0.2.0
    DESCRIPTION "A C++20 library for Discord user accounts"
    HOMEPAGE_URL "https://github.com/Cavanshirpro/discord.cpp-self" LANGUAGES CXX)

option(DISCORD_SELF_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(DISCORD_SELF_BUILD_TESTS "Build offline tests" ${PROJECT_IS_TOP_LEVEL})
option(DISCORD_SELF_BUILD_LIVE_EXAMPLES "Build manual examples that use real accounts" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
option(DISCORD_SELF_NATIVE_MEDIA "Build the C++ RTP/DAVE media backend (libdave, OpenSSL, Opus)" OFF)

find_package(CURL 7.86 REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(discord_cpp_self
    src/models.cpp src/http.cpp src/gateway.cpp src/client.cpp src/commands.cpp src/media.cpp)
add_library(discord::self ALIAS discord_cpp_self)
set_target_properties(discord_cpp_self PROPERTIES EXPORT_NAME self WINDOWS_EXPORT_ALL_SYMBOLS ON
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
if(MINGW AND BUILD_SHARED_LIBS)
    # MinGW disables implicit exports when libdave's C API declares dllexport.
    # WINDOWS_EXPORT_ALL_SYMBOLS applies to MS-compatible toolchains only.
    target_link_options(discord_cpp_self PRIVATE -Wl,--export-all-symbols)
endif()
target_compile_features(discord_cpp_self PUBLIC cxx_std_20)
target_include_directories(discord_cpp_self PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(discord_cpp_self PUBLIC nlohmann_json::nlohmann_json
    PRIVATE CURL::libcurl ZLIB::ZLIB Threads::Threads)
if(WIN32)
    target_link_libraries(discord_cpp_self PRIVATE ws2_32)
endif()
if(DISCORD_SELF_NATIVE_MEDIA)
    find_package(OpenSSL 3 REQUIRED)
    find_package(MLSPP REQUIRED)
    find_path(DISCORD_SELF_DAVE_INCLUDE_DIR dave/dave.h REQUIRED)
    find_library(DISCORD_SELF_DAVE_LIBRARY NAMES dave libdave REQUIRED)
    find_path(DISCORD_SELF_OPUS_INCLUDE_DIR opus/opus.h REQUIRED)
    find_library(DISCORD_SELF_OPUS_LIBRARY NAMES opus REQUIRED)
    target_sources(discord_cpp_self PRIVATE src/native_media.cpp)
    target_compile_definitions(discord_cpp_self PRIVATE DISCORD_SELF_NATIVE_MEDIA=1)
    add_library(discordSelf::native_media_deps INTERFACE IMPORTED)
    set_target_properties(discordSelf::native_media_deps PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${DISCORD_SELF_DAVE_INCLUDE_DIR};${DISCORD_SELF_OPUS_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${DISCORD_SELF_DAVE_LIBRARY};MLSPP::mlspp;OpenSSL::Crypto;${DISCORD_SELF_OPUS_LIBRARY}")
    target_link_libraries(discord_cpp_self PRIVATE discordSelf::native_media_deps)
    if(WIN32)
        target_link_libraries(discord_cpp_self PRIVATE winmm)
    endif()
endif()
if(MSVC)
    target_compile_options(discord_cpp_self PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(discord_cpp_self PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(DISCORD_SELF_BUILD_EXAMPLES)
    foreach(example IN ITEMS ping commands rest auto_reply)
        add_executable(discord_self_${example} examples/${example}.cpp)
        target_link_libraries(discord_self_${example} PRIVATE discord::self)
    endforeach()
endif()
if(DISCORD_SELF_BUILD_LIVE_EXAMPLES)
    foreach(example IN ITEMS check_connection account_tests media)
        add_executable(discord_self_${example} examples/${example}.cpp)
        target_link_libraries(discord_self_${example} PRIVATE discord::self)
    endforeach()
    if(MINGW)
        target_link_options(discord_self_media PRIVATE -municode)
    endif()
endif()

if(DISCORD_SELF_BUILD_TESTS)
    enable_testing()
    add_executable(discord_self_tests tests/unit.cpp)
    target_include_directories(discord_self_tests PRIVATE src)
    target_link_libraries(discord_self_tests PRIVATE discord::self ZLIB::ZLIB)
    add_test(NAME unit COMMAND discord_self_tests)
    if(DISCORD_SELF_NATIVE_MEDIA)
        find_program(DISCORD_SELF_FFMPEG ffmpeg REQUIRED)
        add_executable(discord_self_native_media_tests tests/native_media.cpp)
        target_include_directories(discord_self_native_media_tests PRIVATE src ${DISCORD_SELF_DAVE_INCLUDE_DIR} ${DISCORD_SELF_OPUS_INCLUDE_DIR})
        target_link_libraries(discord_self_native_media_tests PRIVATE discord::self ${DISCORD_SELF_DAVE_LIBRARY} MLSPP::mlspp OpenSSL::Crypto ${DISCORD_SELF_OPUS_LIBRARY})
        if(WIN32)
            target_link_libraries(discord_self_native_media_tests PRIVATE winmm ws2_32)
        endif()
        add_test(NAME native_media COMMAND discord_self_native_media_tests ${CMAKE_CURRENT_BINARY_DIR}/native-tests ${DISCORD_SELF_FFMPEG})
        set_tests_properties(native_media PROPERTIES TIMEOUT 90)
    endif()
    # Compile the documentation consumer without running any of its network calls.
    add_library(discord_self_hover OBJECT tests/hover.cpp)
    target_link_libraries(discord_self_hover PRIVATE discord::self)
    find_package(Python3 3.9 COMPONENTS Interpreter REQUIRED)
    if(Python3_FOUND)
        add_executable(discord_self_integration tests/integration.cpp)
        target_link_libraries(discord_self_integration PRIVATE discord::self)
        add_test(NAME loopback COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/loopback.py $<TARGET_FILE:discord_self_integration>)
        set_tests_properties(loopback PROPERTIES TIMEOUT 45)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
install(TARGETS discord_cpp_self EXPORT discordSelfTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT discordSelfTargets NAMESPACE discord:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/discordSelf)
configure_package_config_file(cmake/discordSelfConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/discordSelfConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/discordSelf)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/discordSelfConfigVersion.cmake
    COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/discordSelfConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/discordSelfConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/discordSelf)
install(FILES LICENSE COPYING NOTICE THIRD_PARTY.md
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/discord.cpp-self)
if(WIN32)
    set(DISCORD_SELF_PC_SYSTEM_LIBS "-lws2_32")
else()
    set(DISCORD_SELF_PC_SYSTEM_LIBS "-pthread")
endif()
if(DISCORD_SELF_NATIVE_MEDIA)
    string(APPEND DISCORD_SELF_PC_SYSTEM_LIBS " -ldave -lmlspp -lhpke -lbytes -ltls_syntax -lcrypto -lopus")
    if(WIN32)
        string(APPEND DISCORD_SELF_PC_SYSTEM_LIBS " -lwinmm")
    endif()
endif()
file(RELATIVE_PATH DISCORD_SELF_PC_PREFIX_RELATIVE
    "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
configure_file(cmake/discord-self.pc.in discord-self.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/discord-self.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
