// The testbed's scenes must build and simulate without blowing up.
#include "scenes.hpp"

#include <cmath>
#include <print>

int main()
{
   int failures = 0;
   const auto fail = [&](std::string_view scene, std::string_view what)
   {
      ++failures;
      std::println(stderr, "{}: {}", scene, what);
   };

   // box faces point outward: positive signed volume
   {
      tinyverlet::World world;
      const auto first = tinyverlet::add_box(world, {0, 0, 0}, {1, 1, 1});
      float volume = 0.0f;
      for (const auto& [a, b, c] : testbed::box_triangles(first))
      {
         volume += tinyverlet::dot(world.particles[a].position,
                                   tinyverlet::cross(world.particles[b].position, world.particles[c].position));
      }
      if (std::abs(volume / 6.0f - 1.0f) > 1e-5f)
      {
         fail("box", "faces do not enclose the unit box outward");
      }
   }

   for (std::size_t i = 0; i < testbed::scene_names().size(); ++i)
   {
      const auto name = testbed::scene_names()[i];
      auto scene = testbed::make_scene(i);
      for (int step = 0; step < 240; ++step)
      {
         scene.world.step(1.0f / 120.0f);
      }
      for (const auto& p : scene.world.particles)
      {
         if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.position.z) ||
             std::abs(p.position.x) > 50.0f || std::abs(p.position.y) > 50.0f || std::abs(p.position.z) > 50.0f)
         {
            fail(name, "particle escaped or became NaN");
            break;
         }
      }
      if (scene.world.settings.ground)
      {
         for (const auto& p : scene.world.particles)
         {
            if (p.position.y < *scene.world.settings.ground - 1e-4f)
            {
               fail(name, "particle below the ground");
               break;
            }
         }
      }
   }

   if (failures == 0)
   {
      std::println("all scenes valid");
   }
   return failures == 0 ? 0 : 1;
}
