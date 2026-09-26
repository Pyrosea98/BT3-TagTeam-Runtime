// [fe-probe] Headless check on the file picker's ImGui widget lifecycle.
//
// The picker is the only front-end widget that opens a MODAL popup, and it is driven by a
// lifecycle that the rest of the UI does not have: open() arms a deferred OpenPopup, the modal
// is opened and ended by draw() on a later frame, and the user can close it (Cancel), accept a
// file (double-click) or have the caller close it from the outside at any point. Every one of
// those leaves ImGui's popup bookkeeping in a different state, and a stray End() there does not
// fail loudly: it desyncs the window stack and the next Begin/End pair in the app lands on the
// implicit fallback window, which is how it reaches the user as
//
//   In window 'Debug##Default': Calling End() too many times!
//
// 'Debug##Default' is not a window this project creates -- it is the implicit window NewFrame()
// opens (imgui.cpp). Seeing it named in the assert means the stack was already empty when our
// code ended something, so the bug is upstream of the report.
//
// Null backend: no window, no GL. The mouse is injected by hand, so the probe needs no input.
#include <cstdio>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"   // CurrentWindowStack, BeginPopupStack, ErrorCountCurrentFrame
#include "imgui_impl_null.h"

#include "frontend/fe_picker.h"

namespace
{
int g_fail = 0;
int g_errors = 0;
int g_stackBase = -1;

void check(bool ok, const char *what)
{
    std::printf("  %s %s\n", ok ? "ok  " : "FALLA", what);
    if (!ok)
        ++g_fail;
}

ImGuiContext *ctx() { return ImGui::GetCurrentContext(); }

// One frame shaped like fe_app.cpp's host window: the picker is drawn from inside the host,
// after a sibling child has already been opened and closed.
void frame(frontend::FilePicker &picker, bool hoverSweep)
{
    ImGui_ImplNullPlatform_NewFrame();
    ImGui_ImplNullRender_NewFrame();

    // A slow horizontal sweep with the left button held, so the run walks the places bar, the
    // path row, the entry list and the action buttons without needing real coordinates.
    static int sweep = 0;
    ImGuiIO &io = ImGui::GetIO();
    if (hoverSweep)
    {
        io.MousePos = ImVec2(40.0f + float(sweep % 900), 120.0f + float((sweep / 900) % 480));
        io.MouseDown[0] = (sweep % 3) != 0;
        ++sweep;
    }
    else
    {
        io.MousePos = ImVec2(-1.0f, -1.0f);
        io.MouseDown[0] = false;
    }

    ImGui::NewFrame();
    ImGui::Begin("##fe_host");
    ImGui::BeginChild("##fe_side", ImVec2(190.0f, 400.0f), ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("side");
    ImGui::EndChild();
    if (picker.draw())
        std::printf("       (picked %s)\n", picker.result().c_str());
    ImGui::End();
    ImGui::Render();
    ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    g_errors += ctx()->ErrorCountCurrentFrame;
}

// The invariants, checked after every single frame so the report names the frame that broke.
bool invariantsHeld(const char *stage)
{
    const int stack = ctx()->CurrentWindowStack.Size;
    const int popups = ctx()->BeginPopupStack.Size;
    if (g_stackBase < 0)
        g_stackBase = stack;
    if (stack != g_stackBase || popups != 0 || ctx()->ErrorCountCurrentFrame != 0)
    {
        std::printf("       %s: frame broke it -> windowStack=%d (base %d)  popupStack=%d  errors=%d\n",
                    stage, stack, g_stackBase, popups, ctx()->ErrorCountCurrentFrame);
        return false;
    }
    return true;
}

const char *kHome = nullptr;
}   // namespace

int main()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2(1280.0f, 720.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;
    // Same posture as the launcher (see fe_window.cpp): report misuse instead of swallowing it.
    io.ConfigErrorRecoveryEnableTooltip = true;
    io.ConfigErrorRecoveryEnableAssert = true;
    io.ConfigErrorRecoveryEnableDebugLog = false;   // no TTY here; we count errors ourselves

    ImGui_ImplNull_Init();
    ImGui_ImplNullRender_Init();

    frontend::FilePicker picker;
    kHome = std::getenv("HOME");

    std::printf("[1] 60 frames with the modal open and closed again (Cancel path)\n");
    picker.open("Select your game dump", kHome ? kHome : "/tmp", {".iso", ".img", ".7z"});
    for (int i = 0; i < 60; ++i)
    {
        const bool sweep = (i % 2) == 0;
        frame(picker, sweep);
        if (!invariantsHeld("open"))
            break;
    }
    check(ctx()->ErrorCountCurrentFrame == 0 || g_errors == 0, "no ImGui error while open");
    picker.close();
    for (int i = 0; i < 30; ++i)
    {
        frame(picker, false);
        if (!invariantsHeld("after close"))
            break;
    }

    std::printf("[2] reopened immediately after a close, twice in a row\n");
    for (int round = 0; round < 3; ++round)
    {
        picker.open("Select your game dump", kHome ? kHome : "/tmp", {".iso"});
        for (int i = 0; i < 20; ++i)
        {
            frame(picker, false);
            if (!invariantsHeld("reopen"))
                break;
        }
        picker.close();
        for (int i = 0; i < 10; ++i)
        {
            frame(picker, false);
            if (!invariantsHeld("reopen/closed"))
                break;
        }
    }

    std::printf("[3] closed by the caller on the same frame it was opened\n");
    picker.open("Select your game dump", kHome ? kHome : "/tmp", {".iso"});
    picker.close();
    for (int i = 0; i < 20; ++i)
    {
        frame(picker, false);
        if (!invariantsHeld("open+close same frame"))
            break;
    }

    std::printf("[4] 400 frames of mouse sweeping with the modal open\n");
    picker.open("Select your game dump", kHome ? kHome : "/tmp", {".iso", ".7z", ".zip"});
    for (int i = 0; i < 400; ++i)
    {
        frame(picker, true);
        if (!invariantsHeld("sweep"))
            break;
    }

    check(ctx()->BeginPopupStack.Size == 0, "the popup stack ends up empty");
    check(ctx()->CurrentWindowStack.Size == g_stackBase, "the window stack did not grow");
    check(g_errors == 0, "ErrorCountCurrentFrame stayed at 0 the whole time");
    if (g_errors)
        std::printf("       errores=%d\n", g_errors);

    ImGui_ImplNullRender_Shutdown();
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();

    std::printf("%s (%d fallos)\n", g_fail ? "PROBE FAILED" : "PROBE OK", g_fail);
    return g_fail ? 1 : 0;
}
