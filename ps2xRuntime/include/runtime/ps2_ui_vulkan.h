#pragma once
#if defined(PS2X_HAVE_PGS)
#include "device.hpp"
namespace ps2x::ui {
// Called under the PGS device mutex on its registered GS thread.
Vulkan::ImageHandle vulkanComposite(Vulkan::Device&, const Vulkan::ImageHandle&);
void vulkanUiShutdown();
void vulkanUiFrameStamp(uint64_t&,uint64_t&);
}
#endif
int ps2xNativeUiVulkanSelfTest(const char* assets, const char* output);
