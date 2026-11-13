# Blend：全屏脚手架着色器 + 硬件混合

> 源码：[`depth_peeling_blend.vert`](../resources/depth_peeling_blend.vert) / [`depth_peeling_blend.frag`](../resources/depth_peeling_blend.frag)  
> 调用：[`DepthPeelingApp::blendPeelLayerToAccum`](../src/DepthPeelingApp.cpp)  
> 公式详解：[FrontToBack混合_src_dst与公式详解.md](./FrontToBack混合_src_dst与公式详解.md)

---

## 1. 和 Peel 怎么区分

| | **Peel** `drawSceneLayer` | **Blend** `blendPeelLayerToAccum` |
| :--- | :--- | :--- |
| 画什么 | 3D 场景：Spot + 三块玻璃 | **全屏 quad**（脚手架，不是地面、不是再画场景） |
| MVP | 有 `model / view / projection` | **无**；`gl_Position = vec4(aPos, 1.0)` |
| 着色器 | `depth_peeling_render.*`（光照、discard） | `depth_peeling_blend.*`（几乎只采样） |
| 输出 | `fboPeel_.color` + 本层深度 | 硬件混合写入 `fboAccum_` |
| 衔接 | — | 读 **`fboPeel_.color` 纹理** |

约定：Peel 使用 MVP 绘制场景几何；Blend 使用 NDC 全屏 quad，无 MVP。

---

## 2. blend 着色器职责

代码量很小，职责如下：

### `blend.vert`

```glsl
textureCoord = aTexCoord;
gl_Position = vec4(aPos, 1.0);  // quad.obj 顶点已是 NDC ±1
```

- 作用：铺满屏幕，让每个像素都出片元。  
- 不做：变换场景、剥层、算混合公式。

### `blend.frag`

```glsl
FragColor = texture(texture_diffuse, textureCoord);  // = fboPeel_.color
```

- 作用：把 **本层 peel 颜色** 交给后续混合单元，作为 **src**。  
- 不做：Front-to-Back 累加（那是 `glBlendFuncSeparate`）。

若关掉 `GL_BLEND`，这两个 shader 仍会跑，但会 **覆盖** `fboAccum_`，OIT 错误。

---

## 3. 真正叠色在哪里

CPU（`blendPeelLayerToAccum`）：

```cpp
glEnable(GL_BLEND);
glBlendFuncSeparate(GL_DST_ALPHA, GL_ONE, GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
// src = FragColor（本层 peel）
// dst = fboAccum_ 已有像素
```

\[
C' = C_{src}\,A_{dst} + C_{dst},\quad A' = A_{dst}(1-A_{src})
\]

分工：

```
Peel 写出一层图
    → blend.frag 采样成 FragColor (src)
    → 硬件 Blend 按上式写入 fboAccum_ (dst)
```

---

## 4. `framebuffer` 与 `inputTextures`（dst / src）

```cpp
modelQuad_->Draw(
    *shaderBlend_,
    fboAccum_.fbo,                            // framebuffer
    {{"texture_diffuse", fboPeel_.color.id}}, // inputTextures
    {}, GL_TRIANGLES, {false, true});
```

`Mesh::Draw` 里：

1. `glBindFramebuffer(..., framebuffer)` → **画到哪个 FBO**  
2. 把 `inputTextures` 绑到 sampler → **片元采样谁**

| 参数 | 作用 | Blend 时 | 混合角色 |
| :--- | :--- | :--- | :--- |
| `framebuffer` | 渲染目标 | `fboAccum_.fbo` | **dst** = 该 FBO **里已有的像素**（累加色） |
| `inputTextures` | 着色器采样输入 | `fboPeel_.color` → `texture_diffuse` | **src** = frag 输出的 `FragColor`（本层色） |
| `{false, true}` | 是否清色/清深 | 不清颜色、清深度 | **保住 dst**，否则累加被清掉 |

说明：

- 累加色不经由 `inputTextures` 传入，而是目标 FBO 中已有像素（dst）。
- 固定管线 `GL_BLEND` 读取当前 FBO 旧值作为 dst，与 shader 输出的 src 混合后写回同一 FBO。

若把混合写在 shader 里，通常要传两张纹理（peel + accum）。本工程用硬件 Blend，**accum 不必进 `inputTextures`**，只要 **绑 `fboAccum_` 当 `framebuffer`** 即可。

```
① Bind FBO = fboAccum_     → 旧像素 = dst
② Bind 纹理 = fboPeel_.color → frag 采样 → FragColor = src
③ 硬件 Blend                 → 写回 fboAccum_
```

---

## 5. 为何共用 `modelQuad_` 却不是「画玻璃」

`quad.obj` 同一份网格：

- Peel 里画玻璃：带 MVP，摆在世界坐标；  
- Blend 里：换 `shaderBlend_`，无 MVP → 全屏贴图。

几何资源可复用；**Pass 角色由着色器 + GL 状态决定**，不是由网格文件名决定。

---

## 6. 硬件混合在管线的哪一段？

不在顶点/片元着色器里算完就结束，而在 **片元着色器之后** 的固定管线 **逐片元操作（Per-Fragment Operations / ROP）**：

```
VS → 光栅化 → FS（产出 FragColor = src）
         → 剪裁 / 模板 / 深度测试 …
         → 【Color Blending】← glEnable(GL_BLEND) + glBlendFuncSeparate
         → 写入当前 FBO 颜色附件
```

同主题里深度写入顺序见：[glDepthMask与深度写入管线.md](./glDepthMask与深度写入管线.md) §2（表里最后一项就是混合）。

注意：**Final 上屏**（`framebuffer = 0`）里背景合成写在 `depth_peeling_final.frag` 里，一般 **不必**再靠硬件 Blend；本工程的硬件 Front-to-Back 主要用在 **Blend → fboAccum_**。

---

## 7. 一句话

`blend.vert` / `blend.frag` 是 **全屏采样脚手架**；  
**src** 来自 `inputTextures`（peel 图），**dst** 来自 `framebuffer`（accum 里已有像素）；  
公式在 `glBlendFuncSeparate`，发生在 **FS 之后的固定管线混合单元**；该 Pass 不绘制场景几何。
