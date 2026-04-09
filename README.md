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
- [ ] deferred / G-buffer
- [ ] postprocess pass
- [ ] compute shader pass
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

## Shadow Map 实现说明

本次提交实现了基础的 Shadow Map（阴影贴图）渲染，主要包含以下内容：

### 渲染流程（两个 Pass）

1. **Shadow Pass（光源视角深度渲染）**  
   - 使用平行光的正交投影矩阵（`lightViewProj`）将场景渲染到一张 2048×2048 的深度图中
   - 该 pass 仅输出深度，不输出颜色
   - 对应 shader：`shaders/shadow.vs.hlsl`

2. **Main Pass（正常渲染 + 阴影采样）**  
   - 顶点着色器（`triangle.vs.hlsl`）额外输出每个顶点在光源空间的位置（`ShadowPosition`）
   - 片元着色器（`triangle.ps.hlsl`）根据 `ShadowPosition` 采样 shadow map，对比当前深度与 shadow map 深度（带 bias），得到阴影系数，调制漫反射和高光结果

### 关键代码结构

| 文件 | 变更说明 |
|------|----------|
| `easyVk.hpp` | 新增 `shadowRenderPassResources` 结构体、`FindShadowDepthFormat()`、`CreateRpwf_Shadow()` |
| `shaders/shadow.vs.hlsl` | 新增，光源空间顶点变换，仅输出 `SV_Position` |
| `shaders/triangle.vs.hlsl` | 新增 `ShadowPosition` 输出（光源视角裁剪坐标） |
| `shaders/triangle.ps.hlsl` | 新增 `ComputeShadowFactor()`，采样 shadow map 并计算阴影遮蔽 |
| `main.cpp` | 新增 shadow pipeline、descriptor set、uniform buffer，以及两 pass 的渲染调度 |

---

## shader扩展

shader使用HLSL，使用dxc编译成SPIR-V

---

## License

MIT
