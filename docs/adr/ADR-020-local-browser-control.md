# ADR-020: local browser capability and reliable editor channel

State: accepted for M03.C03 local implementation; remote/TLS/discovery and full
milestone qualification remain open.

The static Svelte editor must use ControlService's existing validated session,
ownership, revision, retry and subscription contract. The bare OSC channel is a
shared operator-granted local transport and does not provide browser identity or
acknowledged command retry. Add `/control` as a separate reliable WebSocket route.

Generate a per-server 256-bit capability with BCryptGenRandom on Windows,
getrandom on Linux and arc4random_buf on macOS. Fail startup if generation fails;
never use the domain ID generator as a credential. Bind only to loopback. Put the
launch capability in private local readiness/bootstrap output and a URL fragment;
never put it in HOST_INFO, static assets, command/model schemas or HTTP responses.
The UI stores it only for its browser session and removes the fragment from its
visible URL. The WebSocket offers `mapgyver.control.v1` and `token.<capability>` as
subprotocols; validate the exact fixed-length token before upgrading and select
only the public protocol name. Check all token bytes without early equality exit.

Require the bound endpoint Host. Browser control Origin must equal that endpoint
or one explicitly configured `http://127.0.0.1:<port>` development origin. Missing
Origin is permitted for capability-bearing native clients. Arbitrary/null origins
reject. Ordinary same-origin HTTP module/asset requests are allowed; cross-origin
HTTP requests and browser bare-OSC upgrades reject. This is local operator access,
not individual user authentication or an authenticated LAN/Internet service.

Every admitted control connection performs the existing strict hello, obtains a
connection-bound service session and passes original request bytes to ControlService.
Reply caching/sequence watermarks remain there. Disconnect releases editing access;
resume uses retained identity and a fresh full snapshot, then explicit subscription.
Transport cursors advance after complete writes, not assumed browser receipt.
Capacity, completion-owned buffers and native write deadlines remain shared with
the OSC transport; control hello has a five-second deadline.
Shutdown closes/cancels sockets and timers and drains actual completion handlers
on the owner for at most five seconds before releasing any member/peer buffers.
Failure to complete terminates only the owned engine while retaining those
buffers for OS cleanup; requesting cancellation alone never releases them.

The browser retains one original in-flight request for same-service replay, bounds
pending work to 32 entries / 4 MiB, snapshots payload bytes at enqueue, and uses
ten-second reply deadlines with bounded reconnect delay. It does not replay an
uncertain action into a new service or expired session. Browser reload resnapshots
without reconstructing an uncertain action. Authored/applied/saved states stay
separate and native unavailable outcomes remain null.

Preload static files at startup, with explicit count/byte limits and no symlinks;
serve exact preloaded paths with CSP/no-referrer/nosniff. No per-request disk read,
development Node server or browser callback enters native program output. Vite
runs separately in development. The current preview route returns explicit 503
until an actual native renderer/encoder supplies a frame; this does not satisfy
the encoded-preview gate. Native output independence still needs runtime evidence.

Validation uses actual independent native WebSocket clients and Chromium running
the packaged Svelte bundle: authorization/origins, UTF-8 asset paths, preloading,
lost ACK/replay, ownership/revisions/atomic failure, structural create/delete,
subscriptions/deadlines, actual UI edits/history/observer updates/reconnect/reload
and separate Vite full reload. The browser lost-ACK experiment deliberately drops
one accepted terminal reply through Playwright while connecting to the real engine.
Its initial injected close code 1011 was invalid for a browser client close; use
application code 4000, as required by the [WebSockets standard](https://websockets.spec.whatwg.org/#dom-websocket-close).
No test timeout/assertion was removed to accommodate that harness mistake.
