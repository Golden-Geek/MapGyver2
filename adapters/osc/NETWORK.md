# Native OSCQuery transport

Run `mapgyver-engine --serve --pipe-name <stable-id> --oscquery` on Windows.
The ready JSON reports actual ephemeral HTTP/WebSocket and UDP ports, bound only
to `127.0.0.1`, and the external OSC service session ID. Omitting `--oscquery`
opens no network sockets. Sockets and Boost types stay in this adapter; polling
runs on the existing control owner, with at most 64 asynchronous completions per
iteration. It does not run on a render/audio deadline thread.

HTTP GET serves the immutable registry-backed OSCQuery tree and attributes;
`?HOST_INFO` ignores the path and reports the actual OSC/WS endpoints. HTTP
responses close their connection and are capped at 4 MiB. The root WebSocket
accepts proposal LISTEN/IGNORE text commands and standard binary OSC packets.
Subscriptions are exact, bounded to 1,024 paths and start empty on reconnect.
Changed subscribed values use binary OSC; structural changes invalidate `/`
with PATH_CHANGED. Only implemented extensions are advertised. Clients must
finish the WebSocket opening handshake before sending frames.

Network OSC starts read-only. An editor connected through the current-user
named pipe must transfer editing ownership, using the normal generation-checked
`transferEdit` operation, to the reported `oscSessionId`. This grants the explicit
external OSC channel to **all local UDP and originless WebSocket OSC peers**;
it is not individual client authentication. It never displaces an editor by
itself. Use the named-pipe reliable envelope for acknowledged retries. Bare OSC
does not acquire deduplication or reliable delivery; structural operations carry
their explicit authored revision. All writes use the same domain validator.

HTTP Host must match the bound loopback endpoint (`127.0.0.1` or `localhost`
with its port). Requests with Origin are rejected. This adapter has no browser
editing channel, CORS authorization, TLS, authenticated remote endpoint or Bonjour
advertisement. Those capabilities remain open rather than being inferred from a
local socket test. The upcoming editor channel needs its own concrete access
policy and reliable sessions. Unknown configured source adapters remain visible.

Capacity and liveness policy:

- 16 TCP peers; additional accepted sockets close without allocation of a peer.
- 16 KiB HTTP headers, 5 s whole HTTP request/upgrade deadline, 2 s HTTP write.
- 1 MiB complete WebSocket message, including assembled fragments; larger
  messages are refused by Beast with message-too-big. Idle connections use 30 s
  liveness checks and protocol pings; clients must respond.
- Per WebSocket, 256 queued messages / 4 MiB, with a 2 s whole-message write
  deadline. Capacity or deadline failure closes that peer. Outstanding handlers
  retain exact buffer ownership through cancellation/completion; no buffer reuse
  based only on requesting cancellation.
- A valid model value can exceed OSC's 256 KiB per-string encoding capacity.
  That subscriber closes with a notification-rejection counter; the accepted
  model edit and healthy clients continue, and HTTP can still query the value.
- One 64 KiB UDP receive buffer; malformed, non-immediate, inaccessible or invalid
  packets reject completely. UDP provides no retransmission or reply guarantee.
- HOST_INFO's X_MAPGYVER endpoint metadata includes actual accept/reject and
  deadline counters. These are software control/IO observations, never GPU,
  physical output or delivery counters.

Boost.Beast/Asio 1.92.0 comes from the pinned vcpkg baseline; the
[resolved source/notice inventory](../../docs/licenses/network-dependencies.json)
includes transitive packages and source archive hashes. The implementation uses
asynchronous callbacks. Default transitive Asio features also install context and
date-time; this does not claim those libraries are linked or bundled. M13 still
needs a binary inventory and distributable notices/matching source.

`tests/protocol/test_network.py` uses independent Python stdlib HTTP, UDP and
masked RFC6455 frames against real sockets. `protocol.engine-network` tests the
production Windows engine and actual pipe ownership grant. `protocol.network`
uses a portable native socket fixture with an explicitly test-only initial grant;
its parent closes stdin and verifies a normal exit/destruction. Neither evidence
class qualifies remote access, discovery, rendered application or durable save.
