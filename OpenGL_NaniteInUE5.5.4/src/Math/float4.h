#pragma once

// 4维浮点数向量类，用于表示位置、颜色、四元数等
class float4{
public:
	union
	{
		struct {
			float x, y, z, w;
		};
		float v[4];
	};
	float4() :x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
	float4(float inValue) :x(inValue), y(inValue), z(inValue), w(inValue) {}
	float4(float* inData) :x(inData[0]), y(inData[1]), z(inData[2]), w(inData[3]) {}
	float4(float inX, float inY, float inZ, float inW=1.0f) :x(inX), y(inY), z(inZ), w(inW) {}
	float4(const float4 &inV) :x(inV.x), y(inV.y), z(inV.z), w(inV.w) {}
	float& operator[](int inIndex) {
		return v[inIndex];
	}
	void operator=(const float4& inV);
	void operator*=(float inScalar);
	void operator-=(float inScalar);
	void operator/=(float inScalar);
	void operator+=(float inScalar);
	float4 operator+(const float4& inV)const;
	float4 operator-(const float4& inV)const;
	float4 operator*(float inScalar)const;
	void Normalize();
};
float MinValue(float inA, float inB);
float MaxValue(float inA, float inB);
float4 Min(const float4& inA, const float4& inB);
float4 Max(const float4& inA, const float4& inB);
float4 cross(const float4& inA, const float4& inB);
float dot3(const float4& inA, const float4& inB);
float dot4(const float4& inA, const float4& inB);
