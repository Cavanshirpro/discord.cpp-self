# Security policy

Version 0.1.x is the only maintained release line. This is an experimental library;
full API coverage, account safety and protection from platform restrictions are
not guarantees.

Report vulnerabilities privately using the repository's
[security advisory form](https://github.com/Cavanshirpro/discord.cpp-self/security/advisories/new).
Describe the affected version, impact and a minimal reproduction using fake data.
Do not place live tokens or account information in public issues. If private
reporting is unavailable, open an issue requesting a private contact without
including exploit details or secrets.

Treat user tokens as passwords. Keep `.env`, `test_token*`, live reports and local
build artifacts outside Git. Do not publish profile JSON blindly. Custom API and
Gateway endpoints receive credentials and must be trusted. TLS verification is
enabled by default; do not disable it to work around certificate failures.

Verification/CAPTCHA responses are surfaced to the caller and are not bypassed.
Stop a live operation when the account requires verification. Publishing source
or running the offline suite does not require Discord credentials.
