#pragma once
#include <windows.h>
#include "GL/glew.h"
#ifdef _WIN32
#include "GL/wglew.h"
#endif
namespace Alice {
	void InitExtensions();
	bool SupportExtension(const char* inExtensionName);
	const char* LikelyToSupportExtension(const char* inExtension);
}
// ==============================================================================
// GlobalConstants — UBO binding=0，所有 Nanite Pass 共用
//
// 内存布局必须与 GLSL layout(binding=0) uniform GlobalConstants 逐字段一致。
// 字段语义与 LOD 用法见 doc/Code_Reading_Guide.md §3.3
//
// WorkArgs (SSBO，非本 struct) 常用下标速查：
//   mData[0]=384  vertexCount   mData[1]=instanceCount（可见 Cluster 数）
//   mData[5]=nodeListOffset     mData[6]=nodeCount
// ==============================================================================
struct GlobalConstants {
	union {
		struct {
			float mProjectionMatrix[16];  // 透视投影矩阵 (Projection Matrix)
			float mViewMatrix[16];        // 视图/相机矩阵 (View Matrix)
			float mModelMatrix[16];       // 模型世界变换矩阵 (Model Matrix)
			unsigned int mMisc0[4];       // 杂项控制向量 (x: 当前 Mip/LOD 索引)
			// mCameraPositionWS — GLSL: vec4 CameraPositionWS（名字易误解，见下）
			//   [0..2] xyz：相机世界空间位置；Shader 剔除前会减到「相机相对坐标」
			//   [3]    w  ：LODScale（实际参与 LOD 判据，非位置分量）
			//              把 mesh 里世界空间 LOD 误差换算成屏幕像素尺度
			//              CPU 每帧：LODScale = ViewToPixels = 0.5 * Proj[1][1] * 屏幕高度
			//              Pass2: projectionScales.x <= MaxParentLODError * LODScale
			//              Pass3: projectionScales.x >  mLODError * LODScale
			float mCameraPositionWS[4];
			// mViewDirectionWS — GLSL: vec4 ViewDirectionWS
			//   [0..2] xyz：相机前向单位向量（GetProjectionScales 里作视线轴，有实际用途）
			//   [3]    w  ：LODScaleHW = ViewToPixels/32
			//              仅保持与 UE Nanite GlobalConstants 布局一致；本 Demo 无 SW/HW 分流，Shader 未参与判据
			float mViewDirectionWS[4];
		};
		float mData[1024];               // 填充保证满足 4KB UBO 对齐标准
	};
	void SetProjectionMatrix(const float* inMatrix);
	void SetViewMatrix(float* inMatrix);
	void SetModelMatrix(const float* inMatrix);
	void SetMisc0(unsigned int x, unsigned int y, unsigned int z, unsigned int w);
	// inX/Y/Z=相机位置；inW=LODScale（Pass2/3 LOD 误差换算，写入 mCameraPositionWS[3]）
	void SetCameraPositionWS(float inX, float inY, float inZ, float inW = 0.0f);
	// inX/Y/Z=视线方向；inW=LODScaleHW（布局占位，写入 mViewDirectionWS[3]，当前 Shader 未用）
	void SetCameraViewDirectionWS(float inX, float inY, float inZ, float inW = 0.0f);
};

// ==============================================================================
// 2D 纹理对象封装结构 (Texture2D)
// 包含 OpenGL 纹理句柄、内部格式以及分辨率尺寸
// ==============================================================================
struct Texture2D {
	GLuint mTexture; // OpenGL 纹理对象句柄 ID (由 glGenTextures 生成)
	GLenum mFormat;  // 纹理数据格式 (例如 GL_RGBA32F, GL_RGBA 等)
	int mWidth;      // 纹理宽度 (像素)
	int mHeight;     // 纹理高度 (像素)
};

GLuint CompileShader(GLenum shaderType, const char* shaderCode);
GLuint CreateProgram(GLuint vsShader, GLuint fsShader);
GLuint CreateProgram(GLuint csShader);
GLuint CreateBufferObject(GLenum bufferType, GLsizeiptr size, GLenum usage, void* data = nullptr);
void UpdateBufferObject(GLuint object, GLenum type, void* data, int size, int offset);
Texture2D* CreateTexture2D(unsigned char* pixelData, int width, int height, GLenum gpu_format = GL_RGB, GLenum cpu_format = GL_RGB,
	GLenum wrapMode = GL_CLAMP_TO_EDGE, GLenum minFilter = GL_LINEAR, GLenum magFilter = GL_LINEAR);
void SetObjectName(GLenum inType, GLuint inObject, const char* inName);

// ==============================================================================
// 作用域调试事件标签 (ScopedEvent)
// 利用 RAII 机制自动标记 OpenGL Debug 组 (glPushDebugGroup / glPopDebugGroup)，
// 便于在 RenderDoc 或 NSight Graphics 中层次化精准查看每个 Pass 的性能与绘图指令
// ==============================================================================
struct ScopedEvent {
	ScopedEvent(LPCSTR inName) {
		glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, inName);
	}
	~ScopedEvent() {
		glPopDebugGroup();
	}
};
#define EVENT_VAR_INNER(inName, line) _scopedEvent_##line(inName)
#define EventVar(inName,n) EVENT_VAR_INNER(inName, n)
#define SCOPED_EVENT(inName) \
        ScopedEvent EventVar(inName,__LINE__)

// ==============================================================================
// OpenGL 错误检查与断言机制
// ==============================================================================
void CheckLastOpenGLError(const char* prefix, const char* file, long line, const char* operation);
#define GLAssert(x) 	{ CheckLastOpenGLError (NULL,__FILE__, __LINE__,#x); }
#define OGL_CALL(x) do { x; GLAssert(x); } while(0)