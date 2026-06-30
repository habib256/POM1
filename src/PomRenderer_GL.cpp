// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PomRenderer_GL.cpp — OpenGL3 / OpenGL ES3 backend for PomRenderer.
//
// Phase 1 of the macOS Metal port: every direct gl* call that lived in
// main_imgui.cpp / Screen_ImGui / MainWindow_HardwareWindows /
// MainWindow_Dialogs now routes through `PomRenderer::*` so the rest of
// POM1 has no visibility into the underlying graphics API. Behaviour is
// preserved bit-for-bit — every glTexParameter / glBindTexture /
// glTexImage2D pair below mirrors the original call sites verbatim.
//
// The screenshot path's Y-flip is kept here (glReadPixels is bottom-up,
// the PNG writer wants top-down) so the caller (main_imgui.cpp) does not
// need to know which way the framebuffer is oriented.
//
// On WASM the Emscripten/WebGL2 link line forces IMGUI_IMPL_OPENGL_ES3 and
// the very same code compiles unchanged — GLES3 covers every entry point
// we use here.

#include "PomRenderer_Internal.h"   // also pulls in PomRenderer.h
#include "POM1Build.h"

#include "imgui.h"
#include "backends/imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace pom1 {

// `Texture` is defined in PomRenderer.cpp (single struct shared by both
// backends — see the comment in there). This backend only touches `glId`,
// `w`, `h`; the Metal field stays zero-init.

namespace {

class GlRenderer final : public PomRenderer {
public:
    explicit GlRenderer(GLFWwindow* window) : window_(window) {}
    ~GlRenderer() override = default;

    Texture* createTexture(int w, int h, Filter f,
                           const uint32_t* pixels) override
    {
        auto* t = new Texture{};
        t->w = w;
        t->h = h;
        glGenTextures(1, &t->glId);
        glBindTexture(GL_TEXTURE_2D, t->glId);
        const GLint filt = (f == Filter::Linear) ? GL_LINEAR : GL_NEAREST;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filt);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // RGBA8 rows are always 4-byte aligned, but the photo path used to
        // toggle this to 1 anyway (so any future non-multiple-of-4 width
        // works). Save + restore so we never leave the shared context in
        // a non-default state.
        GLint prevAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
        return t;
    }

    void updateTexture(Texture* t, const uint32_t* pixels) override
    {
        if (!t || t->glId == 0 || !pixels) return;
        glBindTexture(GL_TEXTURE_2D, t->glId);
        GLint prevAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, t->w, t->h,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
    }

    void destroyTexture(Texture* t) override
    {
        if (!t) return;
        if (t->glId != 0) glDeleteTextures(1, &t->glId);
        delete t;
    }

    ImTextureID asImTextureID(const Texture* t) const override
    {
        // ImTextureID is configured as ImU64 repo-wide (see CMake comment
        // about ImDrawIdx in POM1ImGuiPreinclude.h; same family of
        // overrides). The cast funnels through uintptr_t so this works
        // identically on 32-bit and 64-bit hosts.
        if (!t) return (ImTextureID)0;
        return (ImTextureID)(uintptr_t)t->glId;
    }

    bool initImGuiBackend(const char* glslVersion) override
    {
        return ImGui_ImplOpenGL3_Init(glslVersion);
    }

    void shutdownImGuiBackend() override
    {
        ImGui_ImplOpenGL3_Shutdown();
    }

    void beginFrame() override
    {
        ImGui_ImplOpenGL3_NewFrame();
    }

    void clear(int fbW, int fbH, float r, float g, float b, float a) override
    {
        glViewport(0, 0, fbW, fbH);
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT);
    }

    void renderDrawData(ImDrawData* drawData) override
    {
        ImGui_ImplOpenGL3_RenderDrawData(drawData);
    }

    void present() override
    {
        if (window_) glfwSwapBuffers(window_);
    }

    bool readBackbufferRGBA(int& outW, int& outH,
                            std::vector<uint8_t>& outPixels) override
    {
        if (!window_) return false;
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (fbW < 1 || fbH < 1) return false;

        outW = fbW;
        outH = fbH;
        outPixels.assign(static_cast<size_t>(fbW) * fbH * 4, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, fbW, fbH, GL_RGBA, GL_UNSIGNED_BYTE,
                     outPixels.data());

        // Y-flip in place: glReadPixels gives bottom-up, callers (the PNG
        // screenshot path) want top-down. Doing it inside the renderer keeps
        // the Metal backend free to skip the flip when its layer is already
        // top-down.
        const size_t rowBytes = static_cast<size_t>(fbW) * 4;
        std::vector<uint8_t> rowTmp(rowBytes);
        for (int y = 0; y < fbH / 2; ++y) {
            uint8_t* top = outPixels.data() + static_cast<size_t>(y) * rowBytes;
            uint8_t* bot = outPixels.data() + static_cast<size_t>(fbH - 1 - y) * rowBytes;
            std::memcpy(rowTmp.data(), top, rowBytes);
            std::memcpy(top, bot, rowBytes);
            std::memcpy(bot, rowTmp.data(), rowBytes);
        }
        return true;
    }

private:
    GLFWwindow* window_ = nullptr;
};

} // namespace

// Called by makeRenderer (PomRenderer.cpp) when POM1_HAS_METAL is not set —
// the OpenGL backend is the default everywhere except macOS-with-Metal and
// stays the only option under WASM (Emscripten ports the GL3 backend to
// WebGL2 / GLES3).
std::unique_ptr<PomRenderer> makeGlRenderer(GLFWwindow* window)
{
    return std::unique_ptr<PomRenderer>(new GlRenderer(window));
}

} // namespace pom1
