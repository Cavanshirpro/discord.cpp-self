# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
cmake_minimum_required(VERSION 3.25)
project(discord_cpp_self VERSION 0.1.0
    DESCRIPTION "A C++20 library for Discord user accounts"
    HOMEPAGE_URL "https://github.com/Cavanshirpro/discord.cpp-self" LANGUAGES CXX)

option(DISCORD_SELF_BUILD_EXAMPLES "Build examples" ${PROJECT_IS_TOP_LEVEL})
option(DISCORD_SELF_BUILD_TESTS "Build offline tests" ${PROJECT_IS_TOP_LEVEL})
option(DISCORD_SELF_BUILD_LIVE_EXAMPLES "Build manual examples that use real accounts" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

find_package(CURL 7.86 REQUIRED)
find_package(nlohmann_json 3.11 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(discord_cpp_self
    src/models.cpp src/http.cpp src/gateway.cpp src/client.cpp src/commands.cpp)
add_library(discord::self ALIAS discord_cpp_self)
set_target_properties(discord_cpp_self PROPERTIES EXPORT_NAME self WINDOWS_EXPORT_ALL_SYMBOLS ON
    VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})
target_compile_features(discord_cpp_self PUBLIC cxx_std_20)
target_include_directories(discord_cpp_self PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(discord_cpp_self PUBLIC nlohmann_json::nlohmann_json
    PRIVATE CURL::libcurl ZLIB::ZLIB Threads::Threads)
if(WIN32)
    target_link_libraries(discord_cpp_self PRIVATE ws2_32)
endif()
if(MSVC)
    target_compile_options(discord_cpp_self PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(discord_cpp_self PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(DISCORD_SELF_BUILD_EXAMPLES)
    foreach(example IN ITEMS ping commands rest)
        add_executable(discord_self_${example} examples/${example}.cpp)
        target_link_libraries(discord_self_${example} PRIVATE discord::self)
    endforeach()
endif()
if(DISCORD_SELF_BUILD_LIVE_EXAMPLES)
    foreach(example IN ITEMS check_connection account_tests)
        add_executable(discord_self_${example} examples/${example}.cpp)
        target_link_libraries(discord_self_${example} PRIVATE discord::self)
    endforeach()
endif()

if(DISCORD_SELF_BUILD_TESTS)
    enable_testing()
    add_executable(discord_self_tests tests/unit.cpp)
    target_include_directories(discord_self_tests PRIVATE src)
    target_link_libraries(discord_self_tests PRIVATE discord::self ZLIB::ZLIB)
    add_test(NAME unit COMMAND discord_self_tests)
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
file(RELATIVE_PATH DISCORD_SELF_PC_PREFIX_RELATIVE
    "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig" "${CMAKE_INSTALL_PREFIX}")
configure_file(cmake/discord-self.pc.in discord-self.pc @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/discord-self.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
