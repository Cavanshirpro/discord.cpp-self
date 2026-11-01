// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include "internal.hpp"
#include <curl/curl.h>
#include <algorithm>

namespace discord {
bool supports_browser_impersonation() noexcept {
#ifdef DISCORD_SELF_CURL_IMPERSONATE
    return true;
#else
    return false;
#endif
}
}
namespace discord::detail {
void apply_browser_identity(void* native_handle, const ClientOptions& options) {
    auto* handle = static_cast<CURL*>(native_handle);
    if (!options.browser_profile.empty()) {
#ifdef DISCORD_SELF_CURL_IMPERSONATE
        if (curl_easy_impersonate(handle, options.browser_profile.c_str(), 1) != CURLE_OK)
            throw std::invalid_argument("Unsupported browser profile: " + options.browser_profile);
#else
        throw std::invalid_argument("Browser profiles require a curl-impersonate-enabled build");
#endif
    } else {
        if (curl_easy_setopt(handle, CURLOPT_USERAGENT, "discord.cpp-self/" DISCORD_SELF_VERSION) != CURLE_OK)
            throw TransportError("Could not configure HTTP user agent");
    }
    // Profile selection must never switch off peer/hostname authentication.
    if (curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK ||
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK)
        throw TransportError("Could not configure TLS certificate verification");
    if (!options.ca_bundle.empty() && curl_easy_setopt(handle, CURLOPT_CAINFO, options.ca_bundle.c_str()) != CURLE_OK)
        throw TransportError("Could not configure TLS trust bundle");
}
void validate_browser_identity(const ClientOptions& options) {
    if (options.ca_bundle.find('\0') != std::string::npos)
        throw std::invalid_argument("TLS trust bundle path contains NUL");
    if (options.browser_profile.empty()) return;
    if (options.browser_profile.size() > 64 || !std::all_of(options.browser_profile.begin(), options.browser_profile.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        })) throw std::invalid_argument("Browser profile must be a simple target name");
    init_curl();
    auto* handle = curl_easy_init();
    if (!handle) throw TransportError("Could not create browser profile validation handle");
    try { apply_browser_identity(handle, options); }
    catch (...) { curl_easy_cleanup(handle); throw; }
    curl_easy_cleanup(handle);
}
}
