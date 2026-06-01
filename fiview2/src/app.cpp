// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2025-2026 Jörg Simbrig
#include "app.hpp"
#include "serialization.hpp"
#include "panels/freq_plot.hpp"
#include "panels/pz_plot.hpp"
#include "panels/param_panel.hpp"
#include "panels/cascade_panel.hpp"
#include "panels/export_panel.hpp"
#include "panels/impulse_panel.hpp"
#include "panels/compare_view.hpp"
#include "panels/stability_panel.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>

#include <cstdio>
#include <stdexcept>

namespace fiview2 {

// ── GLFW error callback ───────────────────────────────────────────────────────

static void glfw_error(int err, const char* desc)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", err, desc);
}

// ── App ───────────────────────────────────────────────────────────────────────

App::App()
    : audio_(AudioBackend::create())
{}

App::~App()
{
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
    }
}

bool App::init(const char* title)
{
    glfwSetErrorCallback(glfw_error);
    if (!glfwInit()) return false;

    // Try progressively less demanding OpenGL contexts for maximum compatibility.
    // Core 3.3 is preferred; compat 3.3 and 3.0 are fallbacks for broken drivers.
    struct GLCtx { int major, minor, profile; };
    static const GLCtx k_ctx[] = {
        {3, 3, GLFW_OPENGL_CORE_PROFILE},
        {3, 3, GLFW_OPENGL_COMPAT_PROFILE},
        {3, 0, GLFW_OPENGL_ANY_PROFILE},
    };
    for (const auto& ctx : k_ctx) {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, ctx.major);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, ctx.minor);
        if (ctx.profile != GLFW_OPENGL_ANY_PROFILE)
            glfwWindowHint(GLFW_OPENGL_PROFILE, ctx.profile);
#ifdef __APPLE__
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
        window_ = glfwCreateWindow(1440, 900, title, nullptr, nullptr);
        if (window_) break;
        std::fprintf(stderr, "fiview2: OpenGL %d.%d profile=%d failed, trying next…\n",
                     ctx.major, ctx.minor, ctx.profile);
    }
    if (!window_) { glfwTerminate(); return false; }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
#ifdef IMGUI_ENABLE_DOCKING
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
#endif

    // Style: dark with accent colours
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 4.0f;
    style.FrameRounding     = 3.0f;
    style.GrabRounding      = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.Colors[ImGuiCol_WindowBg]     = {0.08f, 0.08f, 0.10f, 1.0f};
    style.Colors[ImGuiCol_TitleBgActive]= {0.12f, 0.24f, 0.48f, 1.0f};
    style.Colors[ImGuiCol_FrameBg]      = {0.14f, 0.14f, 0.18f, 1.0f};
    style.Colors[ImGuiCol_Button]       = {0.18f, 0.30f, 0.52f, 0.8f};

    // HiDPI
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(window_, &xscale, &yscale);
    if (xscale > 1.0f) {
        io.FontGlobalScale = xscale;
        style.ScaleAllSizes(xscale);
    }

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    // Use the GLSL version matching the actual context obtained
    int gl_major = 0, gl_minor = 0;
    glfwGetWindowAttrib(window_, GLFW_CONTEXT_VERSION_MAJOR);  // query
    gl_major = glfwGetWindowAttrib(window_, GLFW_CONTEXT_VERSION_MAJOR);
    gl_minor = glfwGetWindowAttrib(window_, GLFW_CONTEXT_VERSION_MINOR);
    const char* glsl_ver = "#version 130";
    if      (gl_major > 3 || (gl_major == 3 && gl_minor >= 3)) glsl_ver = "#version 330";
    else if (gl_major == 3 && gl_minor >= 1)                    glsl_ver = "#version 140";
    else if (gl_major == 3 && gl_minor >= 0)                    glsl_ver = "#version 130";
    ImGui_ImplOpenGL3_Init(glsl_ver);

    cockpit_.emplace(
        imtile::hsplit_px(1044,
            // left 1044 px
            imtile::hsplit_px(234,
                // left column 234 px: Parameters + Cascade + A/B
                imtile::vsplit_px(402,
                    imtile::window("Parameters"),
                    imtile::vsplit_px(164,
                        imtile::window("Filter Cascade"),
                        imtile::window("A/B Comparison")
                    )
                ),
                // plot columns
                imtile::hsplit_px(387,
                    imtile::vsplit_px(424,
                        imtile::window("Frequency Response"),
                        imtile::window("Impulse / Step")
                    ),
                    imtile::vsplit_px(646,
                        imtile::window("Poles / Zeros"),
                        imtile::window("Stability")
                    )
                )
            ),
            // right 396 px: fidgen Export full height
            imtile::window("fidgen Export")
        ),
        imtile::Options{
            .ini_file      = "fiview2.ini",
            .show_controls = true,
        }
    );

    return true;
}

void App::run()
{
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        render_frame();
    }
}

void App::render_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    draw_menu_bar();
    cockpit_->begin_frame();

    // All panels
    panels::draw_param_panel(state_);
    panels::draw_freq_plot(state_);
    panels::draw_pz_plot(state_);
    panels::draw_cascade_panel(state_);
    panels::draw_export_panel(state_);
    panels::draw_impulse_panel(state_);
    panels::draw_compare_view(state_);
    panels::draw_stability_panel(state_);
    if (show_guided_) panels::draw_guided_mode(state_);

    ImGui::Render();

    int w, h;
    glfwGetFramebufferSize(window_, &w, &h);
    glViewport(0, 0, w, h);
    glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    glfwSwapBuffers(window_);
}

void App::draw_menu_bar()
{
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open…",  "Ctrl+O")) open_file_dialog(false);
        if (ImGui::MenuItem("Save…",  "Ctrl+S")) open_file_dialog(true);
        ImGui::Separator();
        if (ImGui::MenuItem("Quit",   "Alt+F4"))
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Guided Mode Wizard", nullptr, &show_guided_);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("About")) {
            // Simple modal
            ImGui::OpenPopup("About fiview2");
        }
        ImGui::EndMenu();
    }

    // About modal
    if (ImGui::BeginPopupModal("About fiview2", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("fiview2 v" FIVIEW2_VERSION);
        ImGui::TextDisabled("Filter Design Workbench");
        ImGui::Separator();
        ImGui::Text("Built with Dear ImGui, fidlib, fidgen");
        ImGui::Text("GPL-2.0-or-later");
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // spec in menu bar
    const auto& spec = state_.spec();
    if (!spec.empty()) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20.0f);
        ImGui::TextColored({0.4f,1.0f,0.4f,1.0f}, "%s", spec.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("@ %.0f Hz", state_.params().rate);
    }

    // layout widget right-aligned
    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 80.0f);
    cockpit_->draw_controls();

    ImGui::EndMainMenuBar();
}


void App::open_file_dialog(bool save)
{
    // Simple path input via modal (no native dialog dependency)
    static char path_buf[256] = "fiview2_state.json";
    ImGui::OpenPopup(save ? "Save State" : "Open State");

    ImGui::SetNextWindowSize({400, 100}, ImGuiCond_Always);
    if (ImGui::BeginPopupModal(save ? "Save State" : "Open State",
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##path", path_buf, sizeof(path_buf));
        if (ImGui::Button(save ? "Save" : "Open")) {
            bool ok = save ? save_state(state_, path_buf)
                           : load_state(state_, path_buf);
            if (ok) current_file_ = path_buf;
            else    ImGui::OpenPopup("IO Error");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

} // namespace fiview2
