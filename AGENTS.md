# Repository Guidelines

## Project Structure & Module Organization
Core source files live at the repository root. `main.cpp` contains the sample render loop, `GlfwGeneral.hpp` handles window and surface setup, `vkBase.h` and `vkBase+.h` wrap Vulkan objects and pipeline state, and `easyVk.hpp` provides higher-level helpers such as the screen render pass/framebuffer setup. Shader sources and compiled SPIR-V files live in [`shaders/`](C:\Users\13240\Desktop\AIgraphics\easy_vulkan\shaders). Ignore `_3rd_party/`, `bin/`, `easy_vulkan_x64/`, and `.vs/` when making feature changes; they are third-party, generated, or IDE output.

## Build, Test, and Development Commands
Use Visual Studio 2022 or newer with the Vulkan SDK installed.

- `devenv easy_vulkan.sln` opens the solution in Visual Studio.
- `msbuild easy_vulkan.sln /p:Configuration=Debug /p:Platform=x64` builds the debug target.
- `msbuild easy_vulkan.sln /p:Configuration=Release /p:Platform=x64` builds the release target.
- Run from Visual Studio with `F5` to validate rendering and shader loading.

There is no automated test suite yet; a successful build and on-screen render are the current validation path.

## Coding Style & Naming Conventions
Follow the existing style in the root sources: tabs or 4-space indentation are both present, so avoid reformatting untouched code. Preserve the current naming patterns: Vulkan wrapper types use lowercase class names such as `pipeline` and `shaderModule`, helper namespaces use `easyVulkan`, and setup functions use PascalCase-like verbs such as `CreateSwapchain` or `InitializeWindow`. Keep comments short and explanatory, especially around synchronization, swapchain recreation, and resource lifetime.

## Testing Guidelines
For now, test by building `Debug|x64` and running the sample. Changes that affect rendering should still produce a visible frame without validation-layer errors. When adding features, prefer small sample-driven verification: e.g. a cube render, resize handling, or shader recompilation. If you add tests later, place them in a dedicated `tests/` directory instead of mixing them into the root.

## Commit & Pull Request Guidelines
Recent history uses short, imperative commit messages such as `update README`, `add pipeline and shader. first triangle!`, and `solve conflict`. Keep commits focused and descriptive, for example: `add vertex buffer helper` or `fix swapchain resize path`. Pull requests should summarize rendering changes, note required SDK/toolchain versions, link related issues, and include screenshots or short clips for visual changes.
