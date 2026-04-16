# easy_vulkan

会是基于Vulkan开发的一个小渲染器

慢慢扩展和开发ing

---

## 📌 TODO

- [x] Vulkan 基础框架，渲染最初的三角形 - 参考 [EasyVulkan](https://easyvulkan.github.io)
- [x] 加入DeviceMemory、Buffer、DescriptorSet，渲染基础立方体
- [x] Camera 系统
- [x] 纹理、光照接入
- [x] .obj 模型加载器
- [x] 多物体渲染
- [x] shadow map
- [x] deferred / G-buffer
- [x] postprocess pass
- [ ] compute shader pass
- [ ] debug 模块
- [ ] 接入 compute-driven 小功能
- [ ] 光追管线接入
- [ ] RDG
- [ ] ...

---

## Requirements

- Visual Studio 2022 / 2026（勾选 C++ 开发）
- Vulkan SDK 1.4.341.1 —— https://vulkan.lunarg.com/sdk/home

---

## Setup / Build

### 1️⃣ Clone项目

```bash
git clone https://github.com/fl0a1e/easy_vulkan.git
cd easy_vulkan
```

### 2️⃣ 打开项目

用 VS 打开 `.sln` 文件，运行即可

### 3️⃣ 编译运行

- 选择 `Debug` / `Release`
- 点击运行（F5）

---

## Notes

- 如果运行报错，请检查是否正确安装 Vulkan SDK 

---

## 🖥️ Tested Environment

本项目已在以下显卡环境测试通过：

- NVIDIA GeForce GTX 1650
- NVIDIA GeForce RTX 4070

---

## shader扩展

shader使用HLSL，使用dxc编译成SPIR-V

---

## License

MIT
