#include "trackball_camera.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

inline float Radians(float degrees) {
    return degrees * (float)M_PI / 180.0f;
}
inline float Degrees(float radians) {
    return radians * 180.0f / (float)M_PI;
}

TrackballCamera::TrackballCamera()
    : mTarget(0.0f, 0.0f, 0.0f)
    , mYaw(0.0f)
    , mPitch(0.0f)
    , mRadius(100.0f)
    , mDragMode(CameraDragMode::None)
    , mSkipDragDelta(false)
    , mLastCursorX(0.0)
    , mLastCursorY(0.0)
{
}

void TrackballCamera::Init(const float4& inPosition, const float4& inTarget) {
    mTarget = inTarget;
    float4 dir = inPosition - inTarget;
    mRadius = sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    if (mRadius > 0.0001f) {
        mPitch = Degrees(asin(dir.y / mRadius));
        mYaw = Degrees(atan2(dir.x, dir.z));
    }
}

float4 TrackballCamera::GetEye() const {
    const float yawRad = Radians(mYaw);
    const float pitchRad = Radians(mPitch);
    float4 eye;
    eye.x = mTarget.x + mRadius * cos(pitchRad) * sin(yawRad);
    eye.y = mTarget.y + mRadius * sin(pitchRad);
    eye.z = mTarget.z + mRadius * cos(pitchRad) * cos(yawRad);
    eye.w = 1.0f;
    return eye;
}

float4 TrackballCamera::GetForward() const {
    float4 eye = GetEye();
    float4 fwd = mTarget - eye;
    fwd.Normalize();
    return fwd;
}

matrix4 TrackballCamera::GetViewMatrix() const {
    matrix4 view;
    view.LookAt(GetEye(), mTarget, float4(0.0f, 1.0f, 0.0f));
    return view;
}

void TrackballCamera::BeginDrag(CameraDragMode mode, double x, double y) {
    mDragMode = mode;
    mSkipDragDelta = true;
    mLastCursorX = x;
    mLastCursorY = y;
}

void TrackballCamera::EndDrag() {
    mDragMode = CameraDragMode::None;
    mSkipDragDelta = false;
}

bool TrackballCamera::IsDragging() const {
    return mDragMode != CameraDragMode::None;
}

bool TrackballCamera::ApplyCursorDelta(double x, double y, unsigned width, unsigned height) {
    if (mDragMode == CameraDragMode::None) {
        return false;
    }
    if (mSkipDragDelta) {
        mLastCursorX = x;
        mLastCursorY = y;
        mSkipDragDelta = false;
        return false;
    }

    const double deltaX = x - mLastCursorX;
    const double deltaY = y - mLastCursorY;
    mLastCursorX = x;
    mLastCursorY = y;

    const float w = static_cast<float>(width > 0 ? width : 1280);
    const float h = static_cast<float>(height > 0 ? height : 720);

    switch (mDragMode) {
    case CameraDragMode::Rotate: {
        const float sensX = mMouseOrbitFullWidthDegrees / w;
        const float sensY = mMouseOrbitFullWidthDegrees / h;
        mYaw -= static_cast<float>(deltaX) * sensX;
        mPitch -= static_cast<float>(-deltaY) * sensY;
        if (mPitch > 85.0f) mPitch = 85.0f;
        if (mPitch < -85.0f) mPitch = -85.0f;
        break;
    }
    case CameraDragMode::Pan: {
        const float4 e = GetEye();
        float4 forward = mTarget - e;
        forward.Normalize();
        const float4 worldUp(0.0f, 1.0f, 0.0f);
        float4 right = cross(forward, worldUp);
        right.Normalize();
        float4 up = cross(right, forward);
        up.Normalize();

        const float panScale = mRadius * mPanFactor / h;
        mTarget.x -= right.x * static_cast<float>(deltaX) * panScale;
        mTarget.y -= right.y * static_cast<float>(deltaX) * panScale;
        mTarget.z -= right.z * static_cast<float>(deltaX) * panScale;

        mTarget.x += up.x * static_cast<float>(deltaY) * panScale;
        mTarget.y += up.y * static_cast<float>(deltaY) * panScale;
        mTarget.z += up.z * static_cast<float>(deltaY) * panScale;
        break;
    }
    case CameraDragMode::Dolly: {
        const float dollyScale = mDollyFactor * mRadius / h;
        mRadius -= static_cast<float>(deltaY) * dollyScale;
        if (mRadius < mMinOrbitRadius) mRadius = mMinOrbitRadius;
        if (mRadius > mMaxOrbitRadius) mRadius = mMaxOrbitRadius;
        break;
    }
    default:
        break;
    }
    return true;
}

void TrackballCamera::ApplyScroll(double yoffset) {
    if (yoffset > 0) {
        mRadius *= mScrollZoomSensitivity;
    } else if (yoffset < 0) {
        mRadius /= mScrollZoomSensitivity;
    }
    if (mRadius < mMinOrbitRadius) mRadius = mMinOrbitRadius;
    if (mRadius > mMaxOrbitRadius) mRadius = mMaxOrbitRadius;
}
