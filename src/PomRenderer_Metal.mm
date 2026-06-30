// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// PomRenderer_Metal.mm — macOS Metal backend for PomRenderer.
//
// Only built when CMake selects POM1_RENDERER=metal (default on Apple-non-
// WASM). Owns a CAMetalLayer attached to GLFW's NSWindow contentView, an
// MTLDevice + MTLCommandQueue, and routes the same PomRenderer interface
// the GL backend exposes to ImGui's imgui_impl_metal.mm.
//
// Frame contract — mirrors what main_imgui.cpp's loop already does:
//
//   beginFrame():
//     acquire CAMetalDrawable    (== next swap-chain back-buffer)
//     allocate a fresh command buffer
//     build MTLRenderPassDescriptor (texture = drawable.texture,
//                                    loadAction = MTLLoadActionClear,
//                                    clearColor = stash 0,0,0,1 for now)
//     ImGui_ImplMetal_NewFrame(renderPassDescriptor)
//
//   clear(fbW,fbH,r,g,b,a):
//     overwrite renderPassDescriptor.clearColor — the actual clear happens
//     when the render encoder is started in renderDrawData(). fbW/fbH are
//     ignored (Metal owns the back-buffer size via the layer).
//
//   renderDrawData(drawData):
//     id<MTLRenderCommandEncoder> = [commandBuffer renderCommandEncoder...]
//     ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, encoder)
//     [encoder endEncoding]
//
//   present():
//     [commandBuffer presentDrawable:drawable]
//     [commandBuffer commit]
//
//   readBackbufferRGBA():
//     before the present commit, schedule a blit drawable.texture →
//     staging texture (MTLStorageModeShared), commit + wait, getBytes.
//     CAMetalLayer.framebufferOnly is set to NO at construction so the
//     drawable is allowed to be a blit source.
//
// Threading: Metal command buffers must be touched from the same thread,
// which is the main render thread here — same constraint as the GL path.

#include "PomRenderer_Internal.h"   // also pulls in PomRenderer.h
#include "POM1Build.h"

#if defined(POM1_HAS_METAL) && POM1_HAS_METAL

#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include "imgui.h"
#include "backends/imgui_impl_metal.h"

#include <cstring>
#include <vector>

namespace pom1 {

namespace {

class MetalRenderer final : public PomRenderer {
public:
    explicit MetalRenderer(GLFWwindow* window)
        : window_(window)
    {
        // Ownership / retain audit under -fno-objc-arc:
        //   device_       — MTLCreateSystemDefaultDevice() returns +1 retained (CF-style)
        //   commandQueue_ — -newCommandQueue starts with "new" → +1 retained
        //   metalLayer_   — +CAMetalLayer.layer is a factory class method (autoreleased)
        //                   so we [retain] explicitly to keep it alive past the next pool drain
        //   renderPassDescriptor_ — same as metalLayer_, also autoreleased
        // Matching releases live in ~MetalRenderer.
        device_       = MTLCreateSystemDefaultDevice();
        commandQueue_ = [device_ newCommandQueue];

        // Hook the layer to GLFW's NSWindow. The window is created with
        // GLFW_CLIENT_API=GLFW_NO_API on macOS-Metal (see main_imgui.cpp)
        // so the contentView has no GL context to fight.
        NSWindow* nsWindow = glfwGetCocoaWindow(window);
        nsWindow.contentView.wantsLayer = YES;
        metalLayer_ = [[CAMetalLayer layer] retain];
        metalLayer_.device          = device_;
        metalLayer_.pixelFormat     = MTLPixelFormatBGRA8Unorm;
        metalLayer_.framebufferOnly = NO;  // readBackbufferRGBA needs blit-src
        metalLayer_.displaySyncEnabled = YES;
        metalLayer_.contentsScale   = nsWindow.backingScaleFactor;
        nsWindow.contentView.layer  = metalLayer_;

        // Initial drawable size — keep in sync with framebufferSize on every
        // beginFrame in case the user resizes the window. GLFW posts the new
        // backing-size before we get here.
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w > 0 && h > 0) {
            metalLayer_.drawableSize = CGSizeMake((CGFloat)w, (CGFloat)h);
        }

        renderPassDescriptor_ = [[MTLRenderPassDescriptor renderPassDescriptor] retain];
    }

    ~MetalRenderer() override
    {
        // ARC is off (CMake's -fno-objc-arc); release everything we created
        // or retained. Drawable/encoder/cb are short-lived per frame and
        // released in present(); nothing to do here for those.
        [renderPassDescriptor_ release];
        [metalLayer_           release];
        [commandQueue_         release];
        [device_               release];
    }

    // ─── Texture lifecycle ──────────────────────────────────────────────
    Texture* createTexture(int w, int h, Filter f,
                           const uint32_t* pixels) override
    {
        auto* t = new Texture{};
        t->w = w;
        t->h = h;

        MTLTextureDescriptor* desc = [MTLTextureDescriptor
            texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                          width:(NSUInteger)w
                                         height:(NSUInteger)h
                                      mipmapped:NO];
        desc.usage       = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModeManaged;

        // -newTextureWithDescriptor: returns a +1 retained id<MTLTexture>
        // (the "new" prefix establishes the +1 convention under manual
        // retain/release). We stash the pointer as-is and release it back
        // in destroyTexture. ARC is OFF on this file (CMake's
        // -fno-objc-arc), so the __bridge_* casts are unavailable and a
        // plain (void*) cast is the correct way to type-pun the pointer.
        id<MTLTexture> mtl = [device_ newTextureWithDescriptor:desc];
        t->mtlTexture = (void*)mtl;
        // Filter param: imgui_impl_metal's fragment shader hardcodes one
        // inline sampler, so per-texture switching isn't available without
        // forking the backend. POM1 instead patches the upstream copy at
        // CMake configure time (linear→nearest) so every draw samples with
        // nearest filtering — the right thing for HGR/TMS/GT6144/glyph
        // atlas/paint canvases. Photos + fonts also get nearest; this is
        // visually invisible at integer zoom and acceptable elsewhere.
        // See CMakeLists.txt:`imgui_impl_metal.patched.mm`.
        (void)f;

        if (pixels) {
            MTLRegion region = MTLRegionMake2D(0, 0, w, h);
            const NSUInteger bytesPerRow = (NSUInteger)w * 4;
            [mtl replaceRegion:region
                   mipmapLevel:0
                     withBytes:pixels
                   bytesPerRow:bytesPerRow];
        }
        return t;
    }

    void updateTexture(Texture* t, const uint32_t* pixels) override
    {
        if (!t || !t->mtlTexture || !pixels) return;
        id<MTLTexture> mtl = (id<MTLTexture>)t->mtlTexture;
        MTLRegion region = MTLRegionMake2D(0, 0, t->w, t->h);
        const NSUInteger bytesPerRow = (NSUInteger)t->w * 4;
        [mtl replaceRegion:region
               mipmapLevel:0
                 withBytes:pixels
               bytesPerRow:bytesPerRow];
    }

    void destroyTexture(Texture* t) override
    {
        if (!t) return;
        if (t->mtlTexture) {
            id<MTLTexture> mtl = (id<MTLTexture>)t->mtlTexture;
            [mtl release];   // balances the +1 from -newTextureWithDescriptor:
            t->mtlTexture = nullptr;
        }
        delete t;
    }

    ImTextureID asImTextureID(const Texture* t) const override
    {
        // ImGui_ImplMetal_RenderDrawData accepts the id<MTLTexture> raw
        // pointer through ImTextureID (cast back inside its render loop).
        // POM1's ImTextureID is configured as ImU64, which fits a pointer
        // on every supported macOS architecture (x86_64 / arm64).
        if (!t || !t->mtlTexture) return (ImTextureID)0;
        return (ImTextureID)(uintptr_t)t->mtlTexture;
    }

    int  textureWidth(const Texture* t)  const override { return t ? t->w : 0; }
    int  textureHeight(const Texture* t) const override { return t ? t->h : 0; }

    // ─── ImGui backend lifecycle ───────────────────────────────────────
    bool initImGuiBackend(const char* /*glslVersion*/) override
    {
        return ImGui_ImplMetal_Init(device_);
    }

    void shutdownImGuiBackend() override
    {
        ImGui_ImplMetal_Shutdown();
    }

    // ─── Per-frame methods ─────────────────────────────────────────────
    void beginFrame() override
    {
        // Keep the layer's drawable size in lockstep with the framebuffer —
        // GLFW resizes the NSWindow but doesn't tell CAMetalLayer; if we
        // skip this the next drawable retains the old size and ImGui draws
        // letterboxed for one frame after every resize.
        int fbW = 0, fbH = 0;
        glfwGetFramebufferSize(window_, &fbW, &fbH);
        if (fbW > 0 && fbH > 0) {
            CGSize want = CGSizeMake((CGFloat)fbW, (CGFloat)fbH);
            if (!CGSizeEqualToSize(metalLayer_.drawableSize, want))
                metalLayer_.drawableSize = want;
        }

        drawable_ = [metalLayer_ nextDrawable];
        if (!drawable_) return;   // window minimised / occluded; skip the frame
        [drawable_ retain];

        commandBuffer_ = [[commandQueue_ commandBuffer] retain];

        renderPassDescriptor_.colorAttachments[0].texture     = drawable_.texture;
        renderPassDescriptor_.colorAttachments[0].loadAction  = MTLLoadActionClear;
        renderPassDescriptor_.colorAttachments[0].storeAction = MTLStoreActionStore;
        // clearColor gets overwritten by clear() before the encoder starts.
        renderPassDescriptor_.colorAttachments[0].clearColor  =
            MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        ImGui_ImplMetal_NewFrame(renderPassDescriptor_);
    }

    void clear(int /*fbW*/, int /*fbH*/,
               float r, float g, float b, float a) override
    {
        if (!drawable_) return;
        renderPassDescriptor_.colorAttachments[0].clearColor =
            MTLClearColorMake((double)r, (double)g, (double)b, (double)a);
    }

    void renderDrawData(ImDrawData* drawData) override
    {
        if (!drawable_ || !commandBuffer_) return;
        // @autoreleasepool: the render encoder is autoreleased and we don't
        // retain it; without an explicit pool the GLFW-driven main loop never
        // drains the thread-default pool, so per-frame autoreleased objects
        // (encoder, transient ImGui-Metal buffers) accumulate. One pool per
        // frame keeps Activity Monitor's memory bar flat.
        @autoreleasepool {
            id<MTLRenderCommandEncoder> encoder =
                [commandBuffer_ renderCommandEncoderWithDescriptor:renderPassDescriptor_];
            [encoder pushDebugGroup:@"POM1 ImGui"];
            ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer_, encoder);
            [encoder popDebugGroup];
            [encoder endEncoding];
        }
    }

    void present() override
    {
        // readBackbufferRGBA short-circuits the frame finalize (encodes
        // the blit + presentDrawable on commandBuffer_ itself, then waits
        // and releases). When that ran this frame, drawable_/commandBuffer_
        // are already nil — nothing to do here.
        if (!drawable_) return;
        [commandBuffer_ presentDrawable:drawable_];
        [commandBuffer_ commit];
        [commandBuffer_ release];
        [drawable_      release];
        commandBuffer_ = nil;
        drawable_      = nil;
    }

    bool readBackbufferRGBA(int& outW, int& outH,
                            std::vector<uint8_t>& outPixels) override
    {
        if (!drawable_ || !commandBuffer_) return false;
        @autoreleasepool {
            id<MTLTexture> src = drawable_.texture;
            const NSUInteger w = src.width;
            const NSUInteger h = src.height;
            if (w == 0 || h == 0) return false;

            // Staging texture in shared storage so the CPU can read it.
            // Drawable is BGRA; we keep the same format here and swizzle
            // B↔R below so callers get GL-style RGBA top-down.
            MTLTextureDescriptor* desc = [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                              width:w
                                             height:h
                                          mipmapped:NO];
            desc.usage       = MTLTextureUsageShaderRead;
            desc.storageMode = MTLStorageModeShared;
            id<MTLTexture> staging = [device_ newTextureWithDescriptor:desc];

            // Encode the blit ONTO commandBuffer_ (the same buffer that
            // holds this frame's ImGui draws), then presentDrawable + commit
            // it here. A separate command buffer would commit out of order —
            // the blit would read drawable.texture BEFORE the ImGui draws
            // were even encoded, producing a stale/empty PNG. Inlining the
            // blit on commandBuffer_ + presenting on the same buffer is the
            // only way to read the just-rendered frame on Metal.
            id<MTLBlitCommandEncoder> blit =
                [commandBuffer_ blitCommandEncoder];
            [blit copyFromTexture:src
                      sourceSlice:0
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(w, h, 1)
                        toTexture:staging
                 destinationSlice:0
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
            [blit endEncoding];
            [commandBuffer_ presentDrawable:drawable_];
            [commandBuffer_ commit];
            [commandBuffer_ waitUntilCompleted];

            outW = (int)w;
            outH = (int)h;
            const size_t rowBytes = (size_t)w * 4;
            outPixels.assign(rowBytes * h, 0);
            [staging getBytes:outPixels.data()
                  bytesPerRow:rowBytes
                   fromRegion:MTLRegionMake2D(0, 0, w, h)
                  mipmapLevel:0];
            [staging release];   // balances the +1 from -newTextureWithDescriptor:

            // commandBuffer_ + drawable_ are done with — release them now so
            // present() (which always runs after this on the screenshot path)
            // detects the frame is finalized and no-ops.
            [commandBuffer_ release];
            [drawable_      release];
            commandBuffer_ = nil;
            drawable_      = nil;

            // BGRA → RGBA in place. Metal drawables on Apple GPUs are BGRA8;
            // POM1's PNG writer expects RGBA8, so we swap the blue/red
            // channels. No Y-flip — CAMetalLayer is top-down by default.
            for (size_t i = 0; i + 3 < outPixels.size(); i += 4)
                std::swap(outPixels[i], outPixels[i + 2]);
            return true;
        }
    }

private:
    GLFWwindow*               window_              = nullptr;
    id<MTLDevice>             device_              = nil;
    id<MTLCommandQueue>       commandQueue_        = nil;
    CAMetalLayer*             metalLayer_          = nil;
    MTLRenderPassDescriptor*  renderPassDescriptor_ = nil;
    id<CAMetalDrawable>       drawable_            = nil;   // alive between beginFrame/present
    id<MTLCommandBuffer>      commandBuffer_       = nil;   // ditto
};

} // namespace

std::unique_ptr<PomRenderer> makeMetalRenderer(GLFWwindow* window)
{
    return std::unique_ptr<PomRenderer>(new MetalRenderer(window));
}

} // namespace pom1

#endif // POM1_HAS_METAL
