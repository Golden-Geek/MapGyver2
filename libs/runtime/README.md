# Logical output preparation

`bind_outputs()` compares immutable authored targets against a supplied native
display/capability inventory. It matches an explicit stable endpoint and exact
pixel/rational refresh mode. Enumeration order, display names and an empty
binding never select a monitor. Missing, ambiguous, disabled, unsupported and
oversized targets remain in the result with their desired settings and reasons.
An empty reason means eligible for preparation; it does not mean a swapchain or
physical presentation already succeeded. Inventory/target capacities are bounded.

The registry owns authored `enabled`, `blackout` and `freeze` controls. They use
the ordinary validated batch/history/wire path and leave calibration/content
ownership intact. Runtime frames/status must come from the native worker;
authored intent alone does not set applied revision. The prerelease generated
wire schema/types include these required boolean fields. Durable older-project
migration remains M12 work; no durable project format is implemented yet.

The provisional Windows compositor also prepares bounded native window outputs
with independent SDL windows/bgfx swapchains, held frame and presented textures.
Freeze retains a per-output complete GPU frame; initial freeze shows opaque
black. Blackout overrides presentation while preserving that held frame.
Disable blacks/hides a visible window without deleting its configured identity.
Unfreezing updates it on the next native frame. Other outputs and the monotonic
software clock continue. Display encoding composites canonical linear content
over black, then encodes opaque sRGB. A shared software frame never proves
synchronized physical scanout.

The Windows engine can now run the prepared scene on a native render owner with
`--render`; its monotonic show clock runs independently of UI/control requests.
Output controls and metadata changes use bounded value batches consumed between
frames. Actual frame observations update reliable control replies/snapshots;
they never imply physical scanout or durable saving. Fixed-layout source image
changes now prepare on a separate device worker, then apply before a native frame.
Graph/window/mapping changes keep the last applied scene and report
`render_state_requires_preparation`; those resource changes remain required work.

Windows native monitor
inventory now queries active device paths, source desktop rectangles and rational
mode rates, with three bounded retries for a topology race. Cloned sources retain
their descriptors with an independent-output constraint; empty/ambiguous bindings
never fall back to a primary display. These are OS-reported modes, not measured
scanout. Exact startup bindings now create windowed or explicitly positioned
borderless targets. Hot topology/resource recovery and nontrivial patch mapping
remain pending. The adapter currently presents a selected exported composition node
over a whole window; callers must not treat it as implemented physical calibration.
Only already-exported roots are allowed, preserving target lifetimes.

Manual native GPU/window validation (outside CTest/hosted hardware claims):

```powershell
python tools/qualification/check_outputs.py --probe out/build/windows-render/adapters/windows/render/Debug/mapgyver-output-probe.exe --report out/reports/output-Debug.json
```

Use the Release executable/report for the Release run. The fixture owns three
hidden windows and validates the exact intermediate textures submitted to them
at three distinct sizes. Its offline readback is outside frame execution. This
is API/real-GPU evidence, not visible projector/physical scanout qualification.

The inventory follows the Windows
[QueryDisplayConfig contract](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig).
It preserves native error codes, bounds path/mode allocation and converts bounded
UTF-16 identity fields explicitly. It does not change display modes or substitute
monitor names/enumeration indices for a requested device path. M12 owns hotplug,
EDID/connector confidence and recovery when those desired paths change.

Native engine qualification:

```powershell
python tools/qualification/check_native_program.py --engine out/build/windows-render/Debug/mapgyver-engine.exe --probe out/build/windows-render/adapters/windows/render/Debug/mapgyver-output-probe.exe --report out/reports/native-program-Debug.json
```

Use Release paths for Release. The fixture exports a validated logical project
with a default composition, one static gradient source and three output windows.
`--initial-state` reads that bounded versioned control document; it is not SQLite
persistence or a durable save implementation. `--render-hidden`, `--native-frames`
and `--native-report` support explicit bounded qualification. Final synchronous
captures/JSON/file IO occur after the bounded program loop ends. Normal output
runs continuously without those options. Built-in `mapgyver.pattern` locators
`bars`, `checker`, `alpha` and `gradient` prepare immutable 128x64 source images;
arbitrary composition/output dimensions remain editable. Other sources retain
unavailable reasons until their adapters are implemented.

The explicit native test uses actual Svelte controls, named-pipe commands and an
operator-granted OSC bundle through the same validator; reloads and terminates
its owned Chromium process; then disconnects all control clients while native
frames continue. An anonymous Windows job contains only that helper/browser tree
for failure cleanup. Offline final textures check freeze/blackout/disable against
fixed color tolerance. This remains stateless-pattern/hidden-window evidence;
animated media, graph/window resources, physical scanout and encoded preview are
unqualified. Immediate-context/bgfx calls stay on their native owner. Shutdown
stops/joins preparation before bgfx teardown, drains unretired copy ownership and
waits for actual owner completion with a five-second fatal boundary.

Append `--hot-images` to also exercise pipe source bursts, an atomic source/output
batch, unavailable-source retention/undo, and an operator-granted OSC source/output
bundle. The final alpha-pattern GPU capture proves changed RGB/alpha content is
composited over black, while the other outputs exercise blackout/disable. Browser
reload/process death and the two-second no-client interval remain in that run.

Preparation has one working/ready/copying/retired candidate slot and one latest
pending immutable project request. Newer scene generations supersede older jobs;
completed/discarded native sources and CPU plans are disposed off the render owner.
The next job starts after retirement disposal. A candidate is capped at 256 images/
128 MiB and active plus candidate texture payload at 512 MiB. CPU source generation
is also bounded before native allocation. Output flags/metadata carry the desired
scene generation and cannot claim a new applied revision on an older scene.
The native owner tries each mailbox lock once and continues frames if it is busy.
Reports distinguish actual supplied CPU upload bytes (including discarded work),
explicit GPU-copy submissions/payload bytes and offline validation transfers.
These are application/API counts, not measured VRAM/bus/hardware counters.
