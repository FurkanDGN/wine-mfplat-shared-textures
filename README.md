# Wine fixes for Unity video on macOS: black video and 4K stutter

Three small patches for Wine 11.0 (`mfplat.dll`, `mfreadwrite.dll`) plus CI-built
x86_64 DLLs, for Unity games whose videos stay **black while audio plays** or
**stutter at 4K** under Wine with DXMT (Whisky on macOS).

Search terms this is meant to answer:

- Unity video black Wine / Whisky, audio plays
- `Got null handle from IDXGIResource::GetSharedHandle` in `Player.log`
- Whisky DXMT video not showing
- 4K video stutter Whisky, Unity FMV game

## Status

| Patch | DLL | What it does | Status |
|---|---|---|---|
| `mfreadwrite-skip-redundant-copy.patch` | mfreadwrite | The source reader no longer copies samples that already have a DXGI buffer into a second allocator sample | Upstream candidate, not submitted |
| `mfplat-reuse-linear-buffers.patch` | mfplat | DXGI surface buffers reuse their frame-sized linear buffer between `Lock()` calls instead of allocating one every time | Upstream candidate, not submitted |
| `mfplat-shared-textures.patch` | mfplat | Creates allocator textures shareable by default | Workaround. Wine's own mfplat tests reject it (2 extra failures), so it cannot go upstream as is |

Released DLLs: `mfplat.dll` (shared textures and linear buffer reuse) and
`mfreadwrite.dll` (redundant copy skipped).

Nothing has been submitted to Wine yet. I could not check what Windows does.

## What is wrong

Unity's Media Foundation video path opens the source reader with advanced video
processing and a DXGI device manager, asks for an `ARGB32` video type, gets the
video processor from `GetServiceForStream` and sets
`MF_SA_D3D11_SHARED_WITHOUT_MUTEX`, bind flags and usage on the processor's
output stream attributes. It then opens each returned texture on its own
render device with `IDXGIResource::GetSharedHandle`.

Wine's source reader copies every returned sample into a sample from its own
allocator. That copy has two effects:

- The copy is not shareable and ignores what Unity configured on the processor,
  so on a d3d11 implementation that really supports shared resources (DXMT 0.72
  or newer) the handle can come back null and no frame is displayed. In a 30
  second test without the shared texture workaround, skipping the copy produced no
  `Got null handle` lines, so it may make the workaround unnecessary. That was
  judged from the log only, and the run also used a patched GStreamer decoder.
- Together with a fresh frame-sized `malloc()` on every DXGI `Lock()` it makes
  each 4K frame (33 MB) be allocated and filled about four times. That, not
  decoding, was the cause of the 4K stutter. Decoding a 4K frame takes about
  2.7 ms.

Related upstream threads: Wine bug 50277, merge requests 11398 and 11404 (those
change wined3d and dxgi, a different layer).

## Measurements

Beta Kafe intro, 3840x2160 H.264, 25 fps, Whisky 3.7.0 stock engine (Wine 11.0),
DXMT 0.80, Apple M4 Pro, stock GStreamer runtime. Timings come from temporary
timing code in `mfreadwrite`, with the same logic as the released DLLs.

| | Stock | With these DLLs |
|---|---|---|
| In game, frames delivered per second | 16.3 | about 25 |
| In game, frame intervals longer than 50 ms | 47 % | 7 % |
| In game, CPU per 60 s | 78 s | 64 s |
| Offline reader loop (`tools/mfbench.c`), 4K | 22 fps | 66 fps |

The 1080p intro already ran fine without the patches (79 fps offline).

## Download and verify

No binary is committed to this repository. The DLLs are built by GitHub Actions
from the `wine-11.0` tag plus the patches (see `.github/workflows/build.yml`) and
attached to each release with their SHA-256 and a signed build provenance
attestation:

```bash
gh attestation verify mfplat.dll --owner FurkanDGN
gh attestation verify mfreadwrite.dll --owner FurkanDGN
sha256sum -c SHA256SUMS
```

Releases: https://github.com/FurkanDGN/wine-mfplat-shared-textures/releases

## Install (Whisky, stock engine v3.1.1 / Wine 11.0)

```bash
WINELIB="$HOME/Library/Application Support/com.franke.Whisky/Libraries/Wine/lib/wine/x86_64-windows"
cp "$WINELIB/mfplat.dll" "$WINELIB/mfplat.dll.orig"
cp "$WINELIB/mfreadwrite.dll" "$WINELIB/mfreadwrite.dll.orig"
cp mfplat.dll mfreadwrite.dll "$WINELIB/"
```

A Whisky engine update overwrites the files; copy them again afterwards. To
revert, move the `.orig` files back.

The released DLLs only match Wine 11.0. For any other Wine, rebuild.

Video decoding itself also needs GStreamer: on the Whisky stock engine install
the official runtime pkg (1.26.9 or newer) from
https://gstreamer.freedesktop.org/data/pkg/osx/ , otherwise no video plays at all
(frankea/Whisky#165).

## Rebuild

```bash
git clone --depth 1 --branch wine-11.0 https://gitlab.winehq.org/wine/wine.git
cd wine
git apply ../mfplat-shared-textures.patch ../mfplat-reuse-linear-buffers.patch ../mfreadwrite-skip-redundant-copy.patch
./configure --enable-win64 --without-x --without-freetype --disable-tests
make -j"$(nproc)" dlls/mfplat/x86_64-windows/mfplat.dll dlls/mfreadwrite/x86_64-windows/mfreadwrite.dll
```

On an Apple Silicon Mac, build with `brew install mingw-w64 bison`, configure
with `CC="gcc -arch x86_64" CXX="g++ -arch x86_64"` and `--host=x86_64-apple-darwin`.

## Benchmark tool

`tools/mfbench.c` reads a video through a source reader configured like Unity's
(DXGI device manager, advanced video processing, `ARGB32` output) and prints
frames per second, frame interval statistics, whether `GetServiceForStream`
returns the video processor, and whether the sample has a DXGI buffer. No game is
needed.

```bash
x86_64-w64-mingw32-gcc -O2 -municode -o mfbench.exe tools/mfbench.c \
    -lmfplat -lmfreadwrite -lmf -lmfuuid -ld3d11 -lole32 -luuid
wine64 mfbench.exe 'C:\path\to\video.mp4' 300
```

Add `nod3d` to run without a D3D device and `nv12` to ask for NV12.

## Limits

- One title, one machine, Wine 11.0 with DXMT only. wined3d, DXVK and Windows were not tested.
- The linear buffer reuse keeps up to four idle frame-sized buffers (about 130 MB at 4K).
- Wine's own `mfplat` and `mfreadwrite` tests show no new failures for the two
  upstream candidates, with the same failures before and after.

## Tested with

Beta Kafe: Write Your Love Story (Unity 6000.0.68f1), Whisky (frankea fork)
3.7.0, stock engine Wine 11.0, DXMT 0.80, Apple Silicon. The same title also
needs the Unity 6 pointer shim on that engine:
https://github.com/FurkanDGN/unity6-wine-pointer-fix

## License

MIT, see `LICENSE`. The patches are modifications of Wine, which is LGPL-2.1+;
the released DLLs are Wine code and stay under the LGPL.
