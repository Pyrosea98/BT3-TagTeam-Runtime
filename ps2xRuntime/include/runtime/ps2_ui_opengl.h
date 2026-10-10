#pragma once
namespace ps2x::ui {
// Called on the window thread after the guest frame, before swapping buffers.
void openglComposite(float x, float y, float width, float height);
void openglUiShutdown();
}
int ps2xNativeUiOpenGlSelfTest(const char* assets, const char* output);
