// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// tmspaint::decodeImageFile — PNG/JPG/BMP → RGBA via stb_image. Own translation
// unit so the pure converter (TmsConvert.cpp) stays unit-testable without an image
// decoder. The stb_image *implementation* is linked from the host app (main_imgui
// / MainWindow_Dialogs define STB_IMAGE_IMPLEMENTATION); here only the decls.

#include "TmsConvert.h"

#include "stb_image.h"   // bare include — host puts src/third_party/stb on the path

namespace tmspaint {

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

} // namespace tmspaint
