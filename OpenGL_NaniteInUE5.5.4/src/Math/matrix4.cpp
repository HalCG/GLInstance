#include "matrix4.h"
#include<math.h>
#include<string.h>

// ==============================================================================
// 加载为单位矩阵 (3x3)
// ==============================================================================
void matrix3::LoadIdentity() {
	memset(v, 0, sizeof(v));
	_11 = 1.0f;
	_22 = 1.0f;
	_33 = 1.0f;
}

// ==============================================================================
// 设置为缩放矩阵 (3x3)
// ==============================================================================
void matrix3::SetScale(float inX, float inY, float inZ) {
	_11 = inX;
	_22 = inY;
	_33 = inZ;
}
void matrix3::operator=(const matrix3& inRightValue) {
	memcpy(v, inRightValue.v, sizeof(v));
}

// ==============================================================================
// 3x3 矩阵乘法
// ==============================================================================
matrix3 matrix3::operator*(const matrix3& inRightValue)const {
	matrix3 ret;
	ret._11 = _11 * inRightValue._11 + _12 * inRightValue._21 + _13 * inRightValue._31;
	ret._12 = _11 * inRightValue._12 + _12 * inRightValue._22 + _13 * inRightValue._32;
	ret._13 = _11 * inRightValue._13 + _12 * inRightValue._23 + _13 * inRightValue._33;

	ret._21 = _21 * inRightValue._11 + _22 * inRightValue._21 + _23 * inRightValue._31;
	ret._22 = _21 * inRightValue._12 + _22 * inRightValue._22 + _23 * inRightValue._32;
	ret._23 = _21 * inRightValue._13 + _22 * inRightValue._23 + _23 * inRightValue._33;

	ret._31 = _31 * inRightValue._11 + _32 * inRightValue._21 + _33 * inRightValue._31;
	ret._32 = _31 * inRightValue._12 + _32 * inRightValue._22 + _33 * inRightValue._32;
	ret._33 = _31 * inRightValue._13 + _32 * inRightValue._23 + _33 * inRightValue._33;
	return ret;
}

// ==============================================================================
// 3x3 矩阵转置
// ==============================================================================
void matrix3::Transpose() {
	float temp = _12;
	_12 = _21;
	_21 = temp;
	temp = _13;
	_13 = _31;
	_31 = temp;
	temp = _23;
	_23 = _32;
	_32 = temp;
}

// ==============================================================================
// 3x3 矩阵行列式计算
// ==============================================================================
float matrix3::Determinant()const {
	float _1 = _11 * (_22 * _33 - _32 * _23);
	float _2 = -_12 * (_21 * _33 - _31 * _23);
	float _3 = _13 * (_21 * _32 - _31 * _22);
	return _1 + _2 + _3;
}

// ==============================================================================
// 构建透视投影矩阵
// 参数：FOV(角度制), 屏幕宽高比(Aspect), 近裁剪面(Near), 远裁剪面(Far)
// ==============================================================================
void matrix4::Perspective(float inFOVInAngle, float inAspect, float inNear, float inFar) {
	memset(v, 0, sizeof(v));
	float halfAngleInRadians = inFOVInAngle * 0.5f * 3.1415926f / 180.0f;
	float R = tanf(halfAngleInRadians) * inNear;
	float T = R / inAspect;
	_11 = inNear / R;
	_22 = inNear / T;
	_33 = (inNear+inFar) / (inNear-inFar);
	_43 = (2.0f*inNear*inFar) / (inNear - inFar);
	_34 = -1.0f;
}

// ==============================================================================
// 构建视图矩阵 (LookAt)
// 根据相机位置、目标点以及上方向(HelpVector)构建视图坐标系并转换为矩阵
// ==============================================================================
void matrix4::LookAt(const float4& inCameraPosition,
	const float4& inTargetPosition, const float4& inHelpVector) {
	float4 z = inCameraPosition - inTargetPosition;
	z.Normalize();
	float4 x = cross(inHelpVector, z);
	x.Normalize();
	float4 y = cross(z, x);
	y.Normalize();

	matrix4 rotateMatrix;
	rotateMatrix.LoadIdentity();
	rotateMatrix._11 = x.x;
	rotateMatrix._12 = x.y;
	rotateMatrix._13 = x.z;

	rotateMatrix._21 = y.x;
	rotateMatrix._22 = y.y;
	rotateMatrix._23 = y.z;

	rotateMatrix._31 = z.x;
	rotateMatrix._32 = z.y;
	rotateMatrix._33 = z.z;

	matrix4 translateMatrix;
	translateMatrix.LoadIdentity();
	translateMatrix.Tranlate(inCameraPosition.x, inCameraPosition.y, inCameraPosition.z);

	*this = (rotateMatrix * translateMatrix);//
	*this = Invert();
}

// ==============================================================================
// 加载为单位矩阵 (4x4)
// ==============================================================================
void matrix4::LoadIdentity() {
	memset(v, 0, sizeof(v));
	_11 = 1.0f;
	_22 = 1.0f;
	_33 = 1.0f;
	_44 = 1.0f;
}
void matrix4::SetLeftTop3x3(const matrix3& inM) {
	_11 = inM._11;
	_12 = inM._12;
	_13 = inM._13;

	_21 = inM._21;
	_22 = inM._22;
	_23 = inM._23;

	_31 = inM._31;
	_32 = inM._32;
	_33 = inM._33;
}

// ==============================================================================
// 平移矩阵
// ==============================================================================
void matrix4::Tranlate(float inX, float inY, float inZ) {
	_41 = inX;
	_42 = inY;
	_43 = inZ;
}

// ==============================================================================
// 辅助函数：获取剔除第 inRow 行和第 inColumn 列后的 3x3 余子式矩阵
// ==============================================================================
matrix3 matrix4::GetMIJ(int inRow,int inColumn) const {
	matrix3 ret;
	int i = 0;
	for (int r = 1; r < 5; r++) {
		for (int c = 1; c < 5; c++) {
			if (r != inRow && c != inColumn) {
				ret.v[i++] = v[(r-1)*4+c-1];
			}
		}
	}
	return ret;
}

// ==============================================================================
// 4x4 矩阵转置
// ==============================================================================
void matrix4::Transpose() {
	float temp = _12;
	_12 = _21;
	_21 = temp;
	temp = _13;
	_13 = _31;
	_31 = temp;
	temp = _23;
	_23 = _32;
	_32 = temp;

	temp = _14;
	_14 = _41;
	_41 = temp;
	temp = _24;
	_24 = _42;
	_42 = temp;
	temp = _34;
	_34 = _43;
	_43 = temp;
}

// ==============================================================================
// 4x4 矩阵行列式计算，通过余子式展开
// ==============================================================================
float matrix4::Determinant()const {
	float _1 = _11 * GetMIJ(1,1).Determinant();
	float _2 = -_12 * GetMIJ(1,2).Determinant();
	float _3 = _13 * GetMIJ(1,3).Determinant();
	float _4 = -_14 * GetMIJ(1,4).Determinant();
	return _1 + _2 + _3 + _4;
}

// ==============================================================================
// 4x4 矩阵求逆 (基于伴随矩阵法)
// ==============================================================================
matrix4 matrix4::Invert()const {
	matrix4 ret;
	float determinant = Determinant();
	float absDeterminant = determinant > 0.0f ? determinant : -determinant;
	if (absDeterminant < 0.000001f) {
		ret.LoadIdentity();
		return ret;
	}
	ret._11 = GetMIJ(1, 1).Determinant();
	ret._12 = -GetMIJ(1, 2).Determinant();
	ret._13 = GetMIJ(1, 3).Determinant();
	ret._14 = -GetMIJ(1, 4).Determinant();

	ret._21 = -GetMIJ(2, 1).Determinant();
	ret._22 = GetMIJ(2, 2).Determinant();
	ret._23 = -GetMIJ(2, 3).Determinant();
	ret._24 = GetMIJ(2, 4).Determinant();

	ret._31 = GetMIJ(3, 1).Determinant();
	ret._32 = -GetMIJ(3, 2).Determinant();
	ret._33 = GetMIJ(3, 3).Determinant();
	ret._34 = -GetMIJ(3, 4).Determinant();

	ret._41 = -GetMIJ(4, 1).Determinant();
	ret._42 = GetMIJ(4, 2).Determinant();
	ret._43 = -GetMIJ(4, 3).Determinant();
	ret._44 = GetMIJ(4, 4).Determinant();
	ret.Transpose();
	for (int i = 0; i < 16; i++) {
		ret.v[i] *= (1.0f/determinant);
	}
	return ret;
}
matrix4 matrix4::operator*(const matrix4& inM)const {
	matrix4 ret;
	ret._11 = dot4(float4(_11, _12, _13, _14), float4(inM._11, inM._21, inM._31, inM._41));
	ret._12 = dot4(float4(_11, _12, _13, _14), float4(inM._12, inM._22, inM._32, inM._42));
	ret._13 = dot4(float4(_11, _12, _13, _14), float4(inM._13, inM._23, inM._33, inM._43));
	ret._14 = dot4(float4(_11, _12, _13, _14), float4(inM._14, inM._24, inM._34, inM._44));

	ret._21 = dot4(float4(_21, _22, _23, _24), float4(inM._11, inM._21, inM._31, inM._41));
	ret._22 = dot4(float4(_21, _22, _23, _24), float4(inM._12, inM._22, inM._32, inM._42));
	ret._23 = dot4(float4(_21, _22, _23, _24), float4(inM._13, inM._23, inM._33, inM._43));
	ret._24 = dot4(float4(_21, _22, _23, _24), float4(inM._14, inM._24, inM._34, inM._44));

	ret._31 = dot4(float4(_31, _32, _33, _34), float4(inM._11, inM._21, inM._31, inM._41));
	ret._32 = dot4(float4(_31, _32, _33, _34), float4(inM._12, inM._22, inM._32, inM._42));
	ret._33 = dot4(float4(_31, _32, _33, _34), float4(inM._13, inM._23, inM._33, inM._43));
	ret._34 = dot4(float4(_31, _32, _33, _34), float4(inM._14, inM._24, inM._34, inM._44));

	ret._41 = dot4(float4(_41, _42, _43, _44), float4(inM._11, inM._21, inM._31, inM._41));
	ret._42 = dot4(float4(_41, _42, _43, _44), float4(inM._12, inM._22, inM._32, inM._42));
	ret._43 = dot4(float4(_41, _42, _43, _44), float4(inM._13, inM._23, inM._33, inM._43));
	ret._44 = dot4(float4(_41, _42, _43, _44), float4(inM._14, inM._24, inM._34, inM._44));
	return ret;
}
void matrix4::operator=(const matrix4& inM) {
	memcpy(v, inM.v, sizeof(v));
}

// ==============================================================================
// 四维向量乘 4x4 矩阵 (Vector * Matrix)
// ==============================================================================
float4 operator*(const float4& inV, const matrix4& inM) {
	float4 p;
	p.x = inV.x * inM._11 + inV.y * inM._21 + inV.z * inM._31 + inV.w * inM._41;
	p.y = inV.x * inM._12 + inV.y * inM._22 + inV.z * inM._32 + inV.w * inM._42;
	p.z = inV.x * inM._13 + inV.y * inM._23 + inV.z * inM._33 + inV.w * inM._43;
	p.w = inV.x * inM._14 + inV.y * inM._24 + inV.z * inM._34 + inV.w * inM._44;
	return p;
}