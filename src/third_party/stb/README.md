# stb — vendored

Single-file public-domain libraries by **Sean Barrett**, from
<https://github.com/nothings/stb>.

| File | Version | Used by |
|---|---|---|
| `stb_image.h` | v2.30 | PNG/JPG import in the HGR and TMS9918 paint editors (`hgrpaint/HgrImageDecode.cpp`, `tmspaint/TmsImageDecode.cpp`), plus the app icon and the Help ▸ Photos windows (`main_imgui.cpp`, `MainWindow_Dialogs.cpp`) |
| `stb_image_write.h` | v1.16 | PNG output — the Terminal Card screenshot (`MainWindow_ImGui.cpp`), the paint/sprite editors' *Save PNG* (`Pom1HgrPaintHost.cpp`, `Pom1TmsPaintHost.cpp`); the implementation is instantiated once, in `main_imgui.cpp` |

A third file from the same collection lives one level up rather than here:
`../stb_vorbis.c` (v1.22), for Ogg playback — `#include`d by `AudioDevice.cpp`
(which is where its warnings are charged) and reached from `SID.cpp`. It carries
the same licence.

## Licence

**Dual: MIT *or* public domain (Unlicense) — take whichever you prefer.**
Copyright (c) 2017 Sean Barrett. The full text is in [`LICENSE`](LICENSE), and
each header repeats it verbatim in its own trailer (the two are byte-identical,
and match the upstream `LICENSE` file).

POM1 itself is GPL-3.0; both alternatives are compatible with that.

## Updating

Replace the file, keep the licence trailer, and check the version line in its
header against the table above. These headers are compiled as SYSTEM includes —
they are not ours to keep warning-clean, and `stb_image_write.h` additionally
needs the local pragma block in `main_imgui.cpp`, because it is included by
relative path through `POM1_SRC_DIR` and GCC classifies a header by where it
*resolved* it rather than by name.
