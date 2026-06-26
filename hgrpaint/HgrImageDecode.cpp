// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// hgrpaint::decodeImageFile — PNG/JPG/BMP → RGBA via stb_image. Kept in its own
// translation unit so the pure converter (HgrConvert.cpp) stays unit-testable
// without pulling in an image decoder. The stb_image *implementation* is linked
// from the host app (main_imgui / MainWindow_Dialogs define
// STB_IMAGE_IMPLEMENTATION); here we only use its declarations.

#include "HgrConvert.h"

#include "third_party/stb/stb_image.h"   // decl only (impl linked from the app)

namespace hgrpaint {

bool decodeImageFile(const std::string& path, int& w, int& h,
                     std::vector<uint8_t>& rgba, std::string& err)
{
    w = h = 0;
    int channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        const char* why = stbi_failure_reason();
        err = std::string("cannot decode image: ") + (why ? why : "unknown format");
        return false;
    }
    rgba.assign(pixels, pixels + static_cast<size_t>(w) * h * 4);
    stbi_image_free(pixels);
    return true;
}

} // namespace hgrpaint
