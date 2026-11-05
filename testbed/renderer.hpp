#pragma once

#include "gl.hpp"
#include "math.hpp"
#include "scenes.hpp"

#include <tinyverlet/tinyverlet.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <utility>
#include <vector>

namespace testbed
{

// owns one GL object name and frees it with the matching glDelete* call
template <void (*Delete)(GLuint)>
class GlObject
{
public:
   GlObject() = default;
   explicit GlObject(GLuint name) : name_(name)
   {
   }
   GlObject(GlObject&& other) noexcept : name_(std::exchange(other.name_, 0))
   {
   }
   GlObject& operator=(GlObject&& other) noexcept
   {
      std::swap(name_, other.name_);
      return *this;
   }
   GlObject(const GlObject&) = delete;
   GlObject& operator=(const GlObject&) = delete;
   ~GlObject()
   {
      if (name_ != 0)
      {
         Delete(name_);
      }
   }

   [[nodiscard]] GLuint get() const noexcept
   {
      return name_;
   }

private:
   GLuint name_ = 0;
};

void delete_buffer(GLuint name);
void delete_vertex_array(GLuint name);
void delete_program(GLuint name);
void delete_framebuffer(GLuint name);
void delete_renderbuffer(GLuint name);

using Buffer = GlObject<delete_buffer>;
using VertexArray = GlObject<delete_vertex_array>;
using Program = GlObject<delete_program>;
using Framebuffer = GlObject<delete_framebuffer>;
using Renderbuffer = GlObject<delete_renderbuffer>;

struct Camera
{
   tinyverlet::Vec3 target;
   float yaw = 0.5f;
   float pitch = 0.35f;
   float distance = 4.0f;
   float fov_y = 0.8f;

   [[nodiscard]] Mat4 view() const;
   [[nodiscard]] tinyverlet::Vec3 eye() const;
   // world-space direction through a point given in normalised device coordinates
   [[nodiscard]] tinyverlet::Vec3 ray(float ndc_x, float ndc_y, float aspect) const;
};

struct Display
{
   bool surfaces = true;
   bool constraints = true;
   bool particles = true;
   std::array<float, 3> cloth_color{0.86f, 0.38f, 0.32f};
};

class Renderer
{
public:
   Renderer();

   // static geometry: spheres, ground grid, bounds; call when the scene changes
   void set_scene(const Scene& scene);

   void draw(int width, int height, const Camera& camera, const Scene& scene, const Display& display);

   // renders one frame offscreen with 4x MSAA and writes it as PNG;
   // `overlay` draws on top of the scene, e.g. the ImGui panel
   bool screenshot(const std::filesystem::path& path, int width, int height, const Camera& camera, const Scene& scene,
                   const Display& display, const std::function<void()>& overlay = {});

private:
   struct Mesh
   {
      VertexArray vao;
      Buffer vertices;
      Buffer indices;
      GLsizei count = 0;
   };

   static Mesh make_mesh(GLenum usage);
   static void upload(Mesh& mesh, const std::vector<tinyverlet::Vec3>& interleaved,
                      const std::vector<std::uint32_t>& indices, GLenum usage);
   void draw_mesh(const Mesh& mesh, GLenum mode, const Mat4& mvp, std::array<float, 3> color, int shading,
                  std::array<float, 4> transform = {0.0f, 0.0f, 0.0f, 1.0f}) const;
   void upload_unit_sphere();

   Program program_;
   GLint mvp_location_ = -1;
   GLint color_location_ = -1;
   GLint shading_location_ = -1;
   GLint transform_location_ = -1;

   Mesh spheres_;
   Mesh grid_;
   Mesh cloth_;
   Mesh boxes_;
   Mesh lines_;
   Mesh points_;

   std::vector<tinyverlet::Vec3> scratch_vertices_;
   std::vector<std::uint32_t> scratch_indices_;
};

}  // namespace testbed
