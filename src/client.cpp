// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
#include "internal.hpp"
#include <condition_variable>
#include <cstdlib>
#include <limits>

namespace discord::detail {
void MessageCache::insert(Json data) {
    if (!capacity_) return;
    const auto id = data.at("id").get<Snowflake>().value();
    if (!values_.contains(id)) {
        while (values_.size() >= capacity_) { values_.erase(order_.front()); order_.pop_front(); }
        order_.push_back(id);
    }
    values_[id] = std::move(data);
}
std::optional<Json> MessageCache::get(Snowflake id) const {
    if (const auto found = values_.find(id.value()); found != values_.end()) return found->second;
    return {};
}
void MessageCache::erase(Snowflake id) {
    values_.erase(id.value());
    std::erase(order_, id.value());
}
void MessageCache::clear() { values_.clear(); order_.clear(); }
}

namespace discord {
struct Client::Impl {
    ClientOptions options;
    std::shared_ptr<detail::Rest> rest;
    std::atomic_bool running{false}, stop{false};
    std::atomic<std::int64_t> latency{-1};
    mutable std::mutex state_mutex;
    std::optional<User> current_user;
    Json current_user_data;
    detail::MessageCache cache;
    std::mutex queue_mutex;
    std::condition_variable changed;
    std::deque<RawEvent> events;
    bool done = false;
    std::exception_ptr failure;
    std::vector<std::function<void(const Ready&)>> ready_handlers;
    std::vector<std::function<void(const Message&)>> message_handlers;
    std::vector<std::function<void(const MessageUpdate&)>> edit_handlers;
    std::vector<std::function<void(const MessageDelete&)>> delete_handlers;
    std::vector<std::function<void(const RawEvent&)>> raw_handlers;
    std::vector<std::function<void()>> resumed_handlers;
    std::function<void(const std::exception&)> error_handler;

    explicit Impl(ClientOptions value) : options(std::move(value)), cache(options.message_cache_size) {
        if (options.event_queue_size == 0 || options.request_timeout.count() <= 0 ||
            options.request_timeout.count() > std::numeric_limits<long>::max() || options.max_rate_limit_wait.count() < 0 ||
            options.max_reconnect_attempts < 0 || options.max_rate_limit_retries < 0)
            throw std::invalid_argument("Invalid ClientOptions limits");
        while (options.api_url.ends_with('/')) options.api_url.pop_back();
        if ((!options.api_url.starts_with("https://") && !options.api_url.starts_with("http://")) ||
            options.api_url.find_first_of("\r\n?#") != std::string::npos)
            throw std::invalid_argument("Invalid API base URL");
        // Normalize before constructing the transport used for requests.
        rest = std::make_shared<detail::Rest>(options);
    }
    void configurable() const {
        if (running) throw Error("Configure handlers before run()");
    }
    Message model(const Json& data) const { auto message = data.get<Message>(); message.bind(rest); return message; }
    template<class List, class Value> void emit(const List& handlers, const Value& value) {
        for (const auto& handler : handlers) invoke([&] { handler(value); });
    }
    template<class Function> void invoke(Function&& function) {
        try { function(); }
        catch (const std::exception& error) {
            if (error_handler) error_handler(error); else throw;
        }
    }
    void dispatch(const RawEvent& event) {
        const auto& data = event.data;
        if (event.name == "READY") {
            Ready ready{data.at("user").get<User>(), data.value("guilds", std::vector<Guild>{})};
            {
                std::lock_guard lock(state_mutex);
                current_user = ready.user;
                current_user_data = data.at("user");
                cache.clear();
            }
            emit(ready_handlers, ready);
        } else if (event.name == "RESUMED") {
            for (const auto& handler : resumed_handlers) invoke(handler);
        } else if (event.name == "USER_UPDATE") {
            std::lock_guard lock(state_mutex);
            if (current_user && data.at("id").get<Snowflake>() == current_user->id) {
                current_user_data.update(data);
                current_user = current_user_data.get<User>();
            }
        } else if (event.name == "MESSAGE_CREATE") {
            const auto message = model(data);
            { std::lock_guard lock(state_mutex); cache.insert(data); }
            emit(message_handlers, message);
        } else if (event.name == "MESSAGE_UPDATE") {
            MessageUpdate update{data.at("id").get<Snowflake>(), data.at("channel_id").get<Snowflake>(), {}, {}, data};
            {
                std::lock_guard lock(state_mutex);
                if (auto previous = cache.get(update.id)) {
                    update.before = model(*previous);
                    previous->update(data); // Shallow patch: missing fields are retained; explicit nulls are applied.
                    update.after = model(*previous);
                    cache.insert(std::move(*previous));
                }
            }
            emit(edit_handlers, update);
        } else if (event.name == "MESSAGE_DELETE" || event.name == "MESSAGE_DELETE_BULK") {
            const auto channel = data.at("channel_id").get<Snowflake>();
            const auto ids = event.name == "MESSAGE_DELETE_BULK" ? data.at("ids").get<std::vector<Snowflake>>() :
                std::vector<Snowflake>{data.at("id").get<Snowflake>()};
            for (const auto id : ids) {
                MessageDelete removed{id, channel, {}};
                {
                    std::lock_guard lock(state_mutex);
                    if (const auto old = cache.get(id)) removed.cached = model(*old);
                    cache.erase(id);
                }
                emit(delete_handlers, removed);
            }
        }
        emit(raw_handlers, event);
    }
};

Client::Client(ClientOptions options) : impl_(std::make_unique<Impl>(std::move(options))) {}
Client::~Client() { close(); }
#define DISCORD_HANDLER(method, field, type) \
Client& Client::method(std::function<void(const type&)> handler) { \
    impl_->configurable(); if (!handler) throw std::invalid_argument("Empty event handler"); \
    impl_->field.push_back(std::move(handler)); return *this; }
DISCORD_HANDLER(on_ready, ready_handlers, Ready)
DISCORD_HANDLER(on_message, message_handlers, Message)
DISCORD_HANDLER(on_message_edit, edit_handlers, MessageUpdate)
DISCORD_HANDLER(on_message_delete, delete_handlers, MessageDelete)
DISCORD_HANDLER(on_raw_event, raw_handlers, RawEvent)
#undef DISCORD_HANDLER
Client& Client::on_resumed(std::function<void()> handler) {
    impl_->configurable();
    if (!handler) throw std::invalid_argument("Empty event handler");
    impl_->resumed_handlers.push_back(std::move(handler));
    return *this;
}
Client& Client::on_error(std::function<void(const std::exception&)> handler) {
    impl_->configurable(); impl_->error_handler = std::move(handler); return *this;
}
void Client::login(std::string token) {
    impl_->configurable();
    impl_->rest->login(std::move(token));
    std::lock_guard lock(impl_->state_mutex);
    impl_->current_user.reset();
    impl_->current_user_data = nullptr;
    impl_->cache.clear();
}
void Client::run(std::string token) {
    if (impl_->running.exchange(true)) throw Error("Client is already running");
    struct Reset { std::atomic_bool& value; ~Reset() { value = false; } } reset{impl_->running};
    impl_->rest->login(token);
    impl_->stop = false;
    impl_->latency = -1;
    {
        std::lock_guard lock(impl_->state_mutex);
        impl_->current_user.reset();
        impl_->current_user_data = nullptr;
        impl_->cache.clear();
    }
    {
        std::lock_guard lock(impl_->queue_mutex);
        impl_->events.clear(); impl_->failure = nullptr; impl_->done = false;
    }
    std::jthread network([&, token = std::move(token)] {
        try {
            detail::Gateway gateway(impl_->options, token, impl_->stop, impl_->latency, [&](RawEvent event) {
                std::lock_guard lock(impl_->queue_mutex);
                if (impl_->events.size() >= impl_->options.event_queue_size)
                    throw Error("Event queue is full; shorten callbacks or increase event_queue_size");
                impl_->events.push_back(std::move(event));
                impl_->changed.notify_one();
            });
            gateway.run();
        } catch (...) { std::lock_guard lock(impl_->queue_mutex); impl_->failure = std::current_exception(); }
        { std::lock_guard lock(impl_->queue_mutex); impl_->done = true; }
        impl_->changed.notify_one();
    });
    try {
        while (!impl_->stop) {
            std::unique_lock lock(impl_->queue_mutex);
            impl_->changed.wait(lock, [&] { return impl_->stop || impl_->done || !impl_->events.empty(); });
            if (impl_->stop) break;
            if (impl_->events.empty()) {
                if (impl_->failure) std::rethrow_exception(impl_->failure);
                break;
            }
            auto event = std::move(impl_->events.front());
            impl_->events.pop_front();
            lock.unlock();
            impl_->dispatch(event);
        }
    } catch (...) { close(); throw; }
    close(); // Network thread is joined before run() returns, including exceptions.
}
void Client::close() noexcept {
    // Use the waiter's mutex so notification cannot be lost between its
    // predicate check and condition_variable::wait().
    { std::lock_guard lock(impl_->queue_mutex); impl_->stop = true; }
    impl_->rest->cancel();
    impl_->changed.notify_all();
}
bool Client::is_running() const noexcept { return impl_->running; }
std::chrono::milliseconds Client::latency() const noexcept { return std::chrono::milliseconds{impl_->latency.load()}; }
std::optional<User> Client::user() const { std::lock_guard lock(impl_->state_mutex); return impl_->current_user; }
std::optional<Message> Client::cached_message(Snowflake id) const {
    std::lock_guard lock(impl_->state_mutex);
    const auto data = impl_->cache.get(id);
    return data ? std::optional<Message>{impl_->model(*data)} : std::nullopt;
}
Channel Client::channel(Snowflake id) const { Channel channel; channel.id = id; channel.bind(impl_->rest); return channel; }
Channel Client::fetch_channel(Snowflake id) const {
    auto value = request("GET", "/channels/" + id.str()).get<Channel>(); value.bind(impl_->rest); return value;
}
User Client::fetch_user(Snowflake id) const {
    try { return request("GET", "/users/" + id.str()).get<User>(); }
    catch (const HttpError& error) {
        // Some normal-account sessions cannot use the bare user route even
        // though the same user is visible through the supported profile route.
        if (error.status != 403 || error.code != 40001) throw;
        return fetch_profile(id).user;
    }
}
User Client::fetch_me() const { return request("GET", "/users/@me").get<User>(); }
std::vector<Guild> Client::fetch_guilds() const {
    // The user API returns the guild list without bot-style pagination.
    return request("GET", "/users/@me/guilds?with_counts=true").get<std::vector<Guild>>();
}
Guild Client::fetch_guild(Snowflake id) const {
    return request("GET", "/guilds/" + id.str() + "?with_counts=true").get<Guild>();
}
Member Client::fetch_member(Snowflake guild_id, Snowflake user_id) const {
    return request("GET", "/guilds/" + guild_id.str() + "/members/" + user_id.str()).get<Member>();
}
UserProfile Client::fetch_profile(Snowflake user_id, std::optional<Snowflake> guild_id) const {
    auto path = "/users/" + user_id.str() + "/profile?with_mutual_guilds=true&with_mutual_friends=false&with_mutual_friends_count=true";
    if (guild_id) path += "&guild_id=" + guild_id->str();
    return request("GET", path).get<UserProfile>();
}
std::vector<Relationship> Client::fetch_relationships() const {
    return request("GET", "/users/@me/relationships").get<std::vector<Relationship>>();
}
void Client::send_friend_request(Snowflake user_id) const {
    (void)request("PUT", "/users/@me/relationships/" + user_id.str(), Json::object());
}
std::vector<Channel> Client::fetch_channels(Snowflake guild_id) const {
    auto channels = request("GET", "/guilds/" + guild_id.str() + "/channels").get<std::vector<Channel>>();
    for (auto& channel : channels) channel.bind(impl_->rest);
    return channels;
}
Channel Client::create_dm(Snowflake recipient) const {
    auto value = request("POST", "/users/@me/channels", {{"recipients", Json::array({recipient})}}).get<Channel>(); value.bind(impl_->rest); return value;
}
Message Client::send(Snowflake id, std::string content) const { return channel(id).send(std::move(content)); }
Message Client::send(Snowflake id, const MessageOptions& options) const { return channel(id).send(options); }
Json Client::request(std::string method, std::string path, Json body) const {
    return impl_->rest->request(std::move(method), std::move(path), std::move(body));
}
std::string token_from_env(const char* name) {
    if (!name || !*name) throw std::invalid_argument("Environment variable name is empty");
    const auto* value = std::getenv(name);
    if (!value || !*value) throw Error(std::string("Set the ") + name + " environment variable first");
    return value;
}
}
