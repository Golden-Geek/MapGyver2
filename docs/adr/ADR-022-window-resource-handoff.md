# ADR-022: Stage window replacement on the native owner

State: accepted for M04 implementation; sustained deadline qualification is open.

Authored stage changes need to replace output windows without stopping the old
scene. D3D11 device texture allocation may run on the preparation thread, but
DXGI operations stay on the immediate-context/window-message owner. Microsoft
documents that threading requirement in its [D3D11 threading introduction](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro)
and the associated [DXGI best practices](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/dxgi-best-practices).
Creating swapchains concurrently on the preparation thread would violate it.

Prepare new held/presented textures and a completion query in the bounded
background candidate slot. Reuse windows whose ID, dimensions, visibility mode
and explicit desktop position agree. Stage one SDL creation, swapchain enqueue,
placeholder or texture import/framebuffer step per old-scene frame. Apply all
new graph routes and windows together. Show replacements after their first
submitted frame and hide retired windows. No browser callback participates.

A resized frozen output samples its previous held display texture into the new
held target. Preserve its held software-frame identity. An event query submitted
after that first frame covers the last old-window reads. Poll without waiting,
destroy owner handles in bounded steps, execute queued destruction before
destroying the SDL window, then return native/CPU ownership for background disposal.
Cancellation and shutdown own partially staged windows through the same fence.

The actual SDK handle limits and the existing 512 MiB concurrent offscreen texture
payload limit apply before allocation. That payload counts held/presented textures,
composition resources, sources and placeholders; it excludes DXGI swapchain and
driver-internal buffers and is not a measured VRAM counter. Counts remain arbitrary
within the explicit 64-window native and 256-output control capacities.

The new resize golden exposed a float UV half-texel tie: error 4/255 against the
unchanged tolerance 2/255. Authored offline shaders now use exact unsigned integer
pixel-centre mapping and texel loads, matching the portable reference. No runtime
shader compilation or tolerance widening is introduced.

SDL/DXGI/SRV/RTV calls still perform SDK work on the owner. Bounded staging steps
do not establish bounded driver latency or zero allocations. Qualification records
observed owner work time, with no sustained budget claim. Physical scanout,
hotplug/device-loss recovery, nontrivial mapping and M01 hardware gates remain open.
