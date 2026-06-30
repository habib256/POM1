// Pom1 Apple 1 Emulator
// Copyright (C) 2000-2026 Verhille Arnaud
//
// NativeFileDialog_Mac.mm — macOS NSOpenPanel / NSSavePanel backend for the
// portable NativeFileDialog API. Compiled as Objective-C++ so we can call
// directly into AppKit. Linked into the same target as the rest of POM1 — the
// "-framework Cocoa" link line already drives this.

#include "NativeFileDialog.h"
#include "POM1Build.h"

#if !POM1_IS_WASM && defined(__APPLE__)

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <vector>
#include <string>

namespace pom1 {

namespace {

// Convert the portable filter list into an NSArray of NSString extensions
// (legacy API) — NSOpenPanel.allowedFileTypes / .allowedContentTypes accept
// extensions without the leading dot. We flatten every filter's extensions
// into a single allow-list (matches Win32 behaviour where the user can also
// toggle "All files (*.*)").
NSArray<NSString*>* flattenExtensions(const std::vector<FileFilter>& filters)
{
    NSMutableArray<NSString*>* out = [NSMutableArray array];
    for (const auto& f : filters) {
        for (const auto& ext : f.extensions) {
            NSString* ns = [NSString stringWithUTF8String:ext.c_str()];
            if (ns && ns.length > 0) [out addObject:ns];
        }
    }
    return out.count > 0 ? out : nil;
}

// macOS 11+ prefers UTType over the deprecated allowedFileTypes setter.
// Build content types from the same extension list when the symbol is
// available; gracefully fall back to allowedFileTypes on older systems.
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 110000
API_AVAILABLE(macos(11.0))
NSArray<UTType*>* flattenUTTypes(NSArray<NSString*>* extensions)
{
    if (!extensions) return nil;
    NSMutableArray<UTType*>* out = [NSMutableArray array];
    for (NSString* ext in extensions) {
        UTType* t = [UTType typeWithFilenameExtension:ext];
        if (t) [out addObject:t];
    }
    return out.count > 0 ? out : nil;
}
#endif

NSWindow* parentWindow(GLFWwindow* w)
{
    return w ? glfwGetCocoaWindow(w) : nil;
}

void applyDefaultDir(NSSavePanel* panel, const std::string& dir)
{
    if (dir.empty()) return;
    NSString* nsDir = [NSString stringWithUTF8String:dir.c_str()];
    if (!nsDir) return;
    NSURL* url = [NSURL fileURLWithPath:nsDir isDirectory:YES];
    if (url) [panel setDirectoryURL:url];
}

void applyTitle(NSSavePanel* panel, const std::string& title)
{
    if (title.empty()) return;
    NSString* ns = [NSString stringWithUTF8String:title.c_str()];
    if (!ns) return;
    // .title is deprecated for NSOpenPanel on 11+ in favour of .message,
    // which is shown in the body of the sheet. Keep both for back-compat.
    panel.message = ns;
    if ([panel respondsToSelector:@selector(setTitle:)])
        panel.title = ns;
}

void applyFilters(NSSavePanel* panel,
                  const std::vector<FileFilter>& filters)
{
    NSArray<NSString*>* exts = flattenExtensions(filters);
    if (!exts) return;
#if defined(__MAC_OS_X_VERSION_MAX_ALLOWED) && __MAC_OS_X_VERSION_MAX_ALLOWED >= 110000
    if (@available(macOS 11.0, *)) {
        NSArray<UTType*>* utts = flattenUTTypes(exts);
        if (utts) {
            panel.allowedContentTypes = utts;
            return;
        }
    }
#endif
    // Pre-11 fallback. allowedFileTypes is still honoured on every release
    // POM1 supports, it just throws a deprecation warning on newer SDKs
    // (silenced repo-wide via -Wno-deprecated-declarations in CMake).
    panel.allowedFileTypes = exts;
}

std::string pathFromURL(NSURL* url)
{
    if (!url || !url.path) return std::string();
    const char* p = url.path.fileSystemRepresentation;
    return p ? std::string(p) : std::string();
}

NSInteger runPanelModal(NSSavePanel* panel, GLFWwindow* parent)
{
    NSWindow* host = parentWindow(parent);
    // beginSheetModalForWindow + waitUntilExitsRunLoop is what NFD does, but
    // it gets fiddly with GLFW's runloop integration. -runModal blocks the
    // calling thread until the user dismisses the panel — safer here because
    // POM1's render loop is single-threaded and is already paused on this
    // call. The (unused) `parent` arg is kept so the API stays symmetric
    // with the other backends.
    (void)host;
    return [panel runModal];
}

} // namespace

bool NativeFileDialog::isAvailable() { return true; }

bool NativeFileDialog::openFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        panel.resolvesAliases = YES;
        applyTitle(panel, title);
        applyDefaultDir(panel, defaultDir);
        applyFilters(panel, filters);

        if (runPanelModal(panel, parent) != NSModalResponseOK) return false;
        NSArray<NSURL*>* urls = panel.URLs;
        if (urls.count == 0) return false;
        outPath = pathFromURL(urls.firstObject);
        return !outPath.empty();
    }
}

bool NativeFileDialog::saveFile(GLFWwindow* parent,
                                const std::string& title,
                                const std::string& defaultDir,
                                const std::string& defaultName,
                                const std::vector<FileFilter>& filters,
                                std::string& outPath)
{
    @autoreleasepool {
        NSSavePanel* panel = [NSSavePanel savePanel];
        applyTitle(panel, title);
        applyDefaultDir(panel, defaultDir);
        applyFilters(panel, filters);
        if (!defaultName.empty()) {
            NSString* ns = [NSString stringWithUTF8String:defaultName.c_str()];
            if (ns) panel.nameFieldStringValue = ns;
        }
        // Mirror the Win32 OFN_OVERWRITEPROMPT default. NSSavePanel does this
        // out of the box, but be explicit about extension visibility so the
        // user always sees what's actually being written.
        panel.canCreateDirectories = YES;
        panel.showsTagField = NO;

        if (runPanelModal(panel, parent) != NSModalResponseOK) return false;
        outPath = pathFromURL(panel.URL);
        return !outPath.empty();
    }
}

} // namespace pom1

#endif // !POM1_IS_WASM && __APPLE__
