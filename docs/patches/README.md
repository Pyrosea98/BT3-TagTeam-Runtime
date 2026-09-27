# Submodule patches for the native renderer (native-seam branch)

The seamvk renderer needs two small changes inside the paraLLEl-GS submodule tree that are not upstream:

- `parallel-gs-seamvk.patch` -- paraLLEl-GS (`ps2xRuntime/third_party/parallel-gs`): frame-boundary flush marking in
  `gs/gs_interface.cpp`, `gs/gs_renderer.cpp`, `gs/gs_renderer.hpp`.
- `granite-seamvk.patch` -- Granite (`ps2xRuntime/third_party/parallel-gs/Granite`): enables `VK_EXT_shader_stencil_export`
  in `vulkan/context.cpp` and a device-side change in `vulkan/device.cpp`.

Apply after `git submodule update --init --recursive`, from the repository root:

    git -C ps2xRuntime/third_party/parallel-gs apply ../../../docs/patches/parallel-gs-seamvk.patch
    git -C ps2xRuntime/third_party/parallel-gs/Granite apply ../../../../docs/patches/granite-seamvk.patch

Check with `git -C ps2xRuntime/third_party/parallel-gs diff --stat` and the same for `Granite`. Regenerate the patches
from a tree that has them applied with `git diff -- gs/` and `git diff -- vulkan/` respectively.
