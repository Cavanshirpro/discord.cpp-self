// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#pragma once
#include <stdexcept>
#include <string>

namespace discord {
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class TransportError : public Error { using Error::Error; };
class ProtocolError : public Error { using Error::Error; };
class Cancelled : public Error { using Error::Error; };
class HttpError : public Error {
public:
    HttpError(int status, int code, std::string message)
        : Error("Discord HTTP " + std::to_string(status) + " (" + std::to_string(code) + "): " + message),
          status(status), code(code) {}
    int status;
    int code;
};
class GatewayError : public Error {
public:
    explicit GatewayError(int code)
        : Error("Gateway closed with code " + std::to_string(code)), code(code) {}
    int code;
};
class VerificationRequired : public HttpError {
public:
    explicit VerificationRequired(int status) : HttpError(status, 0, "Discord requires account verification or CAPTCHA") {}
};
}
