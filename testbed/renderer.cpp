#include "renderer.hpp"

#include <stb_image_write.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <print>
#include <stdexcept>
#include <string>

namespace testbed
{

using tinyverlet::Vec3;

void delete_buffer(GLuint name)
{
   glDeleteBuffers(1, &name);
}

void delete_vertex_array(GLuint name)
{
   glDeleteVertexArrays(1, &name);
}

void delete_program(GLuint name)
{
   glDeleteProgram(name);
}

void delete_framebuffer(GLuint name)
{
   glDeleteFramebuffers(1, &name);
}

void delete_renderbuffer(GLuint name)
{
   glDeleteRenderbuffers(1, &name);
}

namespace
{

// shading: 0 unlit, 1 smooth (vertex normals), 2 flat (normal from screen-space derivatives)
constexpr const char* vertex_shader = R"(#version 300 es
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
uniform mat4 u_mvp;
uniform vec4 u_transform;  // xyz offset, w scale; used to place the unit sphere
out vec3 v_position;
out vec3 v_normal;
void main()
{
   v_position = a_position * u_transform.w + u_transform.xyz;
   v_normal = a_normal;
   gl_Position = u_mvp * vec4(v_position, 1.0);
   gl_PointSize = 5.0;
}
)";

constexpr const char* fragment_shader = R"(#version 300 es
precision highp float;
in vec3 v_position;
in vec3 v_normal;
uniform vec3 u_color;
uniform int u_shading;
out vec4 o_color;
void main()
{
   if (u_shading == 0)
   {
      o_color = vec4(u_color, 1.0);
      return;
   }
   vec3 n = u_shading == 1 ? normalize(v_normal) : normalize(cross(dFdx(v_position), dFdy(v_position)));
   if (u_shading == 1 && !gl_FrontFacing)
   {
      n = -n;  // two-sided cloth
   }
   vec3 l = normalize(vec3(0.5, 0.9, 0.4));
   float diffuse = max(dot(n, l), 0.0);
   float sky = 0.5 + 0.5 * n.y;
   o_color = vec4(u_color * (0.18 + 0.17 * sky + 0.7 * diffuse), 1.0);
}
)";

GLuint compile(GLenum type, const char* source)
{
   const GLuint shader = glCreateShader(type);
   glShaderSource(shader, 1, &source, nullptr);
   glCompileShader(shader);

   GLint ok = 0;
   glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
   if (ok == 0)
   {
      std::string log(1024, '\0');
      glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), nullptr, log.data());
      glDeleteShader(shader);
      throw std::runtime_error("shader: " + log);
   }
   return shader;
}

// apply the inverse of rotation_x(pitch) * rotation_y(yaw) to a view-space vector
Vec3 unrotate(const Vec3& v, float yaw, float pitch)
{
   const float cp = std::cos(-pitch);
   const float sp = std::sin(-pitch);
   const Vec3 a{v.x, cp * v.y - sp * v.z, sp * v.y + cp * v.z};
   const float cy = std::cos(-yaw);
   const float sy = std::sin(-yaw);
   return {cy * a.x + sy * a.z, a.y, -sy * a.x + cy * a.z};
}

void push(std::vector<Vec3>& interleaved, const Vec3& position, const Vec3& normal = {})
{
   interleaved.push_back(position);
   interleaved.push_back(normal);
}

}  // namespace

Mat4 Camera::view() const
{
   return translation(0.0f, 0.0f, -distance) * rotation_x(pitch) * rotation_y(yaw) *
          translation(-target.x, -target.y, -target.z);
}

Vec3 Camera::eye() const
{
   return target + unrotate({0.0f, 0.0f, distance}, yaw, pitch);
}

Vec3 Camera::ray(float ndc_x, float ndc_y, float aspect) const
{
   const float t = std::tan(fov_y * 0.5f);
   const Vec3 direction = unrotate({ndc_x * t * aspect, ndc_y * t, -1.0f}, yaw, pitch);
   return direction * (1.0f / tinyverlet::length(direction));
}

Renderer::Renderer()
{
   const GLuint vs = compile(GL_VERTEX_SHADER, vertex_shader);
   const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment_shader);

   program_ = Program{glCreateProgram()};
   glAttachShader(program_.get(), vs);
   glAttachShader(program_.get(), fs);
   glLinkProgram(program_.get());
   glDeleteShader(vs);
   glDeleteShader(fs);

   GLint ok = 0;
   glGetProgramiv(program_.get(), GL_LINK_STATUS, &ok);
   if (ok == 0)
   {
      throw std::runtime_error("shader program failed to link");
   }

   mvp_location_ = glGetUniformLocation(program_.get(), "u_mvp");
   color_location_ = glGetUniformLocation(program_.get(), "u_color");
   shading_location_ = glGetUniformLocation(program_.get(), "u_shading");
   transform_location_ = glGetUniformLocation(program_.get(), "u_transform");

   spheres_ = make_mesh(GL_STATIC_DRAW);
   upload_unit_sphere();
   grid_ = make_mesh(GL_STATIC_DRAW);
   cloth_ = make_mesh(GL_DYNAMIC_DRAW);
   boxes_ = make_mesh(GL_DYNAMIC_DRAW);
   lines_ = make_mesh(GL_DYNAMIC_DRAW);
   points_ = make_mesh(GL_DYNAMIC_DRAW);
}

Renderer::Mesh Renderer::make_mesh(GLenum usage)
{
   Mesh mesh;
   GLuint name = 0;

   glGenVertexArrays(1, &name);
   mesh.vao = VertexArray{name};
   glBindVertexArray(name);

   glGenBuffers(1, &name);
   mesh.vertices = Buffer{name};
   glBindBuffer(GL_ARRAY_BUFFER, name);
   glBufferData(GL_ARRAY_BUFFER, 0, nullptr, usage);

   constexpr auto stride = static_cast<GLsizei>(2 * sizeof(Vec3));
   glEnableVertexAttribArray(0);
   glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
   glEnableVertexAttribArray(1);
   glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(sizeof(Vec3)));

   glGenBuffers(1, &name);
   mesh.indices = Buffer{name};
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, name);

   glBindVertexArray(0);
   return mesh;
}

void Renderer::upload(Mesh& mesh, const std::vector<Vec3>& interleaved, const std::vector<std::uint32_t>& indices,
                      GLenum usage)
{
   glBindVertexArray(mesh.vao.get());
   glBindBuffer(GL_ARRAY_BUFFER, mesh.vertices.get());
   glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(interleaved.size() * sizeof(Vec3)), interleaved.data(), usage);
   glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.get());
   glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                indices.data(), usage);
   mesh.count = static_cast<GLsizei>(indices.size());
   glBindVertexArray(0);
}

// one latitude/longitude unit sphere, placed per sphere by u_transform
void Renderer::upload_unit_sphere()
{
   std::vector<Vec3> vertices;
   std::vector<std::uint32_t> indices;
   constexpr std::uint32_t stacks = 24;
   constexpr std::uint32_t slices = 48;
   for (std::uint32_t i = 0; i <= stacks; ++i)
   {
      const float theta = std::numbers::pi_v<float> * static_cast<float>(i) / stacks;
      for (std::uint32_t j = 0; j <= slices; ++j)
      {
         const float phi = 2.0f * std::numbers::pi_v<float> * static_cast<float>(j) / slices;
         const Vec3 n{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
         push(vertices, n, n);
      }
   }
   for (std::uint32_t i = 0; i < stacks; ++i)
   {
      for (std::uint32_t j = 0; j < slices; ++j)
      {
         const std::uint32_t a = i * (slices + 1) + j;
         const std::uint32_t b = a + slices + 1;
         indices.insert(indices.end(), {a, b, a + 1, a + 1, b, b + 1});
      }
   }
   upload(spheres_, vertices, indices, GL_STATIC_DRAW);
}

void Renderer::set_scene(const Scene& scene)
{
   // ground grid and the bounds box as lines
   std::vector<Vec3> vertices;
   std::vector<std::uint32_t> indices;
   const auto line = [&](const Vec3& a, const Vec3& b)
   {
      const auto i = static_cast<std::uint32_t>(vertices.size() / 2);
      push(vertices, a);
      push(vertices, b);
      indices.insert(indices.end(), {i, i + 1});
   };
   if (scene.world.settings.ground)
   {
      const float y = *scene.world.settings.ground;
      for (int i = -10; i <= 10; ++i)
      {
         const float t = static_cast<float>(i) * 0.25f;
         line({t, y, -2.5f}, {t, y, 2.5f});
         line({-2.5f, y, t}, {2.5f, y, t});
      }
   }
   if (scene.world.settings.bounds)
   {
      const auto& [lo, hi] = *scene.world.settings.bounds;
      const float top = std::min(hi.y, lo.y + 3.0f);
      const std::array<Vec3, 4> corners{{{lo.x, 0, lo.z}, {hi.x, 0, lo.z}, {hi.x, 0, hi.z}, {lo.x, 0, hi.z}}};
      for (std::size_t i = 0; i < 4; ++i)
      {
         const auto& a = corners[i];
         const auto& b = corners[(i + 1) % 4];
         line({a.x, top, a.z}, {b.x, top, b.z});
         line({a.x, std::max(lo.y, 0.0f), a.z}, {a.x, top, a.z});
      }
   }
   upload(grid_, vertices, indices, GL_STATIC_DRAW);
}

void Renderer::draw_mesh(const Mesh& mesh, GLenum mode, const Mat4& mvp, std::array<float, 3> color, int shading,
                         std::array<float, 4> transform) const
{
   if (mesh.count == 0)
   {
      return;
   }
   glUniformMatrix4fv(mvp_location_, 1, GL_FALSE, mvp.data());
   glUniform3f(color_location_, color[0], color[1], color[2]);
   glUniform1i(shading_location_, shading);
   glUniform4f(transform_location_, transform[0], transform[1], transform[2], transform[3]);
   glBindVertexArray(mesh.vao.get());
   if (mode == GL_POINTS)
   {
      glDrawArrays(GL_POINTS, 0, mesh.count);
   }
   else
   {
      glDrawElements(mode, mesh.count, GL_UNSIGNED_INT, nullptr);
   }
}

void Renderer::draw(int width, int height, const Camera& camera, const Scene& scene, const Display& display)
{
   const auto& world = scene.world;
   const auto& particles = world.particles;

   // cloth: shared vertices with smooth normals
   auto& vertices = scratch_vertices_;
   auto& indices = scratch_indices_;
   vertices.clear();
   indices.clear();
   std::vector<Vec3> normals(particles.size());
   for (const auto& grid : scene.cloths)
   {
      for (const auto& [a, b, c] : grid.triangles(world))
      {
         const Vec3 n = tinyverlet::cross(particles[b].position - particles[a].position,
                                          particles[c].position - particles[a].position);
         normals[a] += n;
         normals[b] += n;
         normals[c] += n;
         indices.insert(indices.end(), {a, b, c});
      }
   }
   for (std::size_t i = 0; i < particles.size(); ++i)
   {
      const float l = tinyverlet::length(normals[i]);
      push(vertices, particles[i].position, l > 0.0f ? normals[i] * (1.0f / l) : Vec3{0, 1, 0});
   }
   upload(cloth_, vertices, indices, GL_DYNAMIC_DRAW);

   // boxes reuse the same vertices, flat shaded in the shader
   indices.clear();
   for (const auto first : scene.boxes)
   {
      for (const auto& [a, b, c] : box_triangles(first))
      {
         indices.insert(indices.end(), {a, b, c});
      }
   }
   upload(boxes_, vertices, indices, GL_DYNAMIC_DRAW);

   indices.clear();
   for (const auto& c : world.constraints)
   {
      indices.insert(indices.end(), {c.a, c.b});
   }
   upload(lines_, vertices, indices, GL_DYNAMIC_DRAW);

   indices.clear();
   glBindVertexArray(points_.vao.get());
   glBindBuffer(GL_ARRAY_BUFFER, points_.vertices.get());
   glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vec3)), vertices.data(),
                GL_DYNAMIC_DRAW);
   points_.count = static_cast<GLsizei>(particles.size());
   glBindVertexArray(0);

   // draw
   glViewport(0, 0, width, height);
   glClearColor(0.118f, 0.118f, 0.125f, 1.0f);
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
   glEnable(GL_DEPTH_TEST);
   glDepthFunc(GL_LEQUAL);
   glUseProgram(program_.get());

   const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
   const Mat4 mvp = perspective(camera.fov_y, aspect, 0.05f, 100.0f) * camera.view();

   draw_mesh(grid_, GL_LINES, mvp, {0.27f, 0.27f, 0.3f}, 0);
   for (const auto& sphere : world.spheres)
   {
      // drawn slightly smaller so cloth resting on it does not z-fight
      const auto& c = sphere.center;
      draw_mesh(spheres_, GL_TRIANGLES, mvp, {0.55f, 0.58f, 0.62f}, 1, {c.x, c.y, c.z, sphere.radius * 0.985f});
   }

   if (display.surfaces)
   {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0f, 1.0f);
      draw_mesh(cloth_, GL_TRIANGLES, mvp, display.cloth_color, 1);
      draw_mesh(boxes_, GL_TRIANGLES, mvp, {0.35f, 0.62f, 0.86f}, 2);
      glDisable(GL_POLYGON_OFFSET_FILL);
   }
   if (display.constraints)
   {
      draw_mesh(lines_, GL_LINES, mvp, {0.95f, 0.85f, 0.55f}, 0);
   }
   if (display.particles)
   {
      draw_mesh(points_, GL_POINTS, mvp, {1.0f, 1.0f, 1.0f}, 0);
   }
   glBindVertexArray(0);
}

bool Renderer::screenshot(const std::filesystem::path& path, int width, int height, const Camera& camera,
                          const Scene& scene, const Display& display, const std::function<void()>& overlay)
{
   GLuint name = 0;

   glGenRenderbuffers(1, &name);
   const Renderbuffer msaa_color{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, width, height);

   glGenRenderbuffers(1, &name);
   const Renderbuffer msaa_depth{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_DEPTH_COMPONENT24, width, height);

   glGenFramebuffers(1, &name);
   const Framebuffer msaa{name};
   glBindFramebuffer(GL_FRAMEBUFFER, name);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaa_color.get());
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msaa_depth.get());

   glGenRenderbuffers(1, &name);
   const Renderbuffer resolved_color{name};
   glBindRenderbuffer(GL_RENDERBUFFER, name);
   glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, width, height);

   glGenFramebuffers(1, &name);
   const Framebuffer resolved{name};
   glBindFramebuffer(GL_FRAMEBUFFER, name);
   glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, resolved_color.get());

   glBindFramebuffer(GL_FRAMEBUFFER, msaa.get());
   if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
   {
      std::println(stderr, "offscreen framebuffer is incomplete");
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      return false;
   }

   draw(width, height, camera, scene, display);
   if (overlay)
   {
      overlay();
   }

   glBindFramebuffer(GL_READ_FRAMEBUFFER, msaa.get());
   glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolved.get());
   glBlitFramebuffer(0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST);

   glBindFramebuffer(GL_READ_FRAMEBUFFER, resolved.get());
   std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
   glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
   glBindFramebuffer(GL_FRAMEBUFFER, 0);

   stbi_flip_vertically_on_write(1);
   return stbi_write_png(path.string().c_str(), width, height, 4, pixels.data(), width * 4) != 0;
}

}  // namespace testbed
