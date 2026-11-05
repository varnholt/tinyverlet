#pragma once

#include <tinyverlet/tinyverlet.hpp>

#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace testbed
{

struct Scene
{
   tinyverlet::World world;
   std::vector<tinyverlet::Grid> cloths;  // drawn as smooth surfaces, react to wind
   std::vector<std::uint32_t> boxes;      // first corner of each box, drawn flat shaded
   tinyverlet::Vec3 camera_target;
   float camera_distance = 4.0f;
   float wind = 0.0f;  // default wind strength
   bool show_constraints = false;
   bool show_particles = false;
};

[[nodiscard]] std::span<const std::string_view> scene_names();
[[nodiscard]] Scene make_scene(std::size_t index);

// the 12 triangles of a box added with tinyverlet::add_box
[[nodiscard]] std::vector<tinyverlet::Triangle> box_triangles(std::uint32_t first);

}  // namespace testbed
