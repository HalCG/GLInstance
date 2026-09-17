// ==============================================================================
// RasterClear.glsl — Pass 1：帧初清空与 WorkArgs 复位
//
// 管线位置：RenderOneFrame() 第一个 Compute Pass（见 doc/Code_Reading_Guide.md §1.2）
//
// 职责：
//   1. 将 VisBuffer64 每像素置为 0xFFFFFFFF00000000（最远深度 + 空 Payload）
//   2. 在 (0,0) 单线程初始化 Current/Next WorkArgs，供后续 BVH Ping-Pong 使用
//
// 绑定：
//   [0] CurrentIndirectWorkArgs  — sWorkArgs[0]
//   [1] NextIndirectWorkArgs     — sWorkArgs[1]
//   [2] VisBuffer64              — sVisBuffer64
//
// WorkArgs 下标语义（与 DrawArraysIndirectCommand 前段共用，详见导读 §4.1）：
//   mData[0]=384  每 Instance 顶点数（128 三角 × 3）
//   mData[1]=0    可见 Cluster 计数（instanceCount，后续 Pass 递增）
//   mData[5]=0    当前层节点列表在 Batches 缓冲中的起始 offset
//   mData[6]=1    当前层待遍历节点数（首帧从根节点 1 个开始）
// ==============================================================================
#version 450
#extension GL_ARB_gpu_shader_int64 : enable

layout(local_size_x=8,local_size_y=8,local_size_z=1)in;

layout(binding=0,std430)buffer FCurrentIndirectWorkArgs{
    uint mData[];
}CurrentIndirectWorkArgs;

layout(binding=1,std430)buffer FNextIndirectWorkArgs{
    uint mData[];
}NextIndirectWorkArgs;

layout(binding=2,std430)buffer FVisBuffer64{
    uint64_t mData[];
}VisBuffer64;

void main(){
    ivec2 texcoord=ivec2(gl_GlobalInvocationID.xy);

    // 分辨率与 scene.cpp Init() 默认画布一致；外提为 uniform 见 doc/Code_Optimization_TODO.md §2.3
    if(any(greaterThanEqual(texcoord,ivec2(1280,720)))){
        return;
    }

    // 全局仅一次：复位双缓冲 WorkArgs（Pass 2 Ping-Pong 的读写起点）
    if(texcoord.x==0&&texcoord.y==0){
        CurrentIndirectWorkArgs.mData[0]=384;
        CurrentIndirectWorkArgs.mData[1]=0;
        CurrentIndirectWorkArgs.mData[5]=0;
        CurrentIndirectWorkArgs.mData[6]=1;

        NextIndirectWorkArgs.mData[0]=384;
        NextIndirectWorkArgs.mData[1]=0;
        NextIndirectWorkArgs.mData[5]=0;
        NextIndirectWorkArgs.mData[6]=1;
    }

    int pixelIndex=texcoord.y*1280+texcoord.x;
    VisBuffer64.mData[pixelIndex]=0xFFFFFFFF00000000ul;
}
