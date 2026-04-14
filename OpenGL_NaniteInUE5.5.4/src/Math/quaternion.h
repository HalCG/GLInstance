#pragma once
#include "matrix4.h"

class quaternion {
public:
	float w, x, y, z;
	
	quaternion(float inAngleX, float inAngleY, float inAngleZ);
	
	quaternion(float inW, float inX, float inY, float inZ);
	
	void operator=(const quaternion& inR);
	
	quaternion operator*(const quaternion& inR)const;
	
	matrix3 toMatrix3()const;
};
