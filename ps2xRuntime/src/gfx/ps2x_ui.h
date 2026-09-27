#pragma once

// Overlay UI entry points. These replace the rlImGui* calls in the overlay/debug-panel
// code so the same UI can be drawn through either backend:
//   * rlImGui (OpenGL/rlgl)  -- the default host path
//   * imgui_impl_dx11        -- when native D3D11 video is active (PS2X_D3D11=1)
//
// ps2x_ui.cpp decides per call based on gfx::NativeVideo(). It is the only UI TU that
// pulls in raylib.h; the D3D11 side lives in ui_d3d11.cpp (imgui_impl_dx11.h needs
// windows.h, which cannot share a translation unit with raylib.h).

namespace ps2x::gfx
{
    void UiSetup();
    void UiBegin();
    void UiEnd();
    void UiShutdown();

    // [d3d11 uitest] Draws a tiny debug window (UiBegin + a window + UiEnd). Proves the
    // imgui_impl_dx11 path renders without involving the real overlay. Used by PS2X_UI_TEST=1.
    void UiDrawTestWindow();

    // [netplay] Upload tightly packed RGBA8 (top-down, as GsDecodeImageRGBA8 returns it) and
    // return the ImGui texture id. Kept next to UiBegin/UiEnd because the texture is only valid
    // for the backend currently drawing, so it has to be created through the same path that will
    // sample it. Returns nullptr on failure and never throws.
    unsigned long long UiLoadTextureRgba(const void *rgba, int w, int h);
}
