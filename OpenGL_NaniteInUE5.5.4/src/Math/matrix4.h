#pragma once
#include "float4.h"

// 3x3矩阵类，常用于旋转和缩放
class matrix3 {
public:
	union
	{
		struct {
			float _11, _12, _13;
			float _21, _22, _23;
			float _31, _32, _33;
		};
		float v[9];
	};
	void LoadIdentity();
	void SetScale(float inX, float inY, float inZ);
	void operator=(const matrix3& inRightValue);
	matrix3 operator*(const matrix3& inRightValue)const;
	void Transpose();
	float Determinant() const;
};

// 4x4矩阵类，常用于投影、视图和模型变换 (MVP矩阵)
class matrix4{
public:
	union 
	{
		struct {
			float _11, _12, _13, _14;
			float _21, _22, _23, _24;
			float _31, _32, _33, _34;
			float _41, _42, _43, _44;
		};
		float v[16];
	};
	void LoadIdentity();
	void Perspective(float inFOVInAngle, float inAspect, float inNear, float inFar);
	void LookAt(const float4&inCameraPosition,
		const float4&inTargetPosition,const float4&inHelpVector);
	void SetLeftTop3x3(const matrix3& inM);
	void Tranlate(float inX, float inY, float inZ);
	matrix3 GetMIJ(int inRow, int inColumn) const;
	void Transpose();
	float Determinant() const;
	matrix4 Invert() const;
	matrix4 operator*(const matrix4& inM)const;
	void operator=(const matrix4& inM);
};
float4 operator*(const float4&inV,const matrix4&inM);

