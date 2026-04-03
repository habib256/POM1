#define GL_SILENCE_DEPRECATION
#include <iostream>
#include <GLFW/glfw3.h>
#include "POM1Build.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "MainWindow_ImGui.h"
#include "IconsFontAwesome6.h"

#if POM1_IS_WASM
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

    // PRESS + REPEAT : le handler n’exécute les raccourcis sur REPEAT que pour F7 (step).
    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
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
#if POM1_IS_WASM
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
#if POM1_IS_WASM
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
#if POM1_IS_WASM
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
        // Efface le message Emscripten du type « Getting the system running » / spinner dès la 1ʳᵉ frame.
        if (static bool clearedWasmStatus = false; !clearedWasmStatus) {
            clearedWasmStatus = true;
            emscripten_run_script(
                "if(typeof Module!=='undefined'&&Module.setStatus){Module.setStatus('');}"
                "var sp=document.getElementById('spinner');if(sp)sp.style.display='none';"
                "var st=document.getElementById('status');if(st)st.textContent='Ready';");
        }
        glfwPollEvents();

        // Sync taille canvas (CSS) ↔ buffer WebGL ; garde-fous si get_element_css_size renvoie 0
        double cssW = 0.0;
        double cssH = 0.0;
        emscripten_get_element_css_size("#canvas", &cssW, &cssH);
        int cssWi = (int)cssW;
        int cssHi = (int)cssH;
        if (cssWi < 1) {
            cssWi = 1200;
        }
        if (cssHi < 1) {
            cssHi = 800;
        }
        int bufW = 0;
        int bufH = 0;
        emscripten_get_canvas_element_size("#canvas", &bufW, &bufH);
        if (bufW != cssWi || bufH != cssHi) {
            emscripten_set_canvas_element_size("#canvas", cssWi, cssHi);
            glfwSetWindowSize(c->window, cssWi, cssHi);
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();

        ImGuiIO& io = ImGui::GetIO();
        int fbW = 0;
        int fbH = 0;
        glfwGetFramebufferSize(c->window, &fbW, &fbH);
        if (fbW < 1 || fbH < 1) {
            fbW = cssWi;
            fbH = cssHi;
        }
        io.DisplaySize = ImVec2((float)fbW, (float)fbH);

        ImGui::NewFrame();

        c->mainWindow->render();

        ImGui::Render();
        glViewport(0, 0, fbW, fbH);
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