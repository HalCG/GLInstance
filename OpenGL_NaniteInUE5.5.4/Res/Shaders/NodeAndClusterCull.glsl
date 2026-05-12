// ==============================================================================
// NodeAndClusterCull.glsl — Pass 2：四叉 BVH 层级遍历与候选 Cluster 收集
//
// 管线位置：RasterClear 之后，固定执行 4 次 Ping-Pong（scene.cpp 循环）
//   读 CurrentIndirectWorkArgs (sWorkArgs[i%2])
//   写 NextIndirectWorkArgs     (sWorkArgs[(i+1)%2])
//
// 职责：
//   1. 从 Batches[mData[nodeListOffset .. nodeListOffset+nodeCount-1]] 取当前层节点 ID
//   2. 对每个节点的 4 个子分支做 LOD 判据（ShouldVisiteChild）
//   3. 叶子且通过判据 → 展开 NumChildren 个 Cluster 写入 Batches[1024+]（候选列表）
//   4. 非叶且通过判据 → 子节点 ID 写入下一层节点栈，更新 mData[5]/[6]
//
// 阅读分层（见 doc/Code_Reading_Guide.md §5.1）：
//   Layer 1 — UnpackHierarchyNodeSlice / GetHierarchyNodeSlice
//   Layer 2 — GetProjectionScales / ShouldVisiteChild
//   Layer 3 — main() 双层循环
//
// LOD 判据：projectionScales.x <= MaxParentLODError * LODScale → 误差已足够小，可停止向下
// ==============================================================================
#version 450
layout(local_size_x=1,local_size_y=1,local_size_z=1)in;

// ------------------------------------------------------------------------------
// UE Nanite BVH 位域常量（与引擎打包格式一致，解包见 UnpackHierarchyNodeSlice）
// ------------------------------------------------------------------------------
#define NANITE_MAX_BVH_NODE_FANOUT_BITS		2
#define NANITE_MAX_BVH_NODE_FANOUT_MASK		((1 << NANITE_MAX_BVH_NODE_FANOUT_BITS)-1)
#define NANITE_MAX_BVH_NODE_FANOUT			(1 << NANITE_MAX_BVH_NODE_FANOUT_BITS)
#define NANITE_BVH_NODE_ENABLE_MASK			((1 << NANITE_MAX_BVH_NODE_FANOUT)-1)

#define NANITE_MAX_CLUSTERS_PER_GROUP_BITS	9
#define NANITE_MAX_CLUSTERS_PER_GROUP_MASK	((1 << NANITE_MAX_CLUSTERS_PER_GROUP_BITS) - 1)
#define NANITE_MAX_CLUSTERS_PER_GROUP		((1 << NANITE_MAX_CLUSTERS_PER_GROUP_BITS) - 1)

#define NANITE_MAX_GROUP_PARTS_BITS							5
#define NANITE_MAX_GROUP_PARTS_MASK							((1 << NANITE_MAX_GROUP_PARTS_BITS) - 1)
#define NANITE_MAX_GROUP_PARTS								(1 << NANITE_MAX_GROUP_PARTS_BITS)

#define NANITE_MAX_RESOURCE_PAGES_BITS						16
#define NANITE_MAX_RESOURCE_PAGES_MASK						((1 << NANITE_MAX_RESOURCE_PAGES_BITS) - 1)
#define NANITE_MAX_RESOURCE_PAGES							(1 << NANITE_MAX_RESOURCE_PAGES_BITS)

layout(binding=0)uniform GlobalConstants {
    mat4 ProjectionMatrix;
    mat4 ViewMatrix;
    mat4 ModelMatrix;
    uvec4 Misc0;
    vec4 CameraPositionWS;   // xyz=相机世界坐标；w=LODScale（ShouldVisiteChild 里参与 LOD 判据）
    vec4 ViewDirectionWS;    // xyz=相机前向（GetProjectionScales 用）；w=LODScaleHW（布局占位，本 Pass 不读）
};

// 磁盘 BVH 中单个节点的 GPU 视图（4 个子分支打包为一组）
struct FPackedHierarchyNode{
    uvec4 LODBounds[NANITE_MAX_BVH_NODE_FANOUT];
    uvec4 Misc0[NANITE_MAX_BVH_NODE_FANOUT];
    uvec4 Misc1[NANITE_MAX_BVH_NODE_FANOUT];
    uint Misc2[NANITE_MAX_BVH_NODE_FANOUT];
};

layout(binding=1,std430)buffer FBVH{
    FPackedHierarchyNode mData[];
}BVH;

// binding=2：本 Pass 输入调度状态（scene.cpp 每轮 Pass2 绑定 sWorkArgs[i%2]）
layout(binding=2,std430)buffer FCurrentIndirectWorkArgs{
    uint mData[];
}CurrentIndirectWorkArgs;
// binding=3：本 Pass 输出调度状态（写入 sWorkArgs[(i+1)%2]，下一轮或 ClusterCull 读取）
layout(binding=3,std430)buffer FNextIndirectWorkArgs{
    uint mData[];
}NextIndirectWorkArgs;
// binding=4：双用途大数组 sMainAndPostNodeAndClusterBatches（见 RasterClear / 导读 §4.5）
//   mData[0 .. 1023]     当前层/下一层 BVH 节点 ID 栈（由 mData[5] offset + mData[6] count 描述一段）
//   mData[1024 ..]       候选 Cluster 列表，每条 2 个 uint：(pageIndex, clusterIndex) 交错存储
//   本 Pass 只追加写入，不擦除前面层留下的节点 ID 与已收集的 Cluster
layout(binding=4,std430)buffer FMainAndPostNodeAndClusterBatches{
    uint mData[];
}MainAndPostNodeAndClusterBatches;

// 单个 BVH 子分支解包后的可读结构
struct FHierarchyNodeSlice
{
	vec4	LODBounds;
	vec3	BoxBoundsCenter;
	vec3	BoxBoundsExtent;
	float	MinLODError;
	float	MaxParentLODError;
	uint	ChildStartReference;
	uint	NumChildren;
	uint	StartPageIndex;
	uint	NumPages;
	bool	bEnabled;
	bool	bLoaded;
	bool	bLeaf;
};

uint BitFieldExtractU32(uint Data, uint Size, uint Offset)
{
	Size &= 31;
	Offset &= 31;
	return (Data >> Offset) & ((1u << Size) - 1u);
}

// Layer 1：将 FPackedHierarchyNode 的一个子分支 Raw 数据解包为 FHierarchyNodeSlice
FHierarchyNodeSlice UnpackHierarchyNodeSlice(uvec4 RawData0, uvec4 RawData1, uvec4 RawData2, uint RawData3)
{
	const uvec4 Misc0 = RawData1;
	const uvec4 Misc1 = RawData2;
	const uint  Misc2 = RawData3;

	FHierarchyNodeSlice Node;
	Node.LODBounds				= uintBitsToFloat(RawData0);

	Node.BoxBoundsCenter		= uintBitsToFloat(Misc0.xyz);
	Node.BoxBoundsExtent		= uintBitsToFloat(Misc1.xyz);

    vec2 unpackedData           = unpackHalf2x16(Misc0.w);
	Node.MinLODError			= unpackedData.x;
	Node.MaxParentLODError		= unpackedData.y;
	Node.ChildStartReference	= Misc1.w;
	Node.bLoaded				= (Misc1.w != 0xFFFFFFFFu);

	Node.NumChildren			= BitFieldExtractU32(Misc2, NANITE_MAX_CLUSTERS_PER_GROUP_BITS, 0);
	Node.NumPages				= BitFieldExtractU32(Misc2, NANITE_MAX_GROUP_PARTS_BITS, NANITE_MAX_CLUSTERS_PER_GROUP_BITS);
	Node.StartPageIndex			= BitFieldExtractU32(Misc2, NANITE_MAX_RESOURCE_PAGES_BITS, NANITE_MAX_CLUSTERS_PER_GROUP_BITS + NANITE_MAX_GROUP_PARTS_BITS);
	Node.bEnabled				= Misc2 != 0u;
	Node.bLeaf					= Misc2 != 0xFFFFFFFFu;

	return Node;
}

FHierarchyNodeSlice GetHierarchyNodeSlice(uint NodeIndex, uint ChildIndex)
{
    FPackedHierarchyNode packedData=BVH.mData[NodeIndex];
    const uvec4 RawData0=packedData.LODBounds[ChildIndex];
    const uvec4 RawData1=packedData.Misc0[ChildIndex];
    const uvec4 RawData2=packedData.Misc1[ChildIndex];
    const uint RawData3=packedData.Misc2[ChildIndex];
	return UnpackHierarchyNodeSlice(RawData0, RawData1, RawData2, RawData3);
}

// Layer 2：屏幕投影尺度（输入为相机相对空间的包围球）
vec2 GetProjectionScales(vec4 Bounds)
{
	if( ProjectionMatrix[3][3] >= 1.0f )
	{
		return vec2(1,1);
	}
	vec3 Center = Bounds.xyz;
	float Radius = Bounds.w;

	float ZNear = 10.0f;
	float DistToClusterSq = dot(Center,Center);

	float Z = dot(ViewDirectionWS.xyz, Center);
	float XSq = DistToClusterSq - Z * Z;
	float X = sqrt( max(0.0f, XSq) );
	float DistToTSq = DistToClusterSq - Radius * Radius;
	float DistToT = sqrt( max(0.0f, DistToTSq) );
	float ScaledCosTheta = DistToT;
	float ScaledSinTheta = Radius;
	float ScaleToUnit = 1.0f/DistToClusterSq;
	float By = (  ScaledSinTheta * X + ScaledCosTheta * Z ) * ScaleToUnit;
	float Ty = ( -ScaledSinTheta * X + ScaledCosTheta * Z ) * ScaleToUnit;

	float H = ZNear - Z;

	float MinZ = max( Z - Radius, ZNear );
	float MaxZ = max( Z + Radius, ZNear );
	float MinCosAngle = Ty;
	float MaxCosAngle = By;

	if(Z + Radius > ZNear)
		return vec2( MinZ * MinCosAngle, MaxZ * MaxCosAngle );
	else
		return vec2( 0.0f, 0.0f );
}

// Layer 2：true = 屏幕误差已足够小，应处理该子分支（收集叶子或继续向下）
bool ShouldVisiteChild(FHierarchyNodeSlice inHierarchyNodeSlice){
	vec3 boundingSphere=inHierarchyNodeSlice.LODBounds.xyz;
	vec4 boundingSphereWS=ModelMatrix*vec4(boundingSphere,1.0f);
    boundingSphereWS.xyz=boundingSphereWS.xyz-CameraPositionWS.xyz;

	vec2 projectionScales=GetProjectionScales(vec4(boundingSphereWS.xyz,inHierarchyNodeSlice.LODBounds.w));
	// LODScale（CameraPositionWS.w）：世界空间 MaxParentLODError → 屏幕像素阈值
	float lodScale=CameraPositionWS.w;
	float threshold=lodScale*inHierarchyNodeSlice.MaxParentLODError;

	if(projectionScales.x<=threshold){
		return true;
	}
	return false;
}

// ==============================================================================
// main — Layer 3：一层 BVH 广度扩展（本文件每帧被 dispatch 4 次，见 scene.cpp 循环）
// 单次 dispatch 只做「当前层 → 下一层」一步，不递归进 GPU 调用栈。
// 图示：doc/figures/BVH_NaniteMesh/02_bvh_quadtree.svg、07_pipeline_flow.svg
// ==============================================================================
void main(){
	// --- 从 Current WorkArgs 读取本层遍历状态（上一轮 Pass2 或 RasterClear 写入）---
	// 已累计写入 Batches[1024+] 的候选 Cluster 条数；4 轮 Ping-Pong 累加，不清零
	uint currentClusterCount=CurrentIndirectWorkArgs.mData[1];
	// 本层节点 ID 列表在 Batches 里的起始下标（首帧 offset=0 → 读 Batches[0] 根节点）
	uint currentNodeOffset=CurrentIndirectWorkArgs.mData[5];
	// 本层待遍历 BVH 节点个数（首帧为 1，只从根出发）
	uint currentNodeCount=CurrentIndirectWorkArgs.mData[6];
	// 下一层节点 ID 紧接在本层段之后追加，形成 Batches 里的「分层栈」
	uint nextCengNodeIndexOffset=currentNodeOffset+currentNodeCount;
	// 本 dispatch 新产生的下一层节点个数（写入 Next mData[6]）
	uint nextCengNodeCount=0;
	for(uint i=0;i<currentNodeCount;i++){
		// 本层第 i 个待处理节点的 BVH 编号（索引 BVH.mData[nodeIndex]）
		uint currentNodeIndex=MainAndPostNodeAndClusterBatches.mData[currentNodeOffset+i];
		// 内层 j：固定 4 子槽位（四叉 fanout），不是「4 个 Cluster」；空槽 bEnabled=false
		for(int j=0;j<4;j++){
			FHierarchyNodeSlice slice=GetHierarchyNodeSlice(currentNodeIndex,j);
			bool shouldVisiteChild=ShouldVisiteChild(slice);
			// LOD 通过：屏幕误差已足够小，可停在该子树（与 Pass3 比较方向相反，导读 §4.5）
			if(shouldVisiteChild&&slice.bEnabled){
				if(slice.bLeaf){
					// === 叶子：ChildStartReference 指向 NaniteMesh（图示 03_child_start_reference.svg）===
					// 本叶子展开 Cluster 个数（可 >4，上限约 511；与 fanout 4 无关）
					uint clusterCountInThisLeaf=slice.NumChildren;
					// 资源页号 → NaniteMesh.mData[1+pageIndex]
					uint pageIndex=slice.ChildStartReference>>8;
					// 页内起始 cluster 下标；第 k 个候选 = clusterOffsetInThisPage + k
					uint clusterOffsetInThisPage=slice.ChildStartReference&0xFFu;
					for(int index=0;index<clusterCountInThisLeaf;index++){
						// 候选列表从 1024 起，每条 2 个 uint，供 Pass3 ClusterCull 读取
						MainAndPostNodeAndClusterBatches.mData[1024+currentClusterCount*2]=pageIndex;
						MainAndPostNodeAndClusterBatches.mData[1024+currentClusterCount*2 + 1]
							= clusterOffsetInThisPage + index;
						currentClusterCount++;
					}
				}else{
					// === 非叶：ChildStartReference = 子 BVH 节点 ID（与 page/cluster 无关）===
					// 通过 LOD 的子树继续向下，子节点 ID 追加到下一层段
					MainAndPostNodeAndClusterBatches.mData[nextCengNodeIndexOffset+nextCengNodeCount]
						= slice.ChildStartReference;
					nextCengNodeCount++;
				}
			}
		}
	}
	// --- 写出 Next WorkArgs：下一轮 Pass2 或 Pass3 读候选数 ---
	// mData[1]：候选 Cluster 总数（4 次 Pass2 后 ClusterCull 读此值）
	NextIndirectWorkArgs.mData[1]=currentClusterCount;
	// mData[5]：下一轮「当前层」节点列表起始 offset
	NextIndirectWorkArgs.mData[5]=nextCengNodeIndexOffset;
	// mData[6]：下一轮待遍历节点数；为 0 则后续 Pass2 dispatch 空转
	NextIndirectWorkArgs.mData[6]=nextCengNodeCount;
}
// ==============================================================================
// 附录：叶子节点 ChildStartReference 位打包（仅 bLeaf 时；非叶为子 BVH 节点 ID，不拆分）
//   ChildStartReference 是 32-bit uint，编码 (pageIndex, 页内起始 clusterIndex)：
//     pageIndex              = ChildStartReference >> 8;   // 高 24 bit → NaniteMesh 页号
//     clusterOffsetInThisPage  = ChildStartReference & 0xFFu; // 低 8 bit → 页内 cluster 起始下标
//   & 0xFFu：掩码 0xFF=bit0~7 全 1，按位与后清掉高位，只保留最低一字节；0xFFu 表无符号 uint
//   例：0x00000108 → pageIndex=1，cluster 从 8 起；NumChildren=2 则写 (1,8)(1,9)
//   图示：doc/figures/BVH_NaniteMesh/03_child_start_reference.svg
// ==============================================================================
