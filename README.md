# Wine mfplat: shareable Media Foundation sample textures (Unity black video fix)

A one-hunk patch for Wine's `mfplat.dll` plus a CI-built x86_64 DLL for
Wine 11.0, for Unity games whose videos stay **black while audio plays**
under Wine with DXMT (Whisky on macOS) or DXVK.

Search terms this is meant to answer:

- Unity video black Wine / Whisky, audio plays
- `Got null handle from IDXGIResource::GetSharedHandle` in `Player.log`
- Whisky DXMT video not showing

## What is wrong

Unity's Media Foundation video path decodes into Direct3D 11 textures
allocated by MF's sample allocator and then opens each texture on its own
render device with `IDXGIResource::GetSharedHandle`. Wine's allocator
(`dlls/mfplat/sample.c`, `sample_allocator_initialize`) only creates the
textures with `D3D11_RESOURCE_MISC_SHARED*` when the app sets the
`MF_SA_D3D11_SHARED*` attributes. Unity sets none, so on a d3d11
implementation that really supports shared resources (DXMT ≥ 0.72, DXVK)
the handle comes back null and no frame is ever displayed.

`mfplat-shared-textures.patch` defaults `D3D11_RESOURCE_MISC_SHARED` on
the allocator's textures when usage is `D3D11_USAGE_DEFAULT` and no sharing
flag was requested. Related upstream threads: Wine bug 50277, merge
requests 11398 and 11404.

This is a **workaround**, not an upstream fix. Wine's own test shows that on
Windows the bare allocator does not share by default, so the proper fix
probably belongs in the source reader path and still needs to be confirmed
on Windows first. Nothing has been submitted upstream; this patch is simply
the smallest change that makes the videos render.

## Download and verify

No binary is committed to this repository. `mfplat.dll` is built by GitHub
Actions from the `wine-11.0` tag plus this patch (see
`.github/workflows/build.yml`) and attached to each release with its
SHA-256 and a signed build provenance attestation:

```bash
gh attestation verify mfplat.dll --owner FurkanDGN
sha256sum -c SHA256SUMS
```

Releases: https://github.com/FurkanDGN/wine-mfplat-shared-textures/releases

## Install (Whisky, stock engine v3.1.1 / Wine 11.0)

```bash
WINELIB="$HOME/Library/Application Support/com.franke.Whisky/Libraries/Wine/lib/wine/x86_64-windows"
cp "$WINELIB/mfplat.dll" "$WINELIB/mfplat.dll.orig"
cp mfplat.dll "$WINELIB/mfplat.dll"
```

A Whisky engine update overwrites the file; copy it again afterwards. To
revert, move `mfplat.dll.orig` back.

The released DLL only matches Wine 11.0. For any other Wine, rebuild.

Video decoding itself also needs GStreamer: on the Whisky stock engine
install the official runtime pkg (1.26.9 or newer) from
https://gstreamer.freedesktop.org/data/pkg/osx/ , otherwise no video plays
at all (frankea/Whisky#165).

## Rebuild

```bash
git clone --depth 1 --branch wine-11.0 https://gitlab.winehq.org/wine/wine.git
cd wine && git apply ../mfplat-shared-textures.patch
./configure --enable-win64 --without-x --without-freetype --disable-tests
make dlls/mfplat/x86_64-windows/mfplat.dll -j$(nproc)
```

The patch also applies cleanly to current Wine master.

## Tested with

Beta Kafe: Write Your Love Story (Unity 6000.0.68f1), Whisky (frankea fork)
3.7.0, stock engine Wine 11.0, DXMT 0.80, Apple Silicon. The same title also
needs the Unity 6 pointer shim on that engine:
https://github.com/FurkanDGN/unity6-wine-pointer-fix

## License

MIT, see `LICENSE`. The patch is a modification of Wine, which is LGPL-2.1+;
the released `mfplat.dll` is Wine code and stays under the LGPL.
