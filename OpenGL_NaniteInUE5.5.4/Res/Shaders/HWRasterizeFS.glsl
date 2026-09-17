// ==============================================================================
// HWRasterizeFS.glsl — Pass 4 片段着色器：64 位 VisBuffer 原子深度竞争
//
// 无颜色附件输出；通过 SSBO atomicMin 实现「深度 + Cluster Payload」合一写入。
//
// VisBuffer64 每像素 uint64 布局（见 doc/Code_Reading_Guide.md §4.4）：
//   高 32 位 — floatBitsToUint(gl_FragCoord.z)，深度越小值越小（近处胜出）
//   低 32 位 — VS 传入的 (pageIndex<<8)|(clusterIndex+1)
//
// 扩展：GL_ARB_gpu_shader_int64、GL_NV_shader_atomic_int64
// ==============================================================================
#version 450
#extension GL_ARB_gpu_shader_int64 : enable
#extension GL_NV_shader_atomic_int64 : enable

layout(binding=3,std430)buffer FVisBuffer64{
    uint64_t mData[];
}VisBuffer64;

// uvec4：4×uint32 的 VS 输出；本 Pass 只读 .x（Cluster Payload），.y/.z/.w 恒为 0
layout(location=0)flat in uvec4 V_PassThroughValue;

void main(){
    ivec2 texcoord=ivec2(gl_FragCoord.xy);

    float depth=gl_FragCoord.z;
    // 深度转 uint，与 VisBuffer 高 32 位比较（越小越近，atomicMin 保留更近片段）
    uint64_t pixelDepth=floatBitsToUint(depth);

    int pixelIndex=texcoord.y*1280+texcoord.x;

    // 64 位像素值 = [高32:深度][低32:Cluster Payload]
    // Payload 来自 VS 的 V_PassThroughValue.x，打包格式：
    //   pageIndex    = payload >> 8
    //   clusterIndex = (payload & 0xFFu) - 1u   （VS 写入时做了 clusterIndex+1）
    // 若 payload==0，表示该像素无有效 Cluster（背景或未写入）
    uint payload=V_PassThroughValue.x;
    uint64_t pixelValue64 = pixelDepth<<32|uint64_t(payload);

    atomicMin(VisBuffer64.mData[pixelIndex],pixelValue64);
}
// ==============================================================================
// 附录：VisBuffer64 与 atomicMin（64 位原子深度竞争）
//
// 【每像素内存布局】uint64_t mData[pixelIndex]（行优先：pixelIndex = y * width + x）
//   高 32 位 — floatBitsToUint(gl_FragCoord.z)  深度位模式（非负浮点 [0,1] 时 uint 序与 float 序一致）
//   低 32 位 — Payload = (pageIndex<<8) | (clusterIndex+1)  见 HWRasterizeVS.glsl
//   打包：pixelValue64 = (pixelDepth << 32) | uint64_t(payload)
//
// 【本 Pass 不写颜色附件、不用硬件 Depth Buffer】
//   深度测试由 SSBO 上的 atomicMin 完成；需 GL_ARB_gpu_shader_int64 + GL_NV_shader_atomic_int64。
//
// 【atomicMin 语义】（线程安全，多片元同像素并行竞争）
//   VisBuffer64[pixelIndex] = min( VisBuffer64[pixelIndex], pixelValue64 );
//   pixelValue64 更小 → 整条 64 位被替换为新值（更近的深度 + 对应的 Payload）
//   pixelValue64 更大或相等 → 保持原值（更远的不写入；相等时 min 保留旧值）
//
// 【为何一条 uint64 的 min 等价于 Z-Test + 写 Cluster ID】
//   比较的是整个 64 位整数，因深度在高 32 位，故先按深度比远近；
//   只有「整体更小」的片元才会更新内存，赢家的低 32 位 Payload 与深度一并写入。
//
// 【高 32 位相同怎么办】会发生（共面、共享边、Z-fighting、浮点量化等）。
//   此时高 32 相等，比低 32 位：Payload 更小的胜出（平局决胜，非严格材质优先级，通常可接受）。
//
// 【每帧初值】RasterClear.comp 写入 0xFFFFFFFF00000000ul
//   高 32 = 0xFFFFFFFF → 最远深度（1.0f 位模式）；低 32 = 0 → 无 Cluster（背景）
//   任意真实三角形深度位模式 < 0xFFFFFFFF，故首次写入可成功。
//
// 【后续 Pass】Visualization.glsl 读低 32 位伪彩；反解：
//   pageIndex    = payload >> 8
//   clusterIndex = (payload & 0xFFu) - 1u
//   payload==0   → 背景像素
//
// 【注意】pixelIndex 必须与 RasterClear / Visualization 使用同一 width（本文件 main 暂写 1280）
// ==============================================================================
