#include "scenes.hpp"

#include <array>
#include <cmath>
#include <numbers>

namespace testbed
{

using tinyverlet::Vec3;

namespace
{

constexpr std::array<std::string_view, 5> names{"cloth on sphere", "curtain in the wind", "tearable cloth", "ropes",
                                                "soft boxes"};

Scene cloth_on_sphere()
{
   Scene scene;
   auto& world = scene.world;
   world.settings.ground = 0.0f;
   world.spheres.push_back({{0.0f, 1.0f, 0.0f}, 0.6f});

   constexpr std::uint32_t n = 32;
   constexpr float spacing = 0.065f;
   const float half = spacing * static_cast<float>(n - 1) * 0.5f;
   scene.cloths.push_back(tinyverlet::add_cloth(world, {-half, 2.0f, -half}, {spacing, 0, 0}, {0, 0, spacing}, n, n));

   scene.camera_target = {0.0f, 0.9f, 0.0f};
   scene.camera_distance = 4.2f;
   return scene;
}

Scene curtain(bool tearable)
{
   Scene scene;
   auto& world = scene.world;
   world.settings.ground = 0.0f;
   if (tearable)
   {
      world.settings.tear_ratio = 1.35f;
      world.settings.iterations = 12;
   }

   constexpr std::uint32_t columns = 36;
   constexpr std::uint32_t rows = 26;
   constexpr float spacing = 0.07f;
   const float half = spacing * static_cast<float>(columns - 1) * 0.5f;
   const auto grid = tinyverlet::add_cloth(world, {-half, 2.4f, 0.0f}, {spacing, 0, 0}, {0, -spacing, 0}, columns, rows,
                                           {.stiffness = 1.0f, .shear = true, .bend = !tearable});

   // a curtain rod: every second particle of the top row is pinned; the plain
   // curtain is gathered to 65 % of its width so the fabric falls into folds
   const float gather = tearable ? 1.0f : 0.65f;
   for (std::uint32_t c = 0; c < columns; c += tearable ? 1 : 2)
   {
      auto& p = world.particles[grid.at(c, 0)];
      p.position.x *= gather;
      p.previous = p.position;
      p.inverse_mass = 0.0f;
   }

   scene.cloths.push_back(grid);
   scene.camera_target = {0.0f, 1.5f, 0.0f};
   scene.camera_distance = 4.0f;
   scene.wind = tearable ? 0.0f : 4.0f;
   return scene;
}

Scene ropes()
{
   Scene scene;
   auto& world = scene.world;
   world.settings.ground = 0.0f;
   world.spheres.push_back({{0.0f, 0.6f, 0.4f}, 0.4f});

   for (int i = 0; i < 5; ++i)
   {
      const float x = -1.2f + 0.6f * static_cast<float>(i);
      const float length = 1.2f + 0.25f * static_cast<float>(i);
      // start horizontal so they swing down
      const auto rope = tinyverlet::add_rope(world, {x, 2.6f, 0.0f}, {x, 2.6f, length}, 16);
      world.particles[rope.front()].inverse_mass = 0.0f;

      // every other rope carries a small box
      if (i % 2 == 0)
      {
         const auto& end = world.particles[rope.back()].position;
         const auto box = tinyverlet::add_box(world, {end.x, end.y - 0.1f, end.z}, {0.16f, 0.16f, 0.16f});
         world.add_constraint(rope.back(), box + 2);
         world.add_constraint(rope.back(), box + 7);  // opposite top corners keep it level
         scene.boxes.push_back(box);
      }
   }

   scene.camera_target = {0.0f, 1.4f, 0.4f};
   scene.camera_distance = 4.6f;
   scene.show_constraints = true;  // ropes have no surface
   scene.show_particles = true;
   return scene;
}

Scene soft_boxes()
{
   Scene scene;
   auto& world = scene.world;
   world.settings.ground = 0.0f;
   world.settings.bounds = tinyverlet::Box{{-1.5f, -1.0f, -1.5f}, {1.5f, 6.0f, 1.5f}};
   world.settings.restitution = 0.5f;
   world.spheres.push_back({{0.0f, 0.0f, 0.0f}, 0.7f});

   for (int i = 0; i < 9; ++i)
   {
      const float angle = static_cast<float>(i) * 2.399963f;  // golden angle spreads them out
      const float radius = 0.35f + 0.08f * static_cast<float>(i);
      const Vec3 center{radius * std::cos(angle), 1.4f + 0.45f * static_cast<float>(i), radius * std::sin(angle)};
      const float size = 0.3f + 0.05f * static_cast<float>(i % 3);
      const float stiffness = i % 3 == 0 ? 1.0f : (i % 3 == 1 ? 0.25f : 0.06f);  // rigid, soft, squishy
      const auto box = tinyverlet::add_box(world, center, {size, size, size}, stiffness);

      // a little spin: give the top corners some sideways velocity
      for (std::uint32_t corner = 0; corner < 8; ++corner)
      {
         if ((corner & 2) != 0)
         {
            world.particles[box + corner].previous.x += 0.01f;
         }
      }
      scene.boxes.push_back(box);
   }

   scene.camera_target = {0.0f, 1.0f, 0.0f};
   scene.camera_distance = 5.5f;
   scene.show_constraints = true;  // the diagonals show how soft each box is
   return scene;
}

}  // namespace

std::span<const std::string_view> scene_names()
{
   return names;
}

Scene make_scene(std::size_t index)
{
   switch (index)
   {
      case 1: return curtain(false);
      case 2: return curtain(true);
      case 3: return ropes();
      case 4: return soft_boxes();
      default: return cloth_on_sphere();
   }
}

std::vector<tinyverlet::Triangle> box_triangles(std::uint32_t first)
{
   // corner bit 0 = +x, bit 1 = +y, bit 2 = +z (see tinyverlet::add_box); outward winding
   constexpr std::array<std::array<std::uint32_t, 4>, 6> faces{{
       {0, 2, 3, 1},  // -z
       {4, 5, 7, 6},  // +z
       {0, 4, 6, 2},  // -x
       {1, 3, 7, 5},  // +x
       {0, 1, 5, 4},  // -y
       {2, 6, 7, 3},  // +y
   }};
   std::vector<tinyverlet::Triangle> result;
   for (const auto& [a, b, c, d] : faces)
   {
      result.push_back({first + a, first + b, first + c});
      result.push_back({first + a, first + c, first + d});
   }
   return result;
}

}  // namespace testbed
