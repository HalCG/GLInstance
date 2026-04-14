// ==============================================================================
// Visualization.glsl — Pass 5：VisBuffer64 → RGB 伪彩纹理
//
// 作用：把 Pass4 的 64 位可见性缓冲解码为 RGB，供 Pass6 FSQ 上屏（非真实材质，仅调试伪彩）。
// 数据流：VisBuffer64[pixel] → 低 32 位 Payload → clusterIndex → MurmurHash → imageStore
// 工作组 8×8；分辨率 1280×720（外提见 Code_Optimization_TODO.md）
// VisBuffer 布局见 HWRasterizeFS.glsl 附录
// ==============================================================================
#version 450
#extension GL_ARB_gpu_shader_int64 : enable

layout(local_size_x=8,local_size_y=8,local_size_z=1)in;

// binding=0：Pass4 atomicMin 写入的 VisBuffer64（只读）
layout(binding=0,std430)buffer FVisBuffer64{
    uint64_t mData[];
}VisBuffer64;

// binding=0（image）：RGBA32F 输出，Pass6 FSQ 采样
layout(binding=0,rgba32f)uniform image2D VisualizationTexture;

// MurmurHash3 整数混淆：clusterIndex → 分布均匀的哈希（仅伪彩，非加密）
uint MurmurMix(uint Hash)
{
	Hash ^= Hash >> 16;
	Hash *= 0x85ebca6b;
	Hash ^= Hash >> 13;
	Hash *= 0xc2b2ae35;
	Hash ^= Hash >> 16;
	return Hash;
}

// 同一 Index 固定同色；相邻 Index 颜色尽量拉开
vec3 IntToColor(uint Index)
{
	uint Hash = MurmurMix(Index);
	vec3 Color = vec3(
		(Hash >>  0) & 255,
		(Hash >>  8) & 255,
		(Hash >> 16) & 255
	);
	return Color * (1.0f / 255.0f);
}

// main — 每线程一个像素（8×8 工作组铺满屏幕）
void main(){
    ivec2 texcoord=ivec2(gl_GlobalInvocationID.xy);

    if(any(greaterThanEqual(texcoord,ivec2(1280,720)))){
        return;
    }

    int pixelIndex=texcoord.x+texcoord.y*1280;

    uint64_t pixelValue64=VisBuffer64.mData[pixelIndex];
    uint pixelValue=uint(pixelValue64);  // 低 32 位 Payload；高 32 位深度本 Pass 不用

    vec3 color=vec3(0.1f,0.4f,0.6f);  // 背景：payload==0

    if(pixelValue!=0){
        // 仅用页内 clusterIndex（低 8 位）；pageIndex=pixelValue>>8 未参与伪彩
        uint clusterIndex=(pixelValue&0xFFu) - 1;  // VS 写入 clusterIndex+1
        color = IntToColor(clusterIndex);
        color = color * 0.8 + 0.2;
    }

    imageStore(VisualizationTexture,texcoord,vec4(color,1.0f));
}
// ==============================================================================
// 附录：pixelValue = (pageIndex<<8)|(clusterIndex+1)
//   0 → 背景；否则颜色由 (pixelValue&0xFFu)-1 唯一决定
// ==============================================================================
