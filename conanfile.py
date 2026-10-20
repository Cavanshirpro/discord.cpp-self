# SPDX-License-Identifier: LGPL-3.0-or-later
# Copyright (C) 2026 Cavanshirpro
"""Conan 2 recipe. Build with conan create . --build=missing."""
from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.build import check_min_cppstd
from conan.tools.cmake import CMake, CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.files import copy
import os

required_conan_version = ">=2.21"


class DiscordSelfConan(ConanFile):
    name = "discord-cpp-self"
    version = "0.1.0"
    description = "A C++20 library for Discord user accounts"
    license = "LGPL-3.0-or-later"
    url = "https://github.com/Cavanshirpro/discord.cpp-self"
    homepage = url
    topics = ("discord", "cpp20", "websocket")
    package_type = "library"
    settings = "os", "arch", "compiler", "build_type"
    options = {"shared": [True, False], "fPIC": [True, False]}
    default_options = {
        "shared": False, "fPIC": True,
        "libcurl/*:with_websockets": True,
        "libcurl/*:with_ssl": "openssl",
        "libcurl/*:with_ca_fallback": True,
    }
    exports_sources = (
        "CMakeLists.txt", "cmake/*", "include/*", "src/*",
        "LICENSE", "COPYING", "NOTICE", "THIRD_PARTY.md",
    )

    def config_options(self):
        if self.settings.os == "Windows":
            self.options.rm_safe("fPIC")

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")

    def requirements(self):
        self.requires("libcurl/8.23.0")
        self.requires("nlohmann_json/3.12.0", transitive_headers=True)
        self.requires("zlib/1.3.1")

    def validate(self):
        if self.settings.compiler.get_safe("cppstd"):
            check_min_cppstd(self, "20")
        curl = self.dependencies["libcurl"].options
        if not curl.with_websockets or not curl.with_ssl:
            raise ConanInvalidConfiguration("libcurl must provide WebSockets and TLS")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        CMakeDeps(self).generate()
        tc = CMakeToolchain(self)
        tc.variables["DISCORD_SELF_BUILD_TESTS"] = False
        tc.variables["DISCORD_SELF_BUILD_EXAMPLES"] = False
        tc.variables["DISCORD_SELF_BUILD_LIVE_EXAMPLES"] = False
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        CMake(self).install()
        for name in ("LICENSE", "COPYING", "NOTICE", "THIRD_PARTY.md"):
            copy(self, name, self.source_folder, os.path.join(self.package_folder, "licenses"))

    def package_info(self):
        self.cpp_info.libs = ["discord_cpp_self"]
        self.cpp_info.set_property("cmake_file_name", "discordSelf")
        self.cpp_info.set_property("cmake_target_name", "discord::self")
        self.cpp_info.set_property("pkg_config_name", "discord-self")
        if self.settings.os == "Windows":
            self.cpp_info.system_libs = ["ws2_32"]
        elif self.settings.os in ("Linux", "FreeBSD"):
            self.cpp_info.system_libs = ["pthread"]
