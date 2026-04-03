#define GL_SILENCE_DEPRECATION
#include <iostream>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "MainWindow_ImGui.h"
#include "IconsFontAwesome6.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

static void glfw_char_callback(GLFWwindow* window, unsigned int codepoint)
{
    ImGui_ImplGlfw_CharCallback(window, codepoint);

    auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(window));
    if (mw) {
        mw->handleGlfwChar(codepoint);
    }
}

static void glfw_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);

    if (action == GLFW_PRESS) {
        auto* mw = static_cast<MainWindow_ImGui*>(glfwGetWindowUserPointer(window));
        if (mw) {
            mw->handleGlfwKey(key, scancode, action, mods);
        }
    }
}

int main(int argc, char* argv[])
{
    std::cout << "POM1 v1.1 - Apple 1 Emulator (Dear ImGui)" << std::endl;
    
    // Setup GLFW
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return -1;

    // OpenGL / GLSL context hints
#ifdef __EMSCRIPTEN__
    // WebGL 2.0 = OpenGL ES 3.0 — GLSL ES 300
    const char* glsl_version = "#version 300 es";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
#else
    // GL 3.2 + GLSL 150 pour macOS / Linux / Windows
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
#endif

    // Create window
    GLFWwindow* window = glfwCreateWindow(1200, 800, "POM1 v1.1 - Apple 1 Emulator", NULL, NULL);
    if (window == NULL)
        return -1;

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // Enable vsync

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // NE PAS activer NavEnableKeyboard - le clavier est pour l'Apple 1, pas pour ImGui!
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Disponible seulement dans la branche docking

    // Charger les polices
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 15.0f;
    io.Fonts->AddFontDefault(&fontConfig);

    // Fusionner la police d'icônes FontAwesome
    ImFontConfig iconsConfig;
    iconsConfig.MergeMode = true;
    iconsConfig.PixelSnapH = true;
    iconsConfig.GlyphMinAdvanceX = 15.0f;
    static const ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
#ifdef __EMSCRIPTEN__
    const char* fontPath = "fonts/fa-solid-900.ttf";
#else
    const char* fontPath = "../fonts/fa-solid-900.ttf";
#endif
    if (!io.Fonts->AddFontFromFileTTF(fontPath, 14.0f, &iconsConfig, iconsRanges)) {
        fprintf(stderr, "Warning: Could not load icon font '%s' — toolbar icons will be missing\n", fontPath);
    }

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Create main application
    MainWindow_ImGui mainWindow;
    mainWindow.setWindow(window);
    glfwSetWindowUserPointer(window, &mainWindow);

    // Installer nos callbacks GLFW APRÈS ImGui pour les chaîner
    glfwSetCharCallback(window, glfw_char_callback);
    glfwSetKeyCallback(window, glfw_key_callback);

    // Main loop
#ifdef __EMSCRIPTEN__
    // Emscripten: browser controls the loop — pass a callback
    struct LoopContext {
        GLFWwindow* window;
        MainWindow_ImGui* mainWindow;
    };
    static LoopContext ctx;
    ctx.window = window;
    ctx.mainWindow = &mainWindow;

    emscripten_set_main_loop_arg([](void* arg) {
        LoopContext* c = static_cast<LoopContext*>(arg);
        glfwPollEvents();

        // Sync canvas drawing buffer with CSS size (fixes fullscreen/resize)
        double cssW, cssH;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        int bufW, bufH;
        emscripten_get_canvas_element_size("#canvas", &bufW, &bufH);
        if (bufW != (int)cssW || bufH != (int)cssH) {
            emscripten_set_canvas_element_size("#canvas", (int)cssW, (int)cssH);
            // Tell GLFW about the new size so mouse coords stay correct
            glfwSetWindowSize(c->window, (int)cssW, (int)cssH);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        // Override ImGui display size with actual canvas dimensions
        // (GLFW's cached window size may be stale after fullscreen toggle)
        ImGuiIO& io = ImGui::GetIO();
        io.DisplaySize = ImVec2((float)cssW, (float)cssH);

        ImGui::NewFrame();

        c->mainWindow->render();

        ImGui::Render();
        // Use actual canvas size for viewport (not glfwGetFramebufferSize which may be stale)
        glViewport(0, 0, (int)cssW, (int)cssH);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(c->window);
    }, &ctx, 0, true);
    // emscripten_set_main_loop_arg never returns when simulate_infinite_loop=true
#else
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Render application
        mainWindow.render();

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
#endif

    return 0;
} 