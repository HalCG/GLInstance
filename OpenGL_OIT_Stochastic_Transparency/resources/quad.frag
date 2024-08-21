#version 420 core
/**
 * @file quad.frag
 * @brief Stochastic Transparency (随机透明度) 片段着色器
 *
 * 核心原理：
 * 1. 子像素多重采样掩码 (gl_SampleMask[])：
 *    GLSL 提供内建输出变量 `out int gl_SampleMask[]`。当 CPU 端开启 `glEnable(GL_SAMPLE_MASK)` 时，
 *    Fragment Shader 写入 `gl_SampleMask[0]` 相当于动态配置当前片段在 16 个 MSAA 采样点上的 Pass/Fail 掩码。
 *
 * 2. Alpha 透明度转换为概率采样 (Probability Sampling)：
 *    设片元的透明度 Alpha 为 `coverage` (如 Alpha = 0.5)。
 *    对 16 个 MSAA 采样点中的每一个 i，利用伪随机函数 `random(i, frameID)` 生成 [0, 1) 的随机实数 r。
 *    若 r < coverage，则将 bit i 置为 1 (保留该子像素采样)；否则置 0 (剔除该子像素采样)。
 *    在期望上，16 个采样点中恰好有 16 * Alpha 个采样点被开启。
 *
 * 3. 混合与 Resolve：
 *    由于 GPU 正常对每个保留的子像素做深度测试 (Z-Test) 和 Depth 写入，
 *    所有不透明与透明物体的子像素会在 MSAA Buffer 中精确重叠覆盖。
 *    最后 Resolve 到屏幕时，硬件自动求 16 个采样点的加权平均，无需任何 Back-to-Front / Front-to-Back 显式排序！
 */

out vec4 FragColor;
in vec2 textureCoord;

uniform int frameID;   // 随机数 Seed 偏移量 (避免不同物体/帧的采样点掩码完全一致)
uniform int sampleCnt; // MSAA 采样点数 (如 16)
uniform sampler2D texture_diffuse;

// 经典 GLSL 三角函数伪随机生成器：输入 seed 向量，输出 [0.0, 1.0) 均匀分布随机数
float rand(vec2 seed) {
    return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() { 
    vec4 color = texture(texture_diffuse, textureCoord);
    
    uint randMask = 0u;
    float coverage = color.a; // Alpha 透明度作为采样概率 coverage
    
    // 【反直觉核心逻辑详解】：
    // 随机透明度 (Stochastic Transparency) 摆脱排序的关键：
    // 将 Alpha = 0.7 解释为“70% 概率”。把一个像素拆成 sampleCnt (例如 16) 个 MSAA 子采样点。
    // 对每个子采样点 i “掷骰子” (生成 0~1 随机数 r)：
    // - 若 r < Alpha (70%)：命中概率，将 bit i 设为 1 (该子像素写入 100% 颜色)
    // - 若 r >= Alpha (30%)：未命中，将 bit i 设为 0 (该子像素保留空位给背后物体)
    // 最终 MSAA 硬件对 16 个子像素求平均时，统计期望正好等于 70% 透明度！
    for (int i = 0; i < sampleCnt; i++) {
        vec2 seed = vec2(i, frameID);
        float r = rand(seed); // 生成 [0.0, 1.0) 的伪随机数
        
        // 概率判定：若随机数 r < alpha，则开启第 i 个采样点的二进制 Bit
        if (r < coverage) {
            randMask |= (1u << i); // 位运算：将二进制第 i 位设为 1 (例如 i=3 -> 0b1000)
        }
    }
    
    // 写入 OpenGL 内置硬件采样掩码 (gl_SampleMask[0])！
    // 【为什么索引是 [0]？】：
    // `out int gl_SampleMask[]` 是一个数组，每个 32 位整型元素包含 32 个 Bit。
    // - `gl_SampleMask[0]` 控制第 0 ~ 31 个 MSAA 采样点；
    // - `gl_SampleMask[1]` 控制第 32 ~ 63 个 MSAA 采样点。
    // 当前项目使用的是 16x MSAA (采样点 0~15)，全都在前 32 位内，因此只需要给 `gl_SampleMask[0]` 赋值！
    //
    // 【硬件 Z-Test 裁决说明】：
    // 每个 MSAA 子采样点 (孔 i) 都拥有独立的 Hardware Depth Buffer！
    // 当近处 A 和远处 B 抢占同一个孔 i 时，GPU 硬件 Z-Test 自动让更近的 A 覆盖 B；
    // 当近处 A 没抢到孔 i 而远处 B 抢到了，背景 B 就会从孔 i 透出来。
    gl_SampleMask[0] = int(randMask);
    
    FragColor = color;
}


/*
fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);，这行代码**每次只生成一个随机数**。

## 随机数范围

范围是 **`[0.0, 1.0)`**（大于等于 0，小于 1）。

因为 `fract()` 取的是小数部分，而 `sin()` 输出范围是 `[-1, 1]`，乘以一个大的常数后取小数，结果始终落在 `[0, 1)` 之间。

## 为什么说"仅生成一个随机数"

这行代码是**确定性伪随机**，对同一个 `seed` 输入，输出总是相同的值。严格来说它不生成"多个"随机数，而是**由输入 seed 决定的一个确定输出**。

如果你需要多个随机数，通常是通过改变 `seed` 来实现。以 GLSL 中常见的模式为例：

```glsl
// 输入一个二维坐标，输出一个随机值
float random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}

// 用不同的输入得到不同的"随机"值
float r1 = random(gl_FragCoord.xy);       // 基于像素坐标
float r2 = random(gl_FragCoord.xy + 1.0); // 偏移后得到另一个值
float r3 = random(gl_FragCoord.yx);       // 交换坐标得到另一个值
```

所以如果你看到这段代码在 Shader 里逐像素执行，那每个像素都会算出一个属于它的随机数（像素之间不同），但单个像素的这行代码自己只产生一个值。
*/