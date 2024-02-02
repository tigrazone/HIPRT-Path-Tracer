#include "camera.h"

const glm::mat4x4 Camera::DEFAULT_COORDINATES_SYSTEM = glm::mat4x4(
     1.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 1.0f, 0.0f, 0.0f,
     0.0f, 0.0f, -1.0f, 0.0f,
     0.0f, 0.0f, 0.0f, 1.0f
 );

Camera::Camera()
{
    translation = glm::vec3(0.0f, 2.0f, 0.0f);
    rotation = glm::quat(glm::vec3(0.0f, 0.0f, 0.0f));
}

HIPRTCamera Camera::to_hiprt()
{
    HIPRTCamera hiprt_cam;

    glm::mat4x4 view_matrix = get_view_matrix();
    glm::mat4x4 view_matrix_inv = glm::inverse(view_matrix);
    glm::mat4x4 projection_matrix_inv = glm::inverse(projection_matrix);

    hiprt_cam.inverse_view = *reinterpret_cast<float4x4*>(&view_matrix_inv);
    hiprt_cam.inverse_projection = *reinterpret_cast<float4x4*>(&projection_matrix_inv);

    return hiprt_cam;
}

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/string_cast.hpp" //TODO remove
glm::mat4x4 Camera::get_view_matrix() const
{
    glm::mat4x4 view_matrix = glm::mat4_cast(glm::normalize(rotation)) * glm::translate(glm::mat4(1.0f), translation);

    return view_matrix;
}

glm::vec3 Camera::get_view_direction() const
{
    glm::mat4x4 view_mat = get_view_matrix();

    return glm::vec3(view_mat[0][2], view_mat[1][2], view_mat[2][2]);
}
