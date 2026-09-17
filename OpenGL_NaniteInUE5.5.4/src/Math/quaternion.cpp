#include "quaternion.h"
#include<math.h>

// ==============================================================================
// 构造函数：通过欧拉角(角度制)初始化四元数
// ==============================================================================
quaternion::quaternion(float inAngleX, float inAngleY, float inAngleZ) {
	float xAngle = 0.5f * inAngleX * 3.1415926f / 180.0f;
	float yAngle = 0.5f * inAngleY * 3.1415926f / 180.0f;
	float zAngle = 0.5f * inAngleZ * 3.1415926f / 180.0f;
	quaternion qRx(cosf(xAngle), sinf(xAngle), 0.0f, 0.0f);//|q|=1
	quaternion qRy(cosf(yAngle), 0.0f, sinf(yAngle), 0.0f);
	quaternion qRz(cosf(zAngle), 0.0f, 0.0f, sinf(zAngle));
	*this = (qRx * qRy) * qRz;
}

// ==============================================================================
// 构造函数：直接指定分量
// ==============================================================================
quaternion::quaternion(float inW, float inX, float inY, float inZ) {
	w = inW;
	x = inX;
	y = inY;
	z = inZ;
}
void quaternion::operator=(const quaternion& inR) {
	w = inR.w;
	x = inR.x;
	y = inR.y;
	z = inR.z;
}

// ==============================================================================
// 四元数乘法，用于组合两个旋转
// ==============================================================================
quaternion quaternion::operator*(const quaternion& inR)const {
	return quaternion(
		w * inR.w - x * inR.x - y * inR.y - z * inR.z,
		w * inR.x + x * inR.w + y * inR.z - z * inR.y,
		w * inR.y + y * inR.w + z * inR.x - x * inR.z,
		w * inR.z + x * inR.y + z * inR.w - y * inR.x
	);
}

// ==============================================================================
// 将四元数转换为 3x3 的旋转矩阵
// ==============================================================================
matrix3 quaternion::toMatrix3()const {
	float xx = x * x;
	float yy = y * y;
	float zz = z * z;
	float xy = x * y;
	float xz = x * z;
	float yz = y * z;
	float wx = w * x;
	float wy = w * y;
	float wz = w * z;
	matrix3 m;
	m._11 = 1.0f - 2.0f * yy - 2.0f * zz;
	m._12 = 2.0f * xy + 2.0f * wz;
	m._13 = 2.0f * xz - 2.0f * wy;

	m._21 = 2.0f * xy - 2.0f * wz;
	m._22 = 1.0f - 2.0f * xx - 2.0f * zz;
	m._23 = 2.0f * yz + 2.0f * wx;

	m._31 = 2.0f * xz + 2.0f * wy;
	m._32 = 2.0f * yz - 2.0f * wx;
	m._33 = 1.0f - 2.0f * xx - 2.0f * yy;
	return m;
}