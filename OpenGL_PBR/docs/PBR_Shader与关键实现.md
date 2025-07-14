# PBR Shader 与关键代码实现导读

本文档针对 `OpenGL_PBR` 项目中的 16 个 GLSL Shader、C++ 渲染核心类以及交互控制状态流转提供逐模块技术导读。

---

## 一、GLSL Shader 详细导读

### 1. `pbr_mesh.vert` / `pbr_mesh.frag` (主 PBR 模型渲染着色器)

#### (1) 顶点着色器 (`pbr_mesh.vert`)
- **顶点属性输入**：
  - `location 0`: `position` 局部顶点位置 (vec4)
  - `location 1`: `texcoord` 纹理坐标 (vec4)
  - `location 2`: `normal` 局部法线向量 (vec4)
  - `location 3`: `tangent` 局部正交切线向量 (vec4)
- **逆转置矩阵 `ITM`**：使用 `mat3(ITM) * normal.xyz` 变换法线，防止模型进行非等比缩放 (Non-uniform scaling) 时法线偏转失真。
- **正交 TBN 矩阵构建**：
  ```glsl
  vec3 t = normalize(mat3(M) * tangent.xyz);
  vec3 b = normalize(cross(V_Normal, t));
  V_TBN = mat3(t, b, V_Normal);
  ```
  在 Fragment Shader 中通过采样 2D 切线空间法线贴图后，乘以 `V_TBN` 恢复出世界空间精确法线。

#### (2) 片段着色器 (`pbr_mesh.frag`)
- **采样 PBR 材质贴图并转为线性空间**：
  ```glsl
  vec3 albedo = pow(texture(U_Albedo, V_Texcoord.xy).rgb, vec3(2.2));
  float roughness = max(texture(U_Roughness, V_Texcoord.xy).r, 0.01);
  float metallic = texture(U_Metallic, V_Texcoord.xy).r;
  float ao = texture(U_AO, V_Texcoord.xy).r;
  vec3 emissive = texture(U_Emissive, V_Texcoord.xy).rgb;
  ```
- **直接光照与 IBL 结合**：
  ```glsl
  // 1. 直接光照 Cook-Torrance BRDF
  vec3 directColor = (diffuse + specular) * vec3(1.0) * 2.0 * NdotL;

  // 2. IBL 漫反射 (Irradiance Cubemap 采样)
  vec3 irradiance = texture(U_DiffuseIrradiance, N).rgb;
  vec3 diffuseIBL = kD_ibl * irradiance * albedo * iblDiffuseIntensity;

  // 3. IBL 镜面反射 (Prefiltered Env Cubemap + BRDF 2D LUT 采样)
  vec3 prefilteredColor = textureLod(U_PrefilteredColor, R, roughness * MAX_REFLECTION_LOD).rgb;
  vec2 brdf = texture(U_BRDFLUT, vec2(NdotV, roughness)).rg;
  vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y) * iblSpecularIntensity;

  // 4. 颜色合成
  vec3 finalColor = directColor + (diffuseIBL + specularIBL) * ao + emissive;
  ```

---

### 2. `brdf.frag` (准蒙特卡洛 BRDF 2D LUT 积分)

利用 **Hammersley 低差异点集** 与 **Van Der Corput 位逆转算法** 进行高效重要性采样：

```glsl
// 二进制按位倒转实现 Van Der Corput 伪随机序列
float RadicalInverse_VDC(uint bits) 
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), RadicalInverse_VDC(i));
}
```

针对坐标范围 $(N \cdot V, \text{roughness}) \in [0, 1]^2$，积分 1024 个样本输出 `A` (Scale 缩放因子) 与 `B` (Bias 偏移因子)。

---

### 3. `equirectangular_to_cubemap.frag` (球面全景投影转换)

将 2D 全景图笛卡尔坐标转换到 3D 向量 `v` 的球面上：

```glsl
vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    uv *= vec2(0.1591, 0.3183); // 映射到 [-0.5, 0.5]
    uv += 0.5;                  // 映射到 [0, 1]
    return uv;
}
```

---

### 4. `skybox.vert` (深度优化 Trick)

```glsl
vec4 pos = projection * mat4(mat3(view)) * vec4(position.xyz, 1.0);
gl_Position = pos.xyww; // 透视除法后 z/w = w/w = 1.0 (最远深度)
```

配合 `glDepthFunc(GL_LEQUAL)`，仅在前面没有遮挡物体的背景网格像素上绘制天空盒，大幅节省像素着色器指令。

---

## 二、C++ 架构核心类说明

### 1. `IblPrecomputation` (IBL 预计算执行管线)
- **`loadHdrTexture(path)`**：使用 `stbi_loadf` 读取 32 位浮点 `.hdr` 数据，创建 `GL_RGB16F` 2D 纹理。
- **`convertEquirectangularToCubemap(...)`**：分配 `512x512` 3D Cubemap，离屏绑定 FBO 渲染 6 个方向（`GL_TEXTURE_CUBE_MAP_POSITIVE_X + i`），最后调用 `glGenerateMipmap` 生成 Mip 链。
- **`computeIrradianceCubemap(...)`**：分配 `32x32` 3D Cubemap，离屏半球卷积积分。
- **`computePrefilterCubemap(...)`**：分配 `128x128` 含 5 级 Mipmap 的 Cubemap，按 Mip 级别从 0 到 4 循环渲染（对应 $\text{roughness} = 0.0 \sim 1.0$），每次更新 Viewport 分辨率。
- **`computeBrdfLutTexture(...)`**：分配 `512x512` `GL_RG16F` 2D 纹理，离屏渲染全屏 Quad 输出 2D LUT。

---

### 2. `PbrApp` (应用程序主控制类)
- **纹理 Unit 分配标准 (Slot Allocation)**：
  - `GL_TEXTURE0`: Prefiltered Environment Cubemap
  - `GL_TEXTURE1`: Irradiance Environment Cubemap
  - `GL_TEXTURE2`: BRDF 2D LUT Texture
  - `GL_TEXTURE3`: Albedo 材质贴图
  - `GL_TEXTURE4`: Normal 材质贴图
  - `GL_TEXTURE5`: Emissive 材质贴图
  - `GL_TEXTURE6`: AO 材质贴图
  - `GL_TEXTURE7`: Metallic 材质贴图
  - `GL_TEXTURE8`: Roughness 材质贴图

- **3 Pass 渲染流程 (`PbrApp::run`)**：
  ```cpp
  // Pass 1: 将主场景渲染至离屏 HDR FBO
  hdrFbo_.bind();
  renderScene();
  hdrFbo_.unbind();

  // Pass 2: 色调映射 Tone Mapping 到主屏幕屏幕
  toneMappingShader_->use();
  glBindTexture(GL_TEXTURE_2D, hdrFbo_.getColorTexture());
  IblPrecomputation::renderQuad();

  // Pass 3: 绘制 ImGui 控件
  renderImGui();
  ```

---

### 3. `Camera` (3D 轨道交互相机)
基于球坐标系推导世界空间坐标：
$$\text{Position} = \text{Target} + \begin{pmatrix} \text{Distance} \cdot \cos(\text{Pitch}) \cdot \sin(\text{Yaw}) \\ \text{Distance} \cdot \sin(\text{Pitch}) \\ \text{Distance} \cdot \cos(\text{Pitch}) \cdot \cos(\text{Yaw}) \end{pmatrix}$$

限制 $\text{Pitch} \in [-89^{\circ}, 89^{\circ}]$，避免万向节锁 (Gimbal Lock) 造成视角颠倒。
