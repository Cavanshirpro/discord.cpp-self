# Optional browser/TLS profiles

The development API adds `ClientOptions::browser_profile` and `ca_bundle`.
The default profile is empty and preserves ordinary libcurl behavior.

```cpp
discord::ClientOptions options;
options.browser_profile = "chrome152";
options.ca_bundle = "/absolute/path/to/trusted-roots.pem";
discord::Client client(options);
client.run(discord::token_from_env());
```

Build with `DISCORD_SELF_CURL_IMPERSONATE=ON` and a compatible
[curl-impersonate](https://github.com/lexiforest/curl-impersonate) library and
headers. `CURL::libcurl` must export `curl_easy_impersonate`; CMake verifies this
by compiling and linking a consumer. `discord::supports_browser_impersonation()`
reports whether this build enables the API. Stock curl builds reject a nonempty
profile before login or network access; unknown targets are also rejected.

Nova provides a pinned multi-platform SDK adapter. Standalone consumers can
provide a compatible CURL config package with `CURL_DIR`, or explicitly configure
FindCURL with `CURL_NO_CURL_CMAKE=ON`, `CURL_INCLUDE_DIR` and
`CURL_LIBRARY_RELEASE`. Static library consumers must also link the same provider.

Profile settings are applied to each REST request (including after an easy-handle
reset) and every Gateway connection/reconnect. Request-specific JSON/auth headers
override profile defaults. The Gateway uses HTTP/1.1 WebSocket Upgrade while REST
retains the profile's HTTP/2 settings. Other optional media transports are outside
this profile API's scope.

`ca_bundle` configures the trusted PEM bundle for both REST and Gateway. Peer and
hostname verification are explicitly enabled after applying a profile. No browser
cookie import, account/device identity forgery, CAPTCHA bypass or rate-limit
override is added. Profile behavior is provided by the selected transport library,
not a promise of complete browser equivalence.

Nova's loopback tests capture Chrome ClientHello and HTTP/2 settings, reject
untrusted/hostname-mismatched certificates, and exercise HTTPS/WSS with fake tokens.
