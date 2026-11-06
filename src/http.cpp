// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include "internal.hpp"
#include <curl/curl.h>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>

namespace discord::detail {
namespace {
constexpr std::size_t response_limit = 16 * 1024 * 1024;
struct CurlDeleter { void operator()(CURL* curl) const { curl_easy_cleanup(curl); } };
struct HeaderDeleter { void operator()(curl_slist* list) const { curl_slist_free_all(list); } };
struct Capture { Response response; bool too_large = false; };
std::size_t write_body(char* data, std::size_t size, std::size_t count, void* userdata) noexcept {
    auto& capture = *static_cast<Capture*>(userdata);
    const auto bytes = size * count;
    if (bytes > response_limit - capture.response.body.size()) { capture.too_large = true; return 0; }
    try { capture.response.body.append(data, bytes); return bytes; } catch (...) { return 0; }
}
std::string trim(std::string text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}
std::size_t write_header(char* data, std::size_t size, std::size_t count, void* userdata) noexcept {
    const auto bytes = size * count;
    try {
        auto& response = static_cast<Capture*>(userdata)->response;
        std::string line(data, bytes);
        if (line.starts_with("HTTP/")) response.headers.clear();
        const auto colon = line.find(':');
        if (colon != std::string::npos) {
            auto key = line.substr(0, colon);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            response.headers[key] = trim(line.substr(colon + 1));
        }
        return bytes;
    } catch (...) { return 0; }
}
int progress(void* userdata, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<std::atomic_bool*>(userdata)->load() ? 1 : 0;
}
std::optional<double> seconds(std::string_view value) {
    try {
        std::size_t read = 0;
        double number = std::stod(std::string(value), &read);
        if (read == value.size() && std::isfinite(number) && number >= 0) return number;
    } catch (const std::exception&) {}
    return {};
}
std::chrono::milliseconds rate_delay(double value) {
    if (!std::isfinite(value) || value < 0) throw ProtocolError("Invalid Discord rate limit delay");
    // Bound conversion to a duration even for maliciously large finite values.
    return std::chrono::milliseconds{static_cast<std::int64_t>(std::ceil(std::min(value, 31'536'000.0) * 1000.0))};
}
std::string major_scope(std::string_view path) {
    std::istringstream input(std::string{path.substr(0, path.find('?'))});
    std::vector<std::string> parts;
    std::string part;
    while (std::getline(input, part, '/')) if (!part.empty()) parts.push_back(part);
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (parts[i] == "channels" || parts[i] == "guilds") return parts[i] + ":" + parts[i + 1];
        if (parts[i] == "webhooks") return "webhooks:" + parts[i + 1] + (i + 2 < parts.size() ? ":" + parts[i + 2] : "");
    }
    return {};
}
}

void init_curl() {
    static const auto initialized = [] {
        if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) throw TransportError("Could not initialize libcurl");
        return true;
    }();
    (void)initialized;
}
std::string encode_path(std::string_view value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : value) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
std::string route_key(const Request& request) {
    // Keep major resource IDs, normalize message/user IDs and query parameters.
    std::string path = request.path.substr(0, request.path.find('?'));
    std::istringstream input(path);
    std::string part, previous, result = request.method;
    while (std::getline(input, part, '/')) {
        if (part.empty()) continue;
        const bool numeric = std::all_of(part.begin(), part.end(), [](unsigned char c) { return std::isdigit(c); });
        result += "/" + (numeric && previous != "channels" && previous != "guilds" && previous != "webhooks" ? ":id" : part);
        previous = part;
    }
    return result;
}
void interruptible_wait(std::chrono::milliseconds delay, const std::atomic_bool& stop) {
    const auto end = Clock::now() + delay;
    while (!stop && Clock::now() < end)
        std::this_thread::sleep_for(std::min(std::chrono::milliseconds{10},
            std::chrono::duration_cast<std::chrono::milliseconds>(end - Clock::now())));
    if (stop) throw Cancelled("Client closed");
}
struct Rest::HttpSession {
    std::unique_ptr<CURL, CurlDeleter> handle{curl_easy_init()};
    HttpSession() {
        if (!handle) throw TransportError("Could not create HTTP session");
    }
};
Rest::Rest(ClientOptions options, Transport transport) : options_(std::move(options)), transport_(std::move(transport)) {
    init_curl();
}
Rest::~Rest() = default;
void Rest::login(std::string token) {
    if (token.empty() || std::any_of(token.begin(), token.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
        throw std::invalid_argument("Provide a nonempty raw user token without whitespace");
    std::lock_guard lock(mutex_);
    const bool same_account = token_ == token;
    token_ = std::move(token);
    cancelled_ = false;
    if (!same_account) {
        // Cookies and authenticated transport state belong to one credential.
        session_.reset();
        unauthorized_ = false;
        blocked_.clear();
        buckets_.clear();
        global_until_ = {};
    }
}
Response Rest::perform(const Request& request) {
    if (!session_) session_ = std::make_unique<HttpSession>();
    auto* curl = session_->handle.get();
    const auto url = options_.api_url + request.path;
    const auto body = request.body.is_null() ? std::string{} : request.body.dump();
    curl_slist* list = nullptr;
    for (const auto& header : {"Authorization: " + token_, std::string("Content-Type: application/json"), std::string("Accept: application/json")}) {
        auto* next = curl_slist_append(list, header.c_str());
        if (!next) { curl_slist_free_all(list); throw std::bad_alloc(); }
        list = next;
    }
    std::unique_ptr<curl_slist, HeaderDeleter> headers(list);
    Capture capture;
    // Reset borrowed request pointers before their owners die, including on
    // exceptions. libcurl retains connections, DNS/TLS caches and cookies.
    struct ResetOptions {
        CURL* handle;
        ~ResetOptions() { curl_easy_reset(handle); }
    } reset{curl};
    // curl_easy_reset clears profile settings after every request; reapply them
    // before request-specific headers/options while retaining the session caches.
    apply_browser_identity(curl, options_);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers.get());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
    // Empty filename enables an in-memory cookie engine; no browser cookie
    // import, cookie file, or automatic persistence is used.
    if (curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "") != CURLE_OK)
        throw TransportError("Could not enable HTTP session cookies");
    curl_easy_setopt(curl, CURLOPT_MAXCONNECTS, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(options_.request_timeout.count()));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(options_.request_timeout.count()));
    // Browser headers advertise compression, but a header alone does not enable
    // libcurl's response decoder. Re-enable it after every easy-handle reset;
    // the profile's explicit Accept-Encoding header retains its wire ordering.
    if (curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "") != CURLE_OK)
        throw TransportError("Could not enable HTTP response decompression");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &capture);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &capture);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelled_);
    if (!request.body.is_null()) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(body.size()));
    }
    const auto result = curl_easy_perform(curl);
    if (cancelled_) throw Cancelled("Client closed");
    if (capture.too_large) throw ProtocolError("HTTP response exceeds 16 MiB");
    if (result != CURLE_OK) throw TransportError(std::string("HTTP transport: ") + curl_easy_strerror(result));
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    capture.response.status = static_cast<int>(status);
    return std::move(capture.response);
}
Json Rest::request(std::string method, std::string path, Json body) {
    if (path.empty() || path[0] != '/' || path.starts_with("//") || path.find_first_of("#\\") != std::string::npos || path.find("..") != std::string::npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c <= 32 || c == 127; }))
        throw std::invalid_argument("API path must be relative, starting with one slash");
    if (method != "GET" && method != "POST" && method != "PATCH" && method != "PUT" && method != "DELETE")
        throw std::invalid_argument("Unsupported HTTP method");
    std::unique_lock lock(mutex_); // Serialized per client, including rate limit waits.
    if (token_.empty()) throw Error("Call login(token) or run(token) first");
    if (unauthorized_) throw HttpError(401, 40001, "Token was rejected; authenticate with a valid token before making further requests");
    const Request request{std::move(method), std::move(path), std::move(body)};
    const auto key = route_key(request);
    const auto major = major_scope(request.path);
    for (int attempt = 0;; ++attempt) {
        if (cancelled_) throw Cancelled("Client closed");
        auto now = Clock::now();
        for (auto it = blocked_.begin(); it != blocked_.end();) {
            if (it->second <= now) it = blocked_.erase(it); else ++it;
        }
        const auto bucket = buckets_.find(key);
        auto limit_key = bucket == buckets_.end() ? key : bucket->second;
        auto until = global_until_;
        if (const auto found = blocked_.find(limit_key); found != blocked_.end()) until = std::max(until, found->second);
        const auto wait = std::max(std::chrono::milliseconds{0}, std::chrono::duration_cast<std::chrono::milliseconds>(until - now));
        if (wait > options_.max_rate_limit_wait) throw Error("Discord rate limit exceeds max_rate_limit_wait");
        interruptible_wait(wait, cancelled_);
        const auto response = transport_ ? transport_(request) : perform(request);
        if (const auto hash = response.headers.find("x-ratelimit-bucket"); hash != response.headers.end()) {
            auto shared_key = "bucket:" + hash->second + ":" + major;
            if (const auto old = blocked_.find(limit_key); old != blocked_.end()) {
                blocked_[shared_key] = std::max(blocked_[shared_key], old->second);
            }
            if (buckets_.size() >= 4096) buckets_.clear();
            buckets_[key] = shared_key;
            limit_key = std::move(shared_key);
        }
        Json data = response.body.empty() ? Json(nullptr) : Json::parse(response.body, nullptr, false);
        const int code = data.is_object() && data.contains("code") && data["code"].is_number_integer() ? data["code"].get<int>() : 0;
        const auto message = data.is_object() && data.contains("message") && data["message"].is_string() ? data["message"].get<std::string>() : "Request failed";
        if (response.status == 401) unauthorized_ = true;
        if (response.status >= 400 && data.is_object() && data.contains("captcha_key"))
            throw VerificationRequired(response.status);
        // Account restrictions are not normal bucket exhaustion, even on HTTP 429.
        if (response.status >= 400 && (code == 40002 || code == 40003 || code == 40004 || code == 20026))
            throw HttpError(response.status, code, message);
        if (response.status == 429) {
            std::optional<double> retry;
            if (data.is_object() && data.contains("retry_after") && data["retry_after"].is_number())
                retry = data["retry_after"].get<double>();
            if (!retry) {
                if (const auto it = response.headers.find("retry-after"); it != response.headers.end()) retry = seconds(it->second);
            }
            if (!retry) throw HttpError(429, code, message + "; missing retry_after");
            const auto wait = rate_delay(*retry);
            const auto reset = Clock::now() + wait;
            const auto global_header = response.headers.find("x-ratelimit-global");
            const bool global = (data.is_object() && data.contains("global") && data["global"].is_boolean() && data["global"].get<bool>()) ||
                (global_header != response.headers.end() && global_header->second == "true");
            if (global) global_until_ = std::max(global_until_, reset);
            else blocked_[limit_key] = std::max(blocked_[limit_key], reset);
            // Retain the server's deadline even when this invocation gives up.
            if (attempt >= options_.max_rate_limit_retries) throw HttpError(429, code, message + "; rate limit retry budget exhausted");
            if (*retry * 1000.0 > static_cast<double>(options_.max_rate_limit_wait.count()))
                throw Error("Discord rate limit exceeds max_rate_limit_wait");
            continue;
        }
        if (response.status < 200 || response.status >= 300) {
            throw HttpError(response.status, code, message);
        }
        if (data.is_discarded()) throw ProtocolError("Discord returned invalid JSON");
        const auto remaining = response.headers.find("x-ratelimit-remaining");
        const auto reset = response.headers.find("x-ratelimit-reset-after");
        if (remaining != response.headers.end() && remaining->second == "0" && reset != response.headers.end()) {
            if (const auto value = seconds(reset->second)) {
                // A successful mutation must remain successful. An excessive wait
                // is rejected before the NEXT request, never after this one.
                const auto delay = rate_delay(*value);
                blocked_[limit_key] = std::max(blocked_[limit_key], Clock::now() + delay);
            }
        }
        return data;
    }
}
}
