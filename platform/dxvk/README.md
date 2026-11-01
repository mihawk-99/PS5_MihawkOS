# Pinned DXVK source overlay

DXVK remains on GE-Proton11-7's exact `601930949d111edbbcf9dd463948426d9f8f6ddd`
revision. `pins.json` pins the source and every header/compiler submodule by
revision and archive SHA256. `tools/build-dxvk.py` uses cached inputs only and
builds all five D3D modules for PE32 and PE64 with the existing checksum-pinned
LLVM MinGW toolchain. Missing inputs report their exact public URL and hash.
Sibling repositories remain read-only.

Patch 0001 prepares deferred clears and shader-readable layouts before DXVK's
external blitter accesses ordinary images. The initial symptom was a black
unbound ClearRenderTargetView followed by Present. CopyResource/Map forced
correct pixels, but is diagnostic only. The unmodified pinned release
reproduces black presentation on Linux Wine 11.18 in both architectures.
The normal console gate uses no readback and changes colour after resize and
child exit. No program-specific switch or resource binding is required.

Patch 0002 gives the exported source an explicit patched build identity;
Git discovery must not name the containing PS5_Proton checkout.

The build manifest fingerprints owned inputs and hashes every DLL. Packaging
refuses stale sources or altered DLLs, installs the same modules into both
managed prefix architectures, and includes upstream licence notices.
Console qualification status lives in `docs/PLAN.md` and `evidence/`.

Patch 0007 shares a texture between a Direct3D 9Ex device and a Direct3D 11 device of one process where the driver has no
Win32 external memory (the console's): a movie player that decodes into a shared D3D9Ex texture which the same game's
Direct3D 11 device opens showed nothing, because the D3D9 handle was `INVALID_HANDLE_VALUE` and `OpenSharedResource` answered
`80070057` (measured on the Pro with `tests/d3d9ex-d3d11-share-probe.c`). A D3D9Ex device made while a Direct3D 11 device lives, on
the same GPU, now runs on that device's Vulkan device (d3d11.dll lists its devices and exports `DXVKLocalShareGetDevice`;
both DLLs stay loaded once they take part), a texture it is asked to share is listed in a table in the device
(`dxvk_local_share.h`) under a handle of its own, and `OpenSharedResource` makes the Direct3D 11 texture around that image after
checking that it is what the description would make. The commands that end each list also carry the clears of a listed texture (DXVK
defers a clear to a render pass or draw that, for a texture the other device reads, never comes: the first Pro run read zeros) and end
with a memory barrier. Where handles work nothing changes. Config: `d3d9.shareD3D11Device`
(default True) turns it off; `d3d9.shareD3D11DeviceAlways` (default False) also uses it where handles would work, to try the path
on a PC under Wine. Not covered: a D3D9Ex device made before the Direct3D 11 one, and a Direct3D 11 texture opened by D3D9.
`tests/test_dxvk_local_share.py` applies the series to the pinned source and runs the table; the APIs using it together are the
probe's to show. Written from the problem PC5's patches/dxvk 0001 to 0003 describe (NOTICE.md, "PC5").
