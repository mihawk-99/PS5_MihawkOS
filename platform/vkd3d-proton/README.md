# Pinned VKD3D-Proton source overlay

VKD3D-Proton stays on GE-Proton11-7's exact `af89350cc2eacd9da2293fbae96bd9ab4987c9bb` revision.

## How it is built

- `tools/build-vkd3d.py` exports that commit from `.deps/vkd3d-proton` with Git. `tools/fetch-runtime.sh` fetches the
  commit.
- Each submodule is exported at the revision that commit records. `tools/fetch-all.sh` fetches the submodules.
- The script applies the patches below and builds `d3d12` and `d3d12core` for PE32 and PE64 with the checksum-pinned
  LLVM MinGW toolchain, as `tools/build-dxvk.py` does for DXVK. It does nothing over the network.
- `tools/stage-wine.py` stages the result over GE's prebuilt builds of the same revision. The exports are the same.

## The patches

Patch 0001: feature level 12_0 on tiled resources tier 1.
- The console's RADV binds sparse memory. A game's binds go through the GPU broker: graphics patch 0041 gives a client
  sparse ranges from the owner, and 0043 lets it hold 4096.
- It has no PRT page bits, so it reports neither `shaderResourceResidency` nor `residencyNonResidentStrict`, and
  VKD3D-Proton stopped tiled resources at tier 1 and the feature level at 11_1.
- `D3D12CreateDevice` then refused every game that asks for 12_0 with `E_INVALIDARG`. A tester's DX12 game showed
  "GPU does not meet the minimum" (2026-10-31).
- 12_0 now asks for tier 1. The tier is still reported as 1, so a game that checks it sees the truth.
- Reserved resources stay real sparse ranges; nothing new is committed in their place.
- What is still refused, as before: a shader that queries residency (at pipeline creation), and a reserved 3D
  texture (it needs tier 3).
- With ROVs and conservative rasterization, which RADV reports on this GPU, 12_1 follows. 12_2 does not: it needs mesh
  shaders and VRS tier 2.
- The problem was found by reading PC5's vkd3d-proton 0001 (bornaradusin/PC5, LGPL-2.1), which makes the same change;
  ours is written here.

Patch 0002: the build's name.
- The exported source has no `.git` and sits inside a PS5_Proton checkout, so it names the build itself.
- Build id `af89350cc2eacd91`, also the pipeline cache's key. GE's prebuilt is `...d90`, so neither takes the
  other's cache.
- Version `v3.1.0-af89350cc-ps5-1`.

Patch 0003: shared resources without Win32 external handles.
- A shared resource's memory was made exportable and exported at once, and `CreateSharedHandle` exported it again,
  whether or not the device has `VK_KHR_external_memory_win32`.
- The broker's client has no Win32 external memory or semaphores. So Wine's allocation with an export request failed
  (Wine patch 2407 makes it an ordinary one), and `vkGetMemoryWin32HandleKHR` is NULL: a game that makes a shared
  resource at start (Unreal Engine 5 does) closed.
- Without the extension, a shared resource is ordinary memory, `CreateSharedHandle` returns `E_NOTIMPL`, and
  `OpenSharedHandle` opens nothing. The same holds for fences without `VK_KHR_external_semaphore_win32`.
- A shared fence the host cannot export is an ordinary fence, where `CreateFence` failed.
- The problem was found by reading PC5's vkd3d-proton 0002 and prospero-win 0054; the change is written here.
- `tests/d3d12-shared-probe.c` makes a shared render target and a shared fence, clears and signals them on the GPU,
  and asks both for handles.

## Games that ship the Agility SDK

A game that ships the Agility SDK gets VKD3D-Proton without anything of ours. Such a game exports `D3D12SDKVersion`
and `D3D12SDKPath` and carries Microsoft's `D3D12Core.dll` in `D3D12\x64`, or beside its exe.
- VKD3D-Proton's `d3d12.dll` reads neither export.
- `ID3D12SDKConfiguration::SetSDKVersion` returns `S_OK` and loads nothing, and `CreateDeviceFactory` ignores the path.
- `d3d12core` is loaded by name. When that finds the game's own copy beside the exe, it is not VKD3D-Proton's, so it
  is unloaded and `system32`'s is taken (`libs/d3d12/main.c`).

`tools/test-d3d12-agility.sh` proves both layouts on the host's Wine, with no device and no Vulkan driver. The game's
copy beside the exe is loaded and its DllMain runs before it is refused; the one in `D3D12\x64` is never loaded.

What could take VKD3D-Proton away is a game's own `WINEDLLOVERRIDES` (game.json "env", the WebUI's "More
environment"). It used to replace the title's. It now joins them (`src/pw_app_environment.h`), so a mod's
`dinput8=n,b` leaves `d3d12,d3d12core=n` in place.

Console qualification status lives in `docs/PLAN.md` and `evidence/`.
