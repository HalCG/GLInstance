#pragma once

#include "float4.h"
#include "matrix4.h"

enum class CameraDragMode { None, Rotate, Pan, Dolly };

class TrackballCamera {
public:
    TrackballCamera();

    // Initialize camera parameters
    void Init(const float4& inPosition, const float4& inTarget);

    // Calculate eye position based on yaw/pitch/radius
    float4 GetEye() const;

    // Get forward direction
    float4 GetForward() const;

    // Get view matrix
    matrix4 GetViewMatrix() const;

    // Begin drag
    void BeginDrag(CameraDragMode mode, double x, double y);

    // End drag
    void EndDrag();

    // Is currently dragging
    bool IsDragging() const;

    // Apply cursor delta
    bool ApplyCursorDelta(double x, double y, unsigned width, unsigned height);

    // Apply scroll for zooming
    void ApplyScroll(double yoffset);

public:
    float4 mTarget;
    float mYaw;
    float mPitch;
    float mRadius;

    CameraDragMode mDragMode;
    bool mSkipDragDelta;
    double mLastCursorX;
    double mLastCursorY;

    // Sensitivity parameters
    float mPanFactor = 1.0f;
    float mDollyFactor = 1.5f;
    float mMouseOrbitFullWidthDegrees = 180.0f;
    float mScrollZoomSensitivity = 0.9f;
    float mMinOrbitRadius = 1.0f;
    float mMaxOrbitRadius = 5000.0f;
};
