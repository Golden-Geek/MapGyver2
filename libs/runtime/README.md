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

This is a preparation library and native-window API path. Native monitor
enumeration, binding-to-window orchestration, render/control worker handoff,
production engine integration, hot topology recovery and patch mapping are still
pending. The adapter currently presents a selected exported composition node
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
