# Primary sources and verification baseline

Baseline checked 2026-10-15. The product model, milestone boundaries and provisional performance budgets are project design decisions. These links support external API, platform and tooling constraints; they do not certify this application's implementation.

| ID | Source | Application |
| --- | --- | --- |
| S01 | [MapGyver](https://github.com/Golden-Geek/MapGyver) | Reference implementation and workflows. Reviewed tree revision `5d138d55948c994abeb6909c0b670f28f8c54bd5`; inspect current target code before reuse. |
| S02 | [MapGyver Media](https://github.com/Golden-Geek/MapGyver/blob/main/Source/Media/Media.h) and [composition ownership](https://github.com/Golden-Geek/MapGyver/blob/main/Source/Media/medias/composition/CompositionLayer/CompositionLayer.h) | Existing named framebuffers and owned/reference concepts. |
| S03 | [bgfx](https://github.com/bkaradzic/bgfx) and [native interoperability API](https://bkaradzic.github.io/bgfx/bgfx.html) | Renderer candidates/platforms, externally created texture handling and render-thread ownership constraints. |
| S04 | [SDL GPU drivers](https://wiki.libsdl.org/SDL3/SDL_CreateGPUDevice) | SDL's GPU abstraction lists Vulkan, D3D12 and Metal; this plan uses SDL platform/window integration and separately gates its renderer choice. |
| S05 | [Diligent backend matrix](https://github.com/DiligentGraphics/DiligentEngine#supported-platforms-and-low-level-graphics-apis) | Native Metal backend is identified as commercially licensed; not a default GPL source-build assumption. |
| S06 | [Spout SDK](https://github.com/leadedge/Spout2) and [D3D12 interoperability discussion](https://spout.discourse.group/t/directx-12-support/216) | Native sharing and D3D11/D3D12 bridge considerations. |
| S07 | [Syphon](https://syphon.info/) and [framework source](https://github.com/Syphon/Syphon-Framework) | IOSurface-backed sharing, Metal/OpenGL integration. |
| S08 | [PipeWire DMA-BUF sharing](https://pipewire.pages.freedesktop.org/pipewire/devel/page_dma_buf.html) | Format/modifier/synchronization negotiation and fallback considerations. |
| S09 | [FFmpeg format documentation](https://ffmpeg.org/general.html) | Broad compatibility; exact build/profile support still needs corpus testing. |
| S10 | [HAP developer integration](https://hap.video/developers.html) | Demux and decode to compressed GPU texture data, including Q colorspace handling. |
| S11 | [Resolume DXV documentation](https://resolume.com/support/en/rendering-to-dxv) | Specialized-player acceleration is not established by generic import compatibility. |
| S12 | [OSCQuery proposal](https://github.com/Vidvox/OSCQueryProposal) | HTTP structure, WebSocket value streaming, advertised extensions and subscription semantics. |
| S13 | [libossia](https://github.com/ossia/libossia) and [OSCQuery server API](https://ossia.io/libossia/html/classossia_1_1oscquery_1_1oscquery__server__protocol.html) | Initial protocol adapter candidate; checked repository LICENSE is LGPLv3. |
| S14 | [OMT C wrapper](https://github.com/openmediatransport/libomt), [VMX](https://github.com/openmediatransport/libvmx) and [OMT implementation](https://github.com/openmediatransport/libomtnet) | C exports, Native AOT build, MIT open components and ARM64 builds. |
| S15 | [Native AOT platforms](https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/) | Native 64-bit target support does not imply ARM32/armhf support. |
| S16 | [NDI distribution](https://docs.ndi.video/all/developing-with-ndi/sdk/software-distribution) and [current SDK licensing summary](https://docs.ndi.video/all/developing-with-ndi/sdk/licensing) | Headers/runtime have different distribution terms; full SDK agreement needs project-specific review. |
| S17 | [GNU GPL FAQ](https://www.gnu.org/licenses/gpl-faq.en.html) and [license compatibility](https://www.gnu.org/licenses/license-compatibility.en.html) | Dynamic loading/IPC is not by itself a complete licensing determination; GPLv3 is the proposed default. |
| S18 | [Windows display configuration](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig) and [device identity](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_target_device_name) | Display paths/EDID/device metadata for persistent output matching. |
| S19 | [Windows interactive services](https://learn.microsoft.com/en-us/windows/win32/services/interactive-services) | Graphics-session separation from services. |
| S20 | [Mesa V3D](https://docs.mesa3d.org/drivers/v3d.html) and [Raspberry Pi processors](https://www.raspberrypi.com/documentation/computers/processors.html) | Board-specific GLES/Vulkan and codec capabilities. |
| S21 | [GL Transitions](https://github.com/gl-transitions/gl-transitions) and [ISF](https://docs.isf.video/) | Sources for adapted, licensed and validated transition/effect conventions. |
| S22 | [Frame synchronization](https://www.nvidia.com/content/Control-Panel-Help/vLatest/en-us/mergedProjects/nvwks/Frame_Synchronization.htm) | Software clock coherence versus physical frame lock/genlock. |
| S23 | [Codex repository instructions](https://learn.chatgpt.com/docs/agent-configuration/agents-md) and [execution plans](https://developers.openai.com/cookbook/articles/codex_exec_plans) | Concise persistent instructions and durable multi-step task plans. |
| S24 | [GitHub runner reference](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) | Windows, macOS, Linux and native ARM runner availability/labels. |
| S25 | [Windows 2025 image](https://github.com/actions/runner-images/blob/main/images/windows/Windows2025-Readme.md) and [macOS ARM image](https://github.com/actions/runner-images/blob/main/images/macos/macos-15-arm64-Readme.md) | Actual toolchain-image choices; record versions because images evolve. |
| S26 | [Actions security guidance](https://docs.github.com/en/actions/reference/security/secure-use) | Public-fork and persistent/self-hosted hardware-runner trust boundaries. |
| S27 | [checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1), [setup-node v7.1.0](https://github.com/actions/setup-node/releases/tag/v7.1.0), [upload-artifact v7.0.2](https://github.com/actions/upload-artifact/releases/tag/v7.0.2) | Resolved full action revisions used in the templates. |

Action tag refs were resolved to commits through the GitHub connector; action metadata was read to verify Node 24 runtime requirements. Runner labels were checked against official documentation, and Windows/macOS image toolchains were inspected. These are planning inputs; actual CI starts when the target repository implements and activates the workflows.
