# Bundled TLS verification correction

The engine already tracks the bundled yasio 4.4.1 sources. The patch in this
directory is applied directly to tracked `yasio/impl/openssl.hpp`; ordinary
CMake compilation of `yasio/io_service.cpp` includes that corrected header.
Fresh checkouts do not depend on an ignored download or build cache, and must
not apply the patch a second time. The source's existing MIT notice is retained.

`tls-verification-manifest.json` records the original header, corrected header,
patch digests and the engine commit that last imported the bundled source.
It does not claim that that engine commit is an upstream yasio release commit.
The changes keep peer verification active when an explicitly requested CA
cannot load, and install IP-SAN/DNS identity verification for verified clients.
SNI remains certificate selection and is not treated as identity validation.

The native WebSocket adapter separately reads requested CA bytes through
FileUtils, materializes them in an application-owned content-addressed cache,
and configures the CA before starting yasio's worker. The worker creates its
client SSL context at startup, so starting it before that configuration was a
real race. Pixel 7a native baseline/candidate, matching-identity echo, negative
trust cases and HTTP/WS regression evidence is recorded in
`tests/js-runtime-tests/evidence/2026-10-13-wss-trust-validation.json`.
