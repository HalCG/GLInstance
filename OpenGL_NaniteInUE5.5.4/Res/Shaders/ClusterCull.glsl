// ==============================================================================
// ClusterCull.glsl — Pass 3：候选 Cluster 精筛 + 生成 DrawIndirect 实例数
// 【本 Pass 在整条管线里的位置】
//   Pass 2 (NodeAndClusterCull ×4) 已在 BVH 上粗筛，把「可能该画」的 Cluster 写入 Batches[1024+]，
//   并把候选个数记在 WorkArgs.mData[1]。本 Pass 再对每个候选做精确 LOD 判断，通过的写入
//   VisiableClusterSWHW，并把最终个数写回 WorkArgs.mData[1]，供 glDrawArraysIndirect 当 instanceCount。
// 【和 Pass 2 的 LOD 比较方向相反（极易混）】
//   Pass 2：projectionScales.x <= 阈值  →  够细了，可以停在这一层
//   Pass 3：projectionScales.x >  阈值  →  够大了，值得画这个 Cluster
// 更多背景：doc/Code_Reading_Guide.md §5.2、§4.5
// ==============================================================================
#version 450
// 本 Shader 只用 1 个线程（单线程 for 循环），便于调试；不是最终性能形态。
layout(local_size_x=1,local_size_y=1,local_size_z=1)in;
// ==============================================================================
// binding=0：GlobalConstants（UBO，CPU 每帧更新，本 Pass 只读）
// C++ 对应：sGlobalConstants（oglcontext.h / scene.cpp::RenderOneFrame）
// ==============================================================================
layout(binding=0)uniform GlobalConstants {
    mat4 ProjectionMatrix;   // 透视投影矩阵（GetProjectionScales 里判断正交/透视）
    mat4 ViewMatrix;         // 本 Pass 未直接使用（包围球变换在 main 里手写 Model + 减相机）
    mat4 ModelMatrix;        // 模型空间 → 世界空间（用于包围球中心）
    uvec4 Misc0;             // x=Mip 级别索引（方向键切换）；本 Pass 当前未读取，保留与 C++ 布局一致
    vec4 CameraPositionWS;   // xyz=相机世界坐标；w=LODScale（参与本 Pass LOD 判据，见 main）
    vec4 ViewDirectionWS;    // xyz=相机前向（GetProjectionScales 用）；w=LODScaleHW（布局占位，未参与判据）
};
// ==============================================================================
// binding=1：NaniteMesh（SSBO，只读）
// C++ 对应：sNaniteMesh，文件 Res/mitsuba.nanitemesh 加载进显存
// 内容：Nanite 离线编码后的整网格二进制流，按 Page → Cluster → 顶点/索引 分层存放
// ==============================================================================
layout(binding=1,std430)buffer FNaniteMesh{
    uint mData[];            // 扁平 uint 数组；具体含义由 GetClusterInfo 按 page/cluster 解析
}NaniteMesh;
// ==============================================================================
// binding=2：IndirectWorkArgs（SSBO，读本 Pass 只改 mData[1]）
// C++ 对应：sWorkArgs[0]（见 scene.cpp ClusterCullPass 绑定）
// 同一块缓冲前半段还是 DrawArraysIndirect 命令字段，见下方 mData 下标说明
// ==============================================================================
layout(binding=2,std430)buffer FIndirectWorkArgs{
    uint mData[];
}IndirectWorkArgs;
// 本 Pass 用到的下标：
//   mData[1]  读：进入本 Pass 时是「候选 Cluster 个数」（Pass 2 写入）
//             写：离开本 Pass 时改为「可见 Cluster 个数」（给 DrawIndirect 当 instanceCount）
// ==============================================================================
// binding=3：VisiableClusterSWHW（SSBO，本 Pass 输出，从 0 开始顺序写）
// C++ 对应：sVisiableClusterSWHW
// 后续 HWRasterizeVS 用 gl_InstanceID 索引：mData[instance] = (pageIndex, clusterIndex)
// ==============================================================================
layout(binding=3,std430)buffer FVisiableClusterSWHW{
    uvec2 mData[];           // 每个可见 Cluster 一条记录，比 uint 交错数组更易读
}VisiableClusterSWHW;
// ==============================================================================
// binding=4：MainAndPostNodeAndClusterBatches（SSBO，本 Pass 只读 [1024..] 段）
// C++ 对应：sMainAndPostNodeAndClusterBatches
//   [0..1023]     Pass 2 的 BVH 节点栈（本 Pass 不读）
//   [1024..]      候选 Cluster 列表，交错存放：pageIndex, clusterIndex, pageIndex, ...
// ==============================================================================
layout(binding=4,std430)buffer FMainAndPostNodeAndClusterBatches{
    uint mData[];
}MainAndPostNodeAndClusterBatches;
// ==============================================================================
// ClusterInfo — 从 NaniteMesh 解包后、Shader 内使用的「单个 Cluster 摘要」
// 注意：这是运行时临时 struct，不是 .nanitemesh 磁盘上的原始布局
// ==============================================================================
struct ClusterInfo{
    uint mBaseOffset;        // 该 Cluster 头在 NaniteMesh.mData 里的 uint 下标（后续 HWRasterize 也用）
    uint mIndexOffset;       // 索引流相对 mBaseOffset 的 uint 偏移（三角形顶点索引表）
    uint mIndexCount;        // 索引个数，Nanite 固定簇多为 384（128 三角 × 3 顶点）
    vec4 mLODBounds;         // xyz=包围球中心（模型空间）, w=包围球半径
    float mLODError;         // 该 Cluster 的几何误差（与屏幕投影尺度比较）
    float mEdgeLength;       // 簇内代表边长（本 Pass 未参与判断，解码保留供它用）
};
// ==============================================================================
// GetClusterInfo — 用 (pageIndex, clusterIndex) 在 NaniteMesh 里定位一个 Cluster
// 【pageIndex 与 clusterIndex 的关系】二者是 Cluster 的「二维地址」，必须成对使用：
//   pageIndex     = 第几个资源页（Page），对应 Nanite 流式分页，不是 BVH 节点号
//   clusterIndex  = 该页内的第几个 Cluster（从 0 起），只在当前 pageIndex 下有效
//   (page=0,cluster=3) 与 (page=1,cluster=3) 是两个完全不同的几何簇
// 【易混：「4」】BVH 每节点 4 个子分支(四叉树,Pass2) ≠ 每页 4 个 Cluster
//   每页 Cluster 个数 = mData[pageBase]（不固定）；Pass2 叶子用 ChildStartReference 打包：
//     pageIndex = ChildStartReference>>8 ; 页内起始 clusterIndex = ChildStartReference&0xFFu
//   同一叶子下多簇：pageIndex 相同，clusterIndex 从页内起始连续 +0,+1,+2...
// 完整示意图（BVH / Page / Cluster / 字节÷4）：doc/Nanite_Data_Structures.md §3
// NaniteMesh 三层结构（简化）：
//   mData[0]                          = 资源页总数 pageCount
//   mData[1 .. pageCount]             = 各 Page 起始字节偏移（再 /4 得 uint 下标）
//   每个 Page 内：
//   mData[pageBase]                   = 本页 Cluster 数量
//   mData[pageBase+1 .. +clusterCount]= 各 Cluster 在本页内的字节偏移
//   每个 Cluster 头（从 clusterBase 起）：
//   [+0] 索引区字节偏移 / 4  → mIndexOffset
//   [+1] 索引个数            → mIndexCount
//   [+2..+5] 包围球 float   → mLODBounds（磁盘上是 uint，uintBitsToFloat）
//   [+6] half2 打包          → mLODError(.x), mEdgeLength(.y)
//   [+7..] 顶点位置等        → HWRasterizeVS 使用，本 Pass 不读
// ==============================================================================
ClusterInfo GetClusterInfo(uint inPageIndex,uint inClusterIndex){
    // --- 第 1 步：根据 pageIndex 找到这一页在 mData[] 里的起始位置 ---
    uint pageCount=NaniteMesh.mData[0];
    uint pageBaseOffsetInBytes=NaniteMesh.mData[1+inPageIndex];  // [0]是页数，故 1+pageIndex 查页目录
    uint pageBaseOffset=pageBaseOffsetInBytes/4;                 // 字节偏移→uint下标（÷4）
    // --- 第 2 步：在本页内根据 clusterIndex 找到 Cluster 头 ---
    uint clusterCountOnPage=NaniteMesh.mData[pageBaseOffset];    // 页头+0：本页 Cluster 个数（1个uint）
    uint clusterBaseOffsetInBytes=NaniteMesh.mData[pageBaseOffset+1+inClusterIndex]; // 页头偏移表[clusterIndex]
    // 页头=1个个数+N个表项；表项是数据区内字节偏移；二者相加才到该 Cluster 头
    uint clusterBaseOffset=pageBaseOffset+1+clusterCountOnPage+clusterBaseOffsetInBytes/4;
    // --- 第 3 步：读 Cluster 头字段 ---
    uint clusterIndexOffset=NaniteMesh.mData[clusterBaseOffset]/4;   // 索引流起始（相对 clusterBase）
    uint clusterIndexCount=NaniteMesh.mData[clusterBaseOffset+1];    // 有多少个索引要画
    // 包围球：磁盘上按 float 位型存的 uint，需 uintBitsToFloat 还原
    uvec4 lodBounds=uvec4(
        NaniteMesh.mData[clusterBaseOffset+2u],
        NaniteMesh.mData[clusterBaseOffset+3u],
        NaniteMesh.mData[clusterBaseOffset+4u],
        NaniteMesh.mData[clusterBaseOffset+5u]
    );
    // LOD 误差与边长：一个 uint 里打包了两个 16-bit half 浮点
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
// GetProjectionScales — 估算包围球在屏幕上「有多大」（像素尺度）
// 输入 Bounds：相机相对空间下的包围球 (center.xyz, radius.w)
//   即 main() 里已做：世界空间包围球 − 相机位置，半径不变
// 输出 vec2：
//   .x = minScale  球在屏幕上投影尺寸的保守下界（本 Pass 用这个做 LOD 比较）
//   .y = maxScale  投影尺寸上界（本 Pass 未用）
// 直观理解：
//   - 物体离相机越远，或球越小 → projectionScales.x 越小
//   - 物体越近、球越大 → projectionScales.x 越大
//   - 与 mLODError * LODScale 比较：大于阈值说明屏幕上的误差「够显眼」，值得画
// 数学来源：UE Nanite 屏幕空间误差推导的简化版；正交投影时直接返回 (1,1)
// 分步图示：doc/figures/GetProjectionScales/00_overview.svg（01~05 各子步骤）
// ==============================================================================
vec2 GetProjectionScales(vec4 Bounds)
{
	// 正交投影矩阵的 [3][3] 为 1；透视则为 0。正交时不做透视缩放判断。
	if( ProjectionMatrix[3][3] >= 1.0f )
	{
		return vec2(1,1);
	}
	vec3 Center = Bounds.xyz;   // 球心（相对相机）
	float Radius = Bounds.w;    // 球半径
	float ZNear = 10.0f;      // 近裁剪面（与 C++ 投影 near=1 不同，此处为 Shader 内常量）
	float DistToClusterSq = dot(Center,Center);  // 相机到球心距离平方 |C|^2
	// 把球心分解到：沿视线方向分量 Z，垂直视线平面分量 X（勾股定理）
	float Z = dot(ViewDirectionWS.xyz, Center);
	float XSq = DistToClusterSq - Z * Z;
	float X = sqrt( max(0.0f, XSq) );
	// 球表面最近点相关几何量（用于估计屏幕张角）
	float DistToTSq = DistToClusterSq - Radius * Radius;
	float DistToT = sqrt( max(0.0f, DistToTSq) );
	float ScaledCosTheta = DistToT;
	float ScaledSinTheta = Radius;
	float ScaleToUnit = 1.0f/DistToClusterSq;
	float By = (  ScaledSinTheta * X + ScaledCosTheta * Z ) * ScaleToUnit;
	float Ty = ( -ScaledSinTheta * X + ScaledCosTheta * Z ) * ScaleToUnit;
	float H = ZNear - Z;  // 保留原公式项（与 UE 一致）
	// 包围球在视线方向上的近端/远端深度（夹到 ZNear 以上）
	float MinZ = max( Z - Radius, ZNear );
	float MaxZ = max( Z + Radius, ZNear );
	float MinCosAngle = Ty;
	float MaxCosAngle = By;
	// 球在相机前方且越过近裁剪面时，返回 [minScale, maxScale]；否则视为不可见尺度 0
	if(Z + Radius > ZNear)
		return vec2( MinZ * MinCosAngle, MaxZ * MaxCosAngle );
	else
		return vec2( 0.0f, 0.0f );
}
// ==============================================================================
// main — 遍历所有候选 Cluster，写可见列表 + instanceCount
// ==============================================================================
void main(){
    // Pass 2 结束时 mData[1] = 候选 Cluster 个数（不是可见个数）
    uint clusterCount=IndirectWorkArgs.mData[1];
    uint visiableClusterCount=0;
    for(uint i=0;i<clusterCount;i++){
        // --- 从候选列表取第 i 个 Cluster 的「地址」---
        // 固定从 1024 起：前面 [0..1023] 留给 Pass 2 的 BVH 节点栈
        uint pageIndex=MainAndPostNodeAndClusterBatches.mData[1024+i*2];
        uint clusterIndex=MainAndPostNodeAndClusterBatches.mData[1024+i*2+1];
        // --- Layer 1：解包 NaniteMesh 里该 Cluster 的包围球与 LOD 元数据 ---
        ClusterInfo clusterInfo=GetClusterInfo(pageIndex,clusterIndex);
        // --- 把包围球中心变到世界空间，再变到「以相机为原点」的坐标系 ---
        // 半径在世界空间各向同性缩放时可不变，仍用模型空间半径
        vec3 boundingSphere=clusterInfo.mLODBounds.xyz;
        vec4 boundingSphereWS=ModelMatrix*vec4(boundingSphere,1.0f);
        boundingSphereWS.xyz=boundingSphereWS.xyz-CameraPositionWS.xyz;
        // --- Layer 2：算屏幕投影尺度，与 LOD 阈值比较 ---
        vec2 projectionScales=GetProjectionScales(vec4(boundingSphereWS.xyz,clusterInfo.mLODBounds.w));
        float lodScale=CameraPositionWS.w;       // LODScale：世界 LOD 误差 → 屏幕像素，参与下方 if 判据
        // LODScaleHW 仅保持与 UE UBO 布局一致；本 Demo 无 SW/HW 分流，故读取后不使用
        float lodScaleHW=ViewDirectionWS.w;
        // 判定式（Pass 3 核心）：
        //   projectionScales.x  = 该 Cluster 在屏幕上大概占多少像素（尺度）
        //   mLODError * lodScale = 允许的最小显示尺度（再粗就不画了）
        //   左边 > 右边  →  屏幕上够大，值得画
        if(projectionScales.x>clusterInfo.mLODError*lodScale){
            VisiableClusterSWHW.mData[visiableClusterCount]=uvec2(pageIndex,clusterIndex);
            visiableClusterCount++;
        }
    }
    // 覆盖 mData[1]：从「候选数」改为「可见数」→ 下一段 HWRasterize DrawIndirect 的 instanceCount
    IndirectWorkArgs.mData[1]=visiableClusterCount;
}
