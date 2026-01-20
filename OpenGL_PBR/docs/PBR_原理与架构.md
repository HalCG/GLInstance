# PBR 物理渲染原理与架构设计

本文档深入解析 `OpenGL_PBR` 项目所涉及的基于物理的渲染 (Physically Based Rendering, PBR) 数学理论、图像环境光照 (Image-Based Lighting, IBL) 近似算法以及 OpenGL 状态机与架构设计。

---

## 一、PBR 光照物理理论与 Cook-Torrance 模型

PBR 基于辐射度学 (Radiometry) 与微表面理论 (Microfacet Theory)，要求材质渲染严格遵守 **能量守恒定律**。

### 1. 渲染方程 (Reflectance Equation)

对于给定的表面点 $p$ 和观察方向 $v$，入射光 Radiance 积分方程表示为：

$$L_o(p, v) = L_e(p, v) + \int_{\Omega} f_r(p, v, l) \, L_i(p, l) \, (n \cdot l) \, d\omega_i$$

其中：
- $L_o$ 为出射辐射率 (Outgoing Radiance)；
- $L_e$ 为自发光 (Emissive)；
- $f_r(p, v, l)$ 为双向反射分布函数 (BRDF)；
- $L_i$ 为来自方向 $l$ 的入射光 Radiance；
- $(n \cdot l)$ 为 Lambert 夹角投影因子。

---

### 2. Cook-Torrance BRDF 模型

Cook-Torrance BRDF 将表面反射拆分为 **漫反射 (Diffuse)** 与 **镜面反射 (Specular)** 两部分：

$$f_r = k_d \, f_{\text{diffuse}} + k_s \, f_{\text{specular}}$$

出于能量守恒限制，$k_d + k_s \le 1.0$。对于纯金属，$k_d = 0$。

#### (1) 漫反射项 (Lambertian Diffuse)
$$f_{\text{diffuse}} = \frac{\text{albedo}}{\pi}$$

#### (2) 镜面反射项 (Cook-Torrance Specular BRDF)
$$f_{\text{specular}} = \frac{D(h) \, F(v, h) \, G(v, l, n)}{4 \, (n \cdot v) \, (n \cdot l) + 0.0001}$$

---

### 3. 三大核心函数推导

#### A. 法线分布函数 NDF：Trowbridge-Reitz GGX
评估微表面朝向与半角向量 $h = \frac{l + v}{\|l + v\|}$ 对齐的概率：

$$D(h) = \frac{\alpha^2}{\pi \left( (n \cdot h)^2 (\alpha^2 - 1) + 1 \right)^2}$$

其中 $\alpha = \text{roughness}^2$（迪士尼/Epic 推荐参数化方式，提升粗糙度感官线性度）。

#### B. 菲涅尔公式 Fresnel：Fresnel-Schlick 近似
计算光线在不同入射角下的反射能量比例：

$$F(v, h) = F_0 + (1 - F_0) (1 - (h \cdot v))^5$$

其中 $F_0$ 为垂直入射时的基础反射率。非金属绝缘体 $F_0 \approx 0.04$，金属性材质 $F_0$ 根据 Albedo 基础颜色直接混合：
$$\text{vec3 } F_0 = \text{mix(vec3(0.04), albedo, metallic)}$$

针对 IBL 间接光照，考虑高粗糙度微表面遮挡对菲涅尔效应的衰减，使用扩展公式：

$$F_{\text{roughness}}(n \cdot v) = F_0 + (\max(\text{vec3}(1 - \text{roughness}), F_0) - F_0)(1 - (n \cdot v))^5$$

#### C. 几何遮蔽函数 Geometry：Smith Schlick-GGX
评估微表面相互自遮挡 (Shadowing / Masking) 造成的能量损失：

$$G(v, l, n) = G_1(v) \cdot G_1(l)$$

$$G_1(v) = \frac{n \cdot v}{(n \cdot v)(1 - k) + k}$$

在直接光照下，$k = \frac{(\text{roughness} + 1)^2}{8}$；在 IBL 预计算中，$k = \frac{\text{roughness}^2}{2}$。

---

## 二、IBL 图像环境光照与 Split-Sum 近似

为了在实时渲染中计算来自无限远环境贴图的全方位光照，Epic Games 提出了 **Split-Sum 近似算法**，将复杂的环境光积分拆分为 **漫反射 IBL** 与 **镜面反射 IBL** 两个独立的预计算 Pass。

$$\int_{\Omega} f_r \, L_i \, (n \cdot l) \, d\omega_i \approx \underbrace{k_d \frac{\text{albedo}}{\pi} \int_{\Omega} L_i \, (n \cdot l) \, d\omega_i}_{\text{Diffuse IBL}} + \underbrace{\left( \int_{\Omega} L_i \, d\omega_i \right) \cdot \left( \int_{\Omega} f_r \, (n \cdot l) \, d\omega_i \right)}_{\text{Specular IBL (Split-Sum)}}$$

---

### 1. 漫反射 IBL：Irradiance Convolution
漫反射部分只依赖于表面法线 $N$。使用半球黎曼和积分，对 2D HDR 环境贴图进行低频卷积采样，导出 $32 \times 32$ 的 3D Irradiance Cubemap：

$$E(N) = \int_{0}^{2\pi} \int_{0}^{\pi/2} L_i(\theta, \phi) \, \cos(\theta) \, \sin(\theta) \, d\theta \, d\phi$$

---

### 2. 镜面反射 IBL：Split-Sum 拆分两项

#### 第一项：预过滤环境贴图 (Prefiltered Environment Map)
在假设视线 $V = N = R$ 的前提下，利用 GGX NDF 重要性采样 (Importance Sampling)，针对不同粗糙度 (Mipmap 0 ~ 4 级) 提取环境光的高光模糊：

$$\text{PrefilteredColor} = \frac{\sum L_i \, (n \cdot l)}{\sum (n \cdot l)}$$

#### 第二项：2D BRDF 积分查找表 (BRDF 2D LUT)
将 Specular BRDF 中的菲涅尔项拆离：
$$\int_{\Omega} f_r \, (n \cdot l) \, d\omega_i \approx F_0 \cdot A + B$$

其中：
- $A = \int_{\Omega} \frac{f_r}{F_0} (1 - (1 - v \cdot h)^5) \, (n \cdot l) \, d\omega_i$
- $B = \int_{\Omega} \frac{f_r}{F_0} (1 - v \cdot h)^5 \, (n \cdot l) \, d\omega_i$

查找表以 $N \cdot V$ 为横坐标 (0~1)，$\text{roughness}$ 为纵坐标 (0~1)，离屏渲染保存为 `GL_RG16F` 浮点纹理。

---

## 三、OpenGL 关键状态管理与 Pass 流转

### 1. IBL 预计算 Pass 状态管理
1. **FBO 捕获设置**：使用 6 个方向的 LookAt 矩阵与 90 度 FOV 透视投影矩阵，绑定 `GL_COLOR_ATTACHMENT0` 到 Cubemap 对应面。
2. **Viewport 动态调整**：根据预计算贴图的分辨率按 Pass 切换 Viewport：
   - Equirect2Cubemap: `512x512`
   - Irradiance Map: `32x32`
   - Prefilter Mipmaps: `128x128` (Mip0) -> `64x64` (Mip1) -> `32x32` (Mip2) -> `16x16` (Mip3) -> `8x8` (Mip4)
   - BRDF LUT: `512x512`
3. **纹理格式**：离屏颜色附着全部采用 `GL_RGB16F` 或 `GL_RG16F` 浮点格式，防止高光超采数值截断。

### 2. 主场景与天空盒深度 Pass
- **HDR FBO**：主渲染循环 Pass 1 将场景绘制到 `hdrFbo_` (`GL_RGBA16F`) 中。
- **Skybox Depth Trick**：
  - 顶点着色器输出 `gl_Position = pos.xyww;`，透视除法后 $z/w = 1.0$，位于远裁剪面。
  - 渲染天空盒前设置 `glDepthMask(GL_FALSE)` 与 `glDepthFunc(GL_LEQUAL)`，利用 Early-Z 仅更新背景像素。
  - 渲染后恢复 `glDepthMask(GL_TRUE)` 与 `glDepthFunc(GL_LESS)`。

### 3. 色调映射 (Tone Mapping) Pass
将 HDR 浮点颜色转换为显示器 LDR 颜色：
- **Exposure Tone Mapping**：`mapped = 1.0 - exp(-hdrColor * exposure)`
- **Gamma Correction**：`mapped = pow(mapped, 1.0 / gamma)` (默认 2.2)
