// Just enough 4x4 matrix code for an orbit camera. Column-major, like OpenGL.
#pragma once

#include <array>
#include <cmath>

namespace testbed
{

using Mat4 = std::array<float, 16>;

constexpr Mat4 identity()
{
   return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

constexpr Mat4 operator*(const Mat4& a, const Mat4& b)
{
   Mat4 r{};
   for (int col = 0; col < 4; ++col)
   {
      for (int row = 0; row < 4; ++row)
      {
         float sum = 0.0f;
         for (int k = 0; k < 4; ++k)
         {
            sum += a[static_cast<std::size_t>(k * 4 + row)] * b[static_cast<std::size_t>(col * 4 + k)];
         }
         r[static_cast<std::size_t>(col * 4 + row)] = sum;
      }
   }
   return r;
}

inline Mat4 perspective(float fov_y, float aspect, float near_plane, float far_plane)
{
   const float f = 1.0f / std::tan(fov_y * 0.5f);
   Mat4 m{};
   m[0] = f / aspect;
   m[5] = f;
   m[10] = (far_plane + near_plane) / (near_plane - far_plane);
   m[11] = -1.0f;
   m[14] = 2.0f * far_plane * near_plane / (near_plane - far_plane);
   return m;
}

constexpr Mat4 translation(float x, float y, float z)
{
   auto m = identity();
   m[12] = x;
   m[13] = y;
   m[14] = z;
   return m;
}

inline Mat4 rotation_x(float angle)
{
   const float c = std::cos(angle);
   const float s = std::sin(angle);
   return {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
}

inline Mat4 rotation_y(float angle)
{
   const float c = std::cos(angle);
   const float s = std::sin(angle);
   return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
}

}  // namespace testbed
