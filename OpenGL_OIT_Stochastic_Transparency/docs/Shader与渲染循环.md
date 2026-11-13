# Shader 与渲染循环

> 返回：[README.md](./README.md) · 相关：[SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md) · [五行GL状态与管线阶段.md](./五行GL状态与管线阶段.md)

实现侧：`resources/quad.vert` / `quad.frag`，以及 `StochasticTransparencyApp` 的 init / 绘制循环。投射与深度语义见专题 1，不在此重复长文。

---

## 1. quad.vert —— 顶点着色器

标准 MVP 变换；本工程片元未用光照，仍输出世界坐标与法线以保持接口完整。

```glsl
#version 430 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

out vec3 vertexPos;
out vec3 vertexNor;
out vec2 textureCoord;

void main() {
    textureCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0f);
    vertexPos = (model * vec4(aPos, 1.0f)).xyz;
    vertexNor = mat3(transpose(inverse(model))) * aNor;
}
```

---

## 2. quad.frag —— 片段着色器（核心）

唯一关键 Shader：α → 随机覆盖掩码 → `gl_SampleMask`；不走 `glBlend`。逻辑摘要（以仓库源码为准，含文件头注释）：

```glsl
vec4 color = texture(texture_diffuse, textureCoord);
float coverage = color.a;  // α = 打开各采样点的概率

uint randMask = 0u;
for (int i = 0; i < sampleCnt; i++) {
  float r = rand(vec2(i, frameID));
  if (r < coverage)
    randMask |= (1u << i);
}
gl_SampleMask[0] = int(randMask);  // 仅用 [0]：16x 落在 sample 0..15
FragColor = color;
```

半透明来自覆盖率 + Resolve 平均。为何 bit=1 才「投得上」、深度仍开着能半透：见 [SampleMask投射与深度测试.md](./SampleMask投射与深度测试.md)。

---

## 3. `gl_SampleMask[0]`

```glsl
gl_SampleMask[0] = int(randMask);
```

| 项目 | 说明 |
|------|------|
| 类型 | `out int gl_SampleMask[]` |
| 可用版本 | GLSL 4.0+ |
| 含义 | 每元素 32 bit；bit i 控制采样点 i 是否被当前片元覆盖 |
| **初值** | **全部 bit = 1**（全开） |
| 为何写 `[0]` | `[0]` → sample 0..31；本工程 16x 只赋 `[0]` |
| 与深度 | Mask 只定「进哪些点」；之后仍按点做 Z |
| `GL_SAMPLE_MASK` 关闭 | FS 赋值被忽略，等效全开 → 无随机半透明 |

示意（MSAA 16x，α = 0.5）：约一半 bit 为 1，与覆盖率匹配。同孔多物体时的 Z 裁决见投射专题。

---

## 4. 伪随机与覆盖率

```glsl
float r = fract(sin(dot(vec2(i, frameID), vec2(12.9898, 78.233))) * 43758.5453);
if (r < coverage)
    randMask |= (1u << i);
```

| 步骤 | 说明 |
|------|------|
| `dot` / `sin` / `* 大常数` / `fract` | 经典 GLSL 伪随机，结果 ∈ [0, 1) |
| `i` | 样本索引，保证各 sample 序列不同 |
| `frameID` | 对象种子，避免多物体 mask 图案完全相同 |
| `(1u << i)` | 将 bit i 置 1 |

---

## 5. CPU：初始化与窗口

```cpp
glEnable(GL_MULTISAMPLE);
glEnable(GL_SAMPLE_MASK);
glEnable(GL_DEPTH_TEST);
glDepthFunc(GL_LEQUAL);
glDepthMask(GL_TRUE);
```

```cpp
glfwWindowHint(GLFW_SAMPLES, 16);
```

| 状态 | 作用 |
|------|------|
| `GL_MULTISAMPLE` | 启用 MSAA 硬件管线 |
| `GL_SAMPLE_MASK` | 允许 FS 写 `gl_SampleMask` |

两者须同时启用。各状态在管线何时生效：见 [五行GL状态与管线阶段.md](./五行GL状态与管线阶段.md)。

---

## 6. CPU：renderScene

- 直接渲染到默认帧缓冲（`targetFbo = 0`），无离屏 FBO。
- Spot + 蓝/绿/红窗各 Draw 一次，**不排序**；物体间 `clearColorDepth = {false,false}`，深度在多次绘制间累积。
- 每物体传入 `sampleCnt`（如 `GL_MAX_SAMPLES`）与不同的 `frameID`：

```cpp
static int frameID = 0;
int modelCnt = 4;
shaderQuad_->setInt("frameID", (frameID++) % modelCnt);
```

完整循环：`beginFrame`（清颜色+深度）→ `renderScene` → `endFrame`（`glfwSwapBuffers`，硬件 Resolve）。相对 Linked List（多 Pass）/ Depth Peeling（N Pass），此处单 Pass。

---

## 7. 数据流

```text
CPU: frameID / sampleCnt / MVP
        ↓
顶点: MVP → gl_Position
        ↓
片元: texture → coverage=α → 对每 sample 掷骰子 → gl_SampleMask[0]
        → FragColor = color
        ↓
硬件: 按 mask 覆盖 → 每 sample 深度测试/写入 → Resolve 平均 → 屏幕
```

---

## 8. 实现小结

| 组成 | 说明 |
|------|------|
| vertex | 标准 MVP |
| fragment | α → 随机掩码 → `gl_SampleMask[0]` |
| 随机数 | `fract(sin(dot(...)))` |
| frameID | `(frameID++) % modelCnt` |
| GL 状态 | MULTISAMPLE + SAMPLE_MASK + DEPTH |
| 目标 | 默认帧缓冲，无 FBO |
| Pass | 1 |

代价是随机噪声；许多实时场景可接受。精确需求仍看 Linked List / Depth Peeling。
