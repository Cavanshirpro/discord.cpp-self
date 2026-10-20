// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <stdexcept>
#include <string>

namespace discord {
/// @brief Base exception for library failures; derived from std::runtime_error.
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
/// @brief A connection, timeout or libcurl transport failure.
class TransportError : public Error { using Error::Error; };
/// @brief Malformed, unsupported or inconsistent REST/Gateway protocol data.
class ProtocolError : public Error { using Error::Error; };
/// @brief An operation interrupted by Client::close().
class Cancelled : public Error { using Error::Error; };
/// @brief A failed Discord HTTP response, retaining HTTP status and API error code.
class HttpError : public Error {
public:
    /// @brief Construct an HTTP failure with its status, Discord code and explanation.
    HttpError(int status, int code, std::string message)
        : Error("Discord HTTP " + std::to_string(status) + " (" + std::to_string(code) + "): " + message),
          status(status), code(code) {}
    int status; ///< HTTP status, such as 401, 403 or 429.
    int code; ///< Discord JSON error code, or zero when unavailable.
};
/// @brief A Gateway WebSocket close, retaining the numeric close code.
class GatewayError : public Error {
public:
    /// @brief Construct a Gateway-close failure.
    explicit GatewayError(int code)
        : Error("Gateway closed with code " + std::to_string(code)), code(code) {}
    int code; ///< WebSocket/Gateway close code, such as 4004 for failed authentication.
};
/// @brief Discord requires account verification/CAPTCHA. No automatic challenge retry is performed.
class VerificationRequired : public HttpError {
public:
    /// @brief Retain the challenge's HTTP status without exposing challenge payloads.
    explicit VerificationRequired(int status) : HttpError(status, 0, "Discord requires account verification or CAPTCHA") {}
};
}
