// ==============================================================================
// HWRasterizeVS.glsl — Pass 4 顶点着色器：按可见 Cluster 实例化绘制
//
// 触发：glDrawArraysIndirect(GL_TRIANGLES, sWorkArgs[0])
//   - instanceCount 来自 ClusterCull 写入的 WorkArgs.mData[1]
//   - 每 Instance 绘制 384 个顶点（WorkArgs.mData[0]）
//
// 实例化语义：
//   gl_InstanceID  → VisiableClusterSWHW 中的 Cluster 下标
//   gl_VertexID    → 簇内顶点序号 0..383（对应 NaniteMesh 中的索引流）
//
// 输出：
//   gl_Position           裁剪空间位置（无效顶点可置 0）
//   V_PassThroughValue.x  Payload = (pageIndex<<8) | (clusterIndex+1)，供 FS 写入 VisBuffer
//
// 详见 doc/Code_Reading_Guide.md §5.3
// ==============================================================================
#version 450

layout(binding=0)uniform GlobalConstants {
    mat4 ProjectionMatrix;
    mat4 ViewMatrix;
    mat4 ModelMatrix;
    uvec4 Misc0;
    vec4 CameraPositionWS;   // xyz=相机世界坐标（本 Pass 减到相机相对空间）；w=LODScale（本 Pass 未用）
    vec4 ViewDirectionWS;      // 布局占位，本 Pass 未读
};

layout(binding=1,std430)readonly buffer FNaniteMesh{
    uint mData[];
}NaniteMesh;

// ClusterCull 输出：每可见簇占 2 个 uint，即 uvec2(pageIndex, clusterIndex)
layout(binding=2,std430)readonly buffer FVisiableClusterSWHW{
    uint mData[];
}VisiableClusterSWHW;

// VS → FS 传参：uvec4 = 4 个 uint（32 位无符号整数），不是 unsigned char / u8
// flat：整型 varyings 必须 flat，避免被当成 float 在三角形内插值
// 本 Demo 只写 .x 作 Cluster Payload；.y/.z/.w 未赋值，默认为 0，FS 也不读
layout(location=0)flat out uvec4 V_PassThroughValue;

struct ClusterInfo{
    uint mBaseOffset;        // Cluster 头在 NaniteMesh.mData 的 uint 下标
    uint mIndexOffset;       // 索引流相对 mBaseOffset 的 uint 偏移
    uint mIndexCount;        // 本簇三角形索引个数（多为 384）；超出部分顶点被 if 丢弃
    vec4 mLODBounds;
    float mLODError;
    float mEdgeLength;
};

// 与 ClusterCull.glsl 相同：从 NaniteMesh 解包 Cluster 头（布局见 ClusterCull.glsl GetClusterInfo 注释）
ClusterInfo GetClusterInfo(uint inPageIndex,uint inClusterIndex){
    uint pageCount=NaniteMesh.mData[0];
    uint pageBaseOffsetInBytes=NaniteMesh.mData[1+inPageIndex];
    uint pageBaseOffset=pageBaseOffsetInBytes/4;
    uint clusterCountOnPage=NaniteMesh.mData[pageBaseOffset];
    uint clusterBaseOffsetInBytes=NaniteMesh.mData[pageBaseOffset+1+inClusterIndex];
    uint clusterBaseOffset=pageBaseOffset+1+clusterCountOnPage+clusterBaseOffsetInBytes/4;
    uint clusterIndexOffset=NaniteMesh.mData[clusterBaseOffset]/4;
    uint clusterIndexCount=NaniteMesh.mData[clusterBaseOffset+1];
    uvec4 lodBounds=uvec4(
        NaniteMesh.mData[clusterBaseOffset+2u],
        NaniteMesh.mData[clusterBaseOffset+3u],
        NaniteMesh.mData[clusterBaseOffset+4u],
        NaniteMesh.mData[clusterBaseOffset+5u]
    );
    uint lodErrorAndEdgeLength=NaniteMesh.mData[clusterBaseOffset+6];
    ClusterInfo clusterInfo;
    clusterInfo.mBaseOffset=clusterBaseOffset;
    clusterInfo.mIndexOffset=clusterIndexOffset;
    clusterInfo.mIndexCount=clusterIndexCount;
    clusterInfo.mLODBounds=uintBitsToFloat(lodBounds);
    vec2 unpackedData  = unpackHalf2x16(lodErrorAndEdgeLength);
    clusterInfo.mLODError=unpackedData.x;
    clusterInfo.mEdgeLength=unpackedData.y;
    return clusterInfo;
}

// ==============================================================================
// main — 每个 Instance 画一个可见 Cluster；每个 Vertex 画簇内一条索引
// DrawIndirect：vertexCount=384（固定上限），instanceCount=可见簇数；实际三角数由 mIndexCount 决定
// ==============================================================================
void main(){
    // gl_InstanceID → 选「哪一个 Cluster」（不同实例 = 不同簇，从 VisiableClusterSWHW 取 page/cluster）
    // gl_VertexID   → 在该簇内部选「索引流第几条」0..383，不是全网格顶点偏移、也不是位置池下标
    //   流程：VertexID 查索引表 → 得到簇内局部顶点号 → 再查位置池（见下方两步间接寻址）
    uint vertexIndex=gl_VertexID;
    uint clusterIndexWithInvoke=gl_InstanceID;

    // 从 Pass3 输出缓冲取本实例要画的 (pageIndex, clusterIndex)
    uint pageIndex=VisiableClusterSWHW.mData[clusterIndexWithInvoke*2];
    uint clusterIndex=VisiableClusterSWHW.mData[clusterIndexWithInvoke*2+1];

    ClusterInfo clusterInfo=GetClusterInfo(pageIndex,clusterIndex);
    // 默认 (0,0,0,0)：vertexIndex>=mIndexCount 的「填充顶点」保持退化，不参与有效光栅
    vec4 positionCS=vec4(0.0f,0.0f,0.0f,0.0f);

    if(vertexIndex<clusterInfo.mIndexCount){
        // --- 第 1 步：索引流 → 簇内局部顶点号 ---
        // 索引表起点 = Cluster 头 + mIndexOffset；第 vertexIndex 个 uint 是三角形顶点索引
        uint currentVertexIndexOffsetBase=clusterInfo.mBaseOffset+clusterInfo.mIndexOffset;
        uint currentVertexIndexOffset=currentVertexIndexOffsetBase+vertexIndex;
        uint currentIndexInCluster=NaniteMesh.mData[currentVertexIndexOffset];

        // --- 第 2 步：按局部顶点号取模型空间 float3 位置 ---
        // Cluster 头占 7 个 uint（+0..+6 元数据），+7 起是位置池：每顶点 3 个 uint（floatBits）
        uint currentClusterPositionOffsetBase=clusterInfo.mBaseOffset+7u;
        uint currentVertexPositionDataOffset=currentClusterPositionOffsetBase+3*currentIndexInCluster;
        vec3 positionMS=uintBitsToFloat(
            uvec3(
                NaniteMesh.mData[currentVertexPositionDataOffset],
                NaniteMesh.mData[currentVertexPositionDataOffset+1],
                NaniteMesh.mData[currentVertexPositionDataOffset+2]
            )
        );

        // --- 第 3 步：模型空间 → 裁剪空间（与 Pass2/3 一致：先 Model，再减相机 xyz）---
        vec4 positionWS=ModelMatrix*vec4(positionMS,1.0f);
        positionWS.xyz=positionWS.xyz-CameraPositionWS.xyz;
        vec4 positionVS=ViewMatrix*positionWS;
        positionCS=ProjectionMatrix*positionVS;

        // --- Payload 打包进 V_PassThroughValue.x（单个 uint，再进 VisBuffer 低 32 位）---
        // 把一个 32 位 uint 切成两段 8 位语义（与 Pass2 ChildStartReference 同款布局）：
        //   高 24 位有效区里，pageIndex 占高位：pageIndex << 8
        //   低  8 位存页内 cluster 编号（+1 编码）：clusterIndex + 1
        // 合成：payload = (pageIndex << 8) | (clusterIndex + 1)
        // FS 反解示例：
        //   pageIndex    = payload >> 8
        //   clusterIndex = (payload & 0xFFu) - 1u
        // clusterIndex+1 的原因：payload==0 保留为「无命中/背景」，有效簇从 1 起编
        V_PassThroughValue.x=(pageIndex<<8)|(clusterIndex+1);
    }

    gl_Position=positionCS;
}
