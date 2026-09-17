#include "oglcontext.h"
#include <unordered_set>
#include <string>
#pragma comment(lib,"opengl32.lib")
#pragma comment(lib,"glew32.lib")
#pragma comment(lib,"glu32.lib")
namespace Alice {
	static std::unordered_set<std::string> sSupportedExtensions;
	void InitExtensions() {
		GLint numExtensions;
		glGetIntegerv(GL_NUM_EXTENSIONS, &numExtensions);
		//printf("OpenGL Extensions Num : %d\n", numExtensions);
		for (int i = 0; i < numExtensions; i++) {
			const char* extension = (char*)glGetStringi(GL_EXTENSIONS, i);
			//printf("Extension [%d] : [%s]\n", i, extension);
			sSupportedExtensions.insert(extension);
		}
	}
	bool SupportExtension(const char* inExtensionName) {
		return sSupportedExtensions.find(inExtensionName) != sSupportedExtensions.end();
	}
	const char* LikelyToSupportExtension(const char* inExtension) {
		auto iter = sSupportedExtensions.begin();
		auto iterEnd = sSupportedExtensions.end();
		while (iter !=iterEnd)
		{
			if ((*iter).find(inExtension, 0) != std::string::npos) {
				return (*iter).c_str();
			}
			iter++;
		}
		return nullptr;
	}
}
void GlobalConstants::SetProjectionMatrix(const float* inMatrix) {
	memcpy(mProjectionMatrix, inMatrix, sizeof(mProjectionMatrix));
}
void GlobalConstants::SetViewMatrix(float* inMatrix) {
	inMatrix[12] = 0.0f;
	inMatrix[13] = 0.0f;
	inMatrix[14] = 0.0f;
	memcpy(mViewMatrix, inMatrix, sizeof(mViewMatrix));
}
void GlobalConstants::SetModelMatrix(const float* inMatrix) {
	memcpy(mModelMatrix, inMatrix, sizeof(mModelMatrix));
}
void GlobalConstants::SetMisc0(unsigned int x, unsigned int y, unsigned int z, unsigned int w) {
	mMisc0[0] = x;
	mMisc0[1] = y;
	mMisc0[2] = z;
	mMisc0[3] = w;
}

// 打包进 UBO：xyz=位置，w=LODScale（UE 风格 vec4 复用，见 oglcontext.h GlobalConstants 注释）
void GlobalConstants::SetCameraPositionWS(float inX, float inY, float inZ, float inW) {
	mCameraPositionWS[0] = inX;
	mCameraPositionWS[1] = inY;
	mCameraPositionWS[2] = inZ;
	mCameraPositionWS[3] = inW;  // LODScale，非世界坐标 w 分量
}
void GlobalConstants::SetCameraViewDirectionWS(float inX, float inY, float inZ, float inW) {
	mViewDirectionWS[0] = inX;
	mViewDirectionWS[1] = inY;
	mViewDirectionWS[2] = inZ;
	mViewDirectionWS[3] = inW;  // LODScaleHW：仅 UBO 布局占位，本 Demo 不参与 LOD 判据
}
void SetObjectName(GLenum inType, GLuint inObject, const char* inName) {
	// 为 OpenGL 对象设置名称，以便在 RenderDoc 或调试器中清晰辨认
	glObjectLabel(inType, inObject, -1, inName);
}
// ==============================================================================
// 编译单个着色器并进行错误检查
// ==============================================================================
GLuint CompileShader(GLenum shaderType, const char* shaderCode) {
	GLuint shader = 0;
	OGL_CALL(shader = glCreateShader(shaderType));
	OGL_CALL(glShaderSource(shader, 1, &shaderCode, nullptr));
	OGL_CALL(glCompileShader(shader));
	GLint compileResult = GL_TRUE;
	OGL_CALL(glGetShaderiv(shader, GL_COMPILE_STATUS, &compileResult));
	if (compileResult == GL_FALSE) {
		char szLog[10240] = { 0 };
		GLsizei logLen = 0;
		OGL_CALL(glGetShaderInfoLog(shader, 10240, &logLen, szLog));
		printf("Compile Shader fail error log : %s \nshader code :\n%s\n", szLog, shaderCode);
		OGL_CALL(glDeleteShader(shader));
		shader = 0;
	}
	return shader;
}
GLuint CreateProgram(GLuint vsShader, GLuint fsShader) {
	GLuint program = 0;
	OGL_CALL(program = glCreateProgram());
	OGL_CALL(glAttachShader(program, vsShader));
	OGL_CALL(glAttachShader(program, fsShader));
	OGL_CALL(glLinkProgram(program));
	OGL_CALL(glDetachShader(program, vsShader));
	OGL_CALL(glDetachShader(program, fsShader));
	GLint nResult;
	OGL_CALL(glGetProgramiv(program, GL_LINK_STATUS, &nResult));
	if (nResult == GL_FALSE) {
		char log[10240] = { 0 };
		GLsizei writed = 0;
		OGL_CALL(glGetProgramInfoLog(program, 10240, &writed, log));
		printf("create gpu program fail,link error : %s\n", log);
		OGL_CALL(glDeleteProgram(program));
		program = 0;
	}
	return program;
}
GLuint CreateProgram(GLuint csShader) {
	GLuint program = 0;
	OGL_CALL(program = glCreateProgram());
	OGL_CALL(glAttachShader(program, csShader));
	OGL_CALL(glLinkProgram(program));
	OGL_CALL(glDetachShader(program, csShader));
	GLint nResult;
	OGL_CALL(glGetProgramiv(program, GL_LINK_STATUS, &nResult));
	if (nResult == GL_FALSE) {
		char log[10240] = { 0 };
		GLsizei writed = 0;
		OGL_CALL(glGetProgramInfoLog(program, 10240, &writed, log));
		printf("create gpu program fail,link error : %s\n", log);
		OGL_CALL(glDeleteProgram(program));
		program = 0;
	}
	return program;
}
// ==============================================================================
// 辅助函数：创建/更新 缓冲区对象 (Buffer Object)
// ==============================================================================
GLuint CreateBufferObject(GLenum bufferType, GLsizeiptr size, GLenum usage, void* data /* = nullptr */) {
	GLuint object;
	OGL_CALL(glGenBuffers(1, &object));
	OGL_CALL(glBindBuffer(bufferType, object));
	OGL_CALL(glBufferData(bufferType, size, data, usage));
	OGL_CALL(glBindBuffer(bufferType, 0));
	return object;
}
void UpdateBufferObject(GLuint object, GLenum type, void* data, int size, int offset) {
	OGL_CALL(glBindBuffer(type, object));
	OGL_CALL(glBufferSubData(type, offset, size, data));//cpu -> gpu
	OGL_CALL(glBindBuffer(type, 0));
}
Texture2D* CreateTexture2D(unsigned char* pixelData, int width, int height, GLenum gpu_format, GLenum cpu_format, GLenum wrapMode, GLenum minFilter, GLenum magFilter) {
	Texture2D*texture=new Texture2D;
	GLenum basicType = GL_UNSIGNED_BYTE;
	if (gpu_format == GL_RGBA32F) {
		basicType = GL_FLOAT;
	}
	OGL_CALL(glGenTextures(1, &texture->mTexture));
	OGL_CALL(glBindTexture(GL_TEXTURE_2D, texture->mTexture));
	OGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapMode));
	OGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapMode));
	OGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter));
	OGL_CALL(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter));
	OGL_CALL(glTexImage2D(GL_TEXTURE_2D, 0, gpu_format, width, height, 0, cpu_format, basicType, pixelData));
	OGL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
	texture->mFormat = gpu_format;
	texture->mWidth = width;
	texture->mHeight = height;
	return texture;
}
void CheckLastOpenGLError(const char* prefix, const char* file, long line, const char* operation) {
	GLenum glerr;
	while ((glerr = glGetError()) != GL_NO_ERROR) {
		switch (glerr) {
		case GL_INVALID_VALUE:
			printf("%s GL_INVALID_VALUE\n", operation);
			break;
		default:
			printf("gl error  0x%x\n", (int)glerr);
			break;
		}
		std::string str = file;
		const int kMaxErrors = 10;
		int counter = 0;
		int pos = str.find_last_of('\\');
		printf("%s:%ld :", str.substr(pos + 1, str.length() - pos).c_str(), line);
		if (prefix) {
			std::string errorString = prefix;
			errorString += ": ";
			const char* gluMsg = reinterpret_cast<const char*>(gluErrorString(glerr));
			if (gluMsg) {
				printf("prefix error  %s\n", gluMsg);

			}
			else {
				printf("prefix error : unkown error 0x%x\n", glerr);
			}
		}
		else {
			const char* gluMsg = reinterpret_cast<const char*>(gluErrorString(glerr));
			if (gluMsg) {
				printf("%s %s\n", operation, gluMsg);
			}
			else {
				printf("%s 0x%x\n", operation, glerr);
			}
		}
		++counter;
		if (counter > kMaxErrors) {
			printf("GL: error count exceeds %i, stop reporting errors\n", kMaxErrors);
			return;
		}
	}
}