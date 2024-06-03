`waitSampleCount()` 的作用是：**等待 GPU 完成剥离 Pass 的统计查询，并返回“本层有多少采样通过了深度测试”**。用它来判断深度剥离是否还要继续。

## 它在哪被调用

在 `peelAndBlend()` 里，每一层剥离画完之后：

```278:305:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
    glBeginQuery(GL_SAMPLES_PASSED, queryId_);
    drawSceneLayer(*shaderPeel_, fboPeel_.fbo, inputDepthIndex_,
                   outputDepthIndex_);
    glEndQuery(GL_SAMPLES_PASSED);

    const GLuint sampleCount = waitSampleCount();
    // ... 混合到 FBO_0 ...

    if (sampleCount <= 0) {
      break;
    }
```

`glBeginQuery` / `glEndQuery` 包住的是 **Peel 着色器绘制**；`waitSampleCount()` 负责把查询结果读回来。

## 函数逐行在做什么

```255:264:e:\opengl\liuhaonian\OpenGL_OIT_Depth_Peeling\src\DepthPeelingApp.cpp
GLuint DepthPeelingApp::waitSampleCount() {
  GLint available = 0;
  while (!available) {
    glGetQueryObjectiv(queryId_, GL_QUERY_RESULT_AVAILABLE, &available);
  }
  GLuint sampleCount = 0;
  glGetQueryObjectuiv(queryId_, GL_QUERY_RESULT, &sampleCount);
  std::cout << "Samples passed: " << sampleCount << std::endl;
  return sampleCount;
}
```

| 步骤 | API                         | 含义                                                         |
| ---- | --------------------------- | ------------------------------------------------------------ |
| 1    | `GL_QUERY_RESULT_AVAILABLE` | 查询对象是 **异步** 的：CPU 提交绘制后，GPU 可能还没算完。这里用 `while` 轮询，直到结果可用。 |
| 2    | `GL_QUERY_RESULT`           | 读取 `GL_SAMPLES_PASSED` 的计数值 → `sampleCount`。          |
| 3    | `std::cout`                 | 调试输出，方便看每一层剥了多少。                             |
| 4    | `return`                    | 把计数交给调用方，用于 `if (sampleCount <= 0) break`。       |

`queryId_` 在 `init()` 里用 `glGenQueries(1, &queryId_)` 创建，整段剥离循环复用同一个查询对象。

## `GL_SAMPLES_PASSED` 统计的是什么

`GL_SAMPLES_PASSED` 统计的是：**在 `BeginQuery`～`EndQuery` 之间，有多少个采样（sample）通过了深度测试并会写入帧缓冲**。

注意和片元着色器里 `discard` 的关系：

- **深度测试失败** → 不计入。
- **深度测试通过，但片元里 `discard`** → 一般 **仍可能** 先通过深度测试再被丢弃；具体实现因驱动/MSAA 略有差异。
- 本项目的剥离主要靠 `discard`（`gl_FragCoord.z <= frontDepth`），所以 `sampleCount` 更多是 **“这一层还有没有新几何参与竞争”** 的粗指标，而不是严格的“写入颜色的片元数”。

## 为什么需要这个函数（业务含义）

深度剥离是 **一层一层剥** 的：

- 第 0 层：最前面的透明片元；
- 第 1 层：去掉第 0 层后，剩余里最前的；
- …

当 **某一层没有任何东西可剥**（`sampleCount == 0`），再画下去也没有意义，应 **提前结束循环**，而不是一直跑到 `kMaxDepthPeelLayers`（默认 10）。

所以 `waitSampleCount()` 本质是：**“这一层剥离 Pass 还有没有产出？”的探测器**。

## 实现上的注意点

**忙等（busy-wait）**：`while (!available)` 会一直占着 CPU 等 GPU，简单但会 **卡住 CPU/GPU 流水线**。教学/demo 够用；正式项目常改为：

- 不等待，固定剥 N 层；或  
- 用 `GL_QUERY_RESULT_NO_WAIT` 做异步查询，下一帧再读。

**与 `discard` 的配合**：即使深度测试通过，Peel 片元里仍可能 `discard`，那时 `sampleCount` 可能 > 0 但颜色缓冲几乎为空；一般场景下两者仍大致相关。若发现“明明没层了还不 break”，需要结合 RenderDoc 看是 MSAA 还是 `discard` 时机问题。

---

**一句话**：`waitSampleCount()` = 等待 GPU 统计完本层 Peel 绘制里“通过深度测试的采样数”，返回该数字；`peelAndBlend` 用它判断 **透明层是否剥完**，从而提前 `break` 出剥离循环。