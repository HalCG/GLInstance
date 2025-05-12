#ifndef ORBIT_CAMERA_HPP
#define ORBIT_CAMERA_HPP

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

/** 绕 Y 轴轨道相机，与原 main.cpp 中 lookAt / perspective 逻辑一致 */
class OrbitCamera {
public:
  float orbitAngleDeg = 45.0f;
  float orbitRadius = 2.0f;
  float fovDeg = 90.0f;
  float aspect = 800.0f / 600.0f;
  float nearPlane = 0.1f;
  float farPlane = 100.0f;

  glm::mat4 view() const {
    const float rad = glm::radians(orbitAngleDeg);
    const glm::vec3 eye(
        orbitRadius * glm::sin(rad), 0.0f,
        orbitRadius * glm::cos(rad));
    return glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
  }

  glm::mat4 projection() const {
    return glm::perspective(glm::radians(fovDeg), aspect, nearPlane,
                            farPlane);
  }

  void setAspectFromViewport(unsigned int width, unsigned int height) {
    aspect = static_cast<float>(width) / static_cast<float>(height);
  }
};

#endif
