# MapGyver editor shell

The Svelte 5 / TypeScript / Vite editor uses the native engine's authored project,
registry, ownership, revisions and reliable commands. It currently displays the
library, arbitrary surfaces and actual triangulated geometry, inspector/name
editing, independent source placement, undo/redo, observers and reconnect state.
The geometry view is authored shape, not an encoded engine preview. Native output,
playback, durable saving, library import and the complete editor remain later work.

Use the project wrappers to build/check/package the editor. Foundation presets
enable `PM_BUILD_UI`; `OFF` explicitly builds control without the static bundle.
Node 24 (or a compatible Vite-supported Node) and npm are build/test tools only.

```powershell
python tools/ci/check.py --preset windows-local --configuration Debug --skip-bootstrap
out/build/windows-local/Debug/mapgyver-engine.exe --serve --pipe-name main --oscquery
```

The engine preloads `ui` beside its executable, or an explicit `--editor-dir`.
Open the private `editor.url` from its ready output. The capability fragment is
removed after bootstrap and retained only for that browser session. One editor
owns writes; other clients observe. A free owner can be claimed explicitly.
The public OSCQuery HOST_INFO does not carry this capability. The local capability
authorizes an operator's clients, not separate user accounts; no remote/TLS claim.

For development, keep the same native process running:

```powershell
out/build/windows-local/Debug/mapgyver-engine.exe --serve --pipe-name dev --oscquery --dev-origin http://127.0.0.1:5173
cd ui
npm ci --ignore-scripts
npm run dev
```

Open `http://127.0.0.1:5173/editor/#engine=<URL-encoded editor.websocket>&token=<editor.capability>`
using that process's private bootstrap values. Vite serves source/development
updates independently of the engine. The browser retains its engine endpoint
through reload. Production serves the compiled static bundle with no Node server.

`python tools/generate_types.py` creates `src/generated/model.ts` from the engine's
generated wire schema; `--check` detects drift. Exact 64-bit integer/rational
strings stay strings. These are model types; graph/range/access validation stays
in the engine. `control.ts` types and implements the versioned reliable envelope.

The client keeps one original in-flight request and a 32-entry / 4 MiB queue.
Payloads are captured at enqueue, reply timeout is ten seconds, reconnect delay
caps at five seconds. Same-service resume replays exact bytes; service restart or
session expiry does not replay an uncertain edit. Full snapshots and subscriptions
reconcile actual state. Applied/saved outcomes are shown separately, with explicit
unavailable reasons; no preview frame or durable acknowledgement is synthesized.

The project wrapper runs native socket contracts and Playwright on packaged assets.
Browser tests cover dropped ACK/exact replay through a fault-injecting proxy,
rename/history/observer updates/source placement/reconnect/reload, private bootstrap
and a separate Vite-requested full reload. This is not native rendered-frame/HMR/
shader/output qualification; those runtime gates remain open. Screenshots and JSON
results are retained under `out/reports/ui-<configuration>`.

Locked npm sources/tool licenses are in
[editor inventory](../docs/licenses/editor-dependencies.json). Runtime notices and
the application GPL license are copied with built assets. Playwright's Chromium/
FFmpeg downloads are development-only and are not installed with the application.
M13 still owns the distributed binary/bundle SBOM and matching source materials.
