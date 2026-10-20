// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Cavanshirpro
// Licensed under LGPL-3.0-or-later; see NOTICE, LICENSE and COPYING.
#include <discord/discord.hpp>

// This object is compiled, never executed. The markers drive real LSP hover queries.
using ClientType = discord::Client; // hover:Client|synchronous REST
using MessageType = discord::Message; // hover:Message|Message snapshot
using ChannelType = discord::Channel; // hover:Channel|Channel snapshot
using CommandsType = discord::Commands; // hover:Commands|prefix router
using ProfileType = discord::UserProfile; // hover:UserProfile|visible to the requesting account
using ErrorType = discord::VerificationRequired; // hover:VerificationRequired|verification/CAPTCHA

void inspect_public_documentation(discord::Client& client, discord::Message& message,
                                  discord::Channel& channel, discord::Commands& commands,
                                  discord::ClientOptions& options, discord::User& user,
                                  discord::MessageOptions& outgoing) {
    (void)client.fetch_me(); // hover:fetch_me|current account over REST
    (void)client.channel(42); // hover:channel|without fetching its metadata
    (void)message.reply("example"); // hover:reply|mention pings
    (void)message.edit("example"); // hover:edit|new snapshot
    (void)channel.history(10); // hover:history|no auto-pagination
    (void)commands.process(message); // hover:process|own-message command
    (void)options.request_timeout; // hover:request_timeout|must be positive
    (void)user.bio; // hover:bio|Biography
    (void)outgoing.mention_author; // hover:mention_author|notify its original author
    (void)discord::token_from_env(); // hover:token_from_env|without printing it
}
