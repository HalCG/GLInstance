#version 420 core
/**
 * Stochastic Transparency 片元着色器
 *
 * 流程：
 *   α → 概率覆盖 → 写入 gl_SampleMask → 硬件按采样点 Z-Test/写色 → Resolve 等权平均
 *
 * 与 Depth Peeling / Linked List 的差异（α 相同时）：
 *   - 前两者：Over / Front-to-Back，近处按透射权重压远处，更接近物理半透明
 *   - 本方案：Resolve 对各 MSAA 采样点算术平均（等权）；近远仅在同一采样点上靠 Z 裁决
 *
 * 前置状态（CPU）：GL_MULTISAMPLE + GL_SAMPLE_MASK；深度测试/写入开启
 *
 * gl_SampleMask：数组，每元素 32 bit（[0]=sample0..31，[1]=32..63…）
 *   初值：全部 bit=1（全开）；本工程按 α 改写为随机子集
 *   为何 [0]：16x 的 sample0..15 落在第一组，只需写 gl_SampleMask[0]
 *   分工：Mask 管覆盖率；之后硬件仍按采样点做 Z（非「不管深度」）
 *   直觉：半透明 = 往 MSAA 孔位「投射」——bit=1 才投得上该点；bit=0 不碰该点
 *   关 GL_SAMPLE_MASK / 掩码全 1：近处对全部孔投射并写 Z → 后面被挡死，像不透明盖住
 *   详解：docs/SampleMask投射与深度测试.md ；状态：docs/五行GL状态与管线阶段.md
 */

out vec4 FragColor;
in vec2 textureCoord;

uniform int frameID;   // 伪随机种子偏移（错开不同物体的掩码图案）
uniform int sampleCnt; // MSAA 采样点数（如 16）
uniform sampler2D texture_diffuse;

// 确定性伪随机：同一 seed → 同一 [0,1) 值；换 seed 得下一个随机数
float rand(vec2 seed) {
  return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
  vec4 color = texture(texture_diffuse, textureCoord);
  float coverage = color.a; // α = 打开各采样点的概率

  // 构造覆盖掩码：randMask 为整数位图，bit i ∈ {0,1} 对应采样点 i
  // 例 α≈0.5、16x：期望约 8 个 bit 为 1，如 0b0101100101011010
  uint randMask = 0u;
  for (int i = 0; i < sampleCnt; i++) {
    float r = rand(vec2(i, frameID));
    if (r < coverage) {
      randMask |= (1u << i);
    }
  }

  // 覆盖默认全 1：写入后 bit=1 的点才写色+Z；同点近远仍由硬件 Z 裁决
  gl_SampleMask[0] = int(randMask); // 仅用 [0]：见文件头

  FragColor = color; // 不走 glBlend；半透明来自覆盖率 + Resolve 平均
}
