#include "float4.h"
#include<stdio.h>
#include<string.h>
#include<math.h>

// 赋值操作符重载
void float4::operator=(const float4& inV) {
	memcpy(v, inV.v, sizeof(v));
}
void float4::operator*=(float inScalar) {
	x *= inScalar;
	y *= inScalar;
	z *= inScalar;
}
void float4::operator-=(float inScalar) {
	x -= inScalar;
	y -= inScalar;
	z -= inScalar;
}
void float4::operator/=(float inScalar) {
	x /= inScalar;
	y /= inScalar;
	z /= inScalar;
}
void float4::operator+=(float inScalar) {
	x += inScalar;
	y += inScalar;
	z += inScalar;
}
float4 float4::operator+(const float4& inV)const {
	return float4(x + inV.x, y + inV.y, z + inV.z, w);
}
float4 float4::operator-(const float4& inV)const {
	return float4(x - inV.x, y - inV.y, z - inV.z, w);
}
float4 float4::operator*(const float inScalar)const {
	return float4(x * inScalar, y * inScalar, z * inScalar, w);
}

// 向量归一化操作，使其长度变为 1
void float4::Normalize() {
	float magnitude = sqrtf(x * x + y * y + z * z);
	if (magnitude < 0.000001f) {
		return;
	}
	x /= magnitude;
	y /= magnitude;
	z /= magnitude;
}
float MinValue(float inA, float inB) {
	return inA < inB ? inA : inB;
}
float MaxValue(float inA, float inB) {
	return inA > inB ? inA : inB;
}
float4 Min(const float4& inA, const float4& inB) { // 分量级最小值
	return float4(MinValue(inA.x, inB.x), MinValue(inA.y, inB.y),
		MinValue(inA.z, inB.z), MinValue(inA.w, inB.w));
}
float4 Max(const float4& inA, const float4& inB) { // 分量级最大值
	return float4(MaxValue(inA.x, inB.x), MaxValue(inA.y, inB.y),
		MaxValue(inA.z, inB.z), MaxValue(inA.w, inB.w));
}

// 向量叉乘，返回两个向量构成的平面的法向量
float4 cross(const float4& inA, const float4& inB) {
	return float4(
		inA.y * inB.z - inA.z * inB.y,
		inA.z * inB.x - inA.x * inB.z,
		inA.x * inB.y - inA.y * inB.x,
		inA.w
	);
}

// 三维向量点乘
float dot3(const float4& inA, const float4& inB) {
	return inA.x * inB.x + inA.y * inB.y + inA.z * inB.z;
}

// 四维向量点乘
float dot4(const float4& inA, const float4& inB) {
	return inA.x * inB.x + inA.y * inB.y + inA.z * inB.z + inA.w * inB.w;
}