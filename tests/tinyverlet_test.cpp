#include <tinyverlet/tinyverlet.hpp>

#include <cmath>
#include <print>
#include <source_location>
#include <string_view>

using tinyverlet::Vec3;
using tinyverlet::World;

namespace
{

int failures = 0;

void check(bool ok, std::string_view what, const std::source_location& where = std::source_location::current())
{
   if (!ok)
   {
      ++failures;
      std::println(stderr, "{}:{}: FAILED: {}", where.file_name(), where.line(), what);
   }
}

bool near(float a, float b, float eps = 1e-4f)
{
   return std::abs(a - b) <= eps;
}

bool finite(const World& world)
{
   for (const auto& p : world.particles)
   {
      if (!std::isfinite(p.position.x) || !std::isfinite(p.position.y) || !std::isfinite(p.position.z))
      {
         return false;
      }
   }
   return true;
}

void test_free_fall()
{
   // Verlet from rest: after n steps the drop is g dt^2 n (n + 1) / 2
   World world;
   world.settings.damping = 1.0f;
   world.settings.gravity = {0.0f, -10.0f, 0.0f};
   world.add_particle({0, 100, 0});

   constexpr float dt = 0.01f;
   constexpr int n = 50;
   for (int i = 0; i < n; ++i)
   {
      world.step(dt);
   }
   const float expected = 100.0f - 10.0f * dt * dt * n * (n + 1) / 2.0f;
   check(near(world.particles[0].position.y, expected, 1e-3f), "free fall follows the Verlet closed form");
   check(world.particles[0].position.x == 0.0f, "no sideways drift");
}

void test_pinned_and_forces()
{
   World world;
   const auto pinned = world.add_particle({0, 1, 0}, 0.0f);
   const auto free = world.add_particle({1, 1, 0});
   world.add_force(pinned, {100, 0, 0});
   for (int i = 0; i < 30; ++i)
   {
      world.step(1.0f / 60.0f);
   }
   check(world.particles[pinned].position == Vec3{0, 1, 0}, "pinned particle does not move");
   check(world.particles[free].position.y < 1.0f, "free particle falls");

   // a force acts for one step only, scaled by the inverse mass
   World pushed;
   pushed.settings.gravity = {};
   pushed.settings.damping = 1.0f;
   const auto light = pushed.add_particle({0, 0, 0}, 2.0f);
   const auto heavy = pushed.add_particle({0, 0, 1}, 0.5f);
   pushed.add_force(light, {1, 0, 0});
   pushed.add_force(heavy, {1, 0, 0});
   pushed.step(0.1f);
   check(near(pushed.particles[light].position.x, 2.0f * 0.01f), "force times inverse mass times dt^2");
   check(near(pushed.particles[heavy].position.x, 0.5f * 0.01f), "heavier particle moves less");
   check(pushed.particles[light].force == Vec3{}, "forces are cleared after a step");
}

void test_constraint_converges()
{
   World world;
   world.settings.gravity = {};
   const auto a = world.add_particle({0, 0, 0});
   const auto b = world.add_particle({1, 0, 0});
   world.add_constraint(a, b);

   world.particles[b].position = {3, 0, 0};  // stretch it
   world.particles[b].previous = {3, 0, 0};
   world.step(0.01f);
   const float distance = tinyverlet::length(world.particles[b].position - world.particles[a].position);
   check(near(distance, 1.0f), "stiff constraint restores the rest length");
   check(near(world.particles[a].position.x + world.particles[b].position.x, 3.0f), "equal masses meet in the middle");

   // a pinned end does not move; the free end takes all the correction
   World anchored;
   anchored.settings.gravity = {};
   const auto anchor = anchored.add_particle({0, 0, 0}, 0.0f);
   const auto end = anchored.add_particle({1, 0, 0});
   anchored.add_constraint(anchor, end);
   anchored.particles[end].position = anchored.particles[end].previous = {0, 2, 0};
   anchored.step(0.01f);
   check(anchored.particles[anchor].position == Vec3{}, "pinned end stays");
   check(near(anchored.particles[end].position.y, 1.0f), "free end moves back to the rest length");
}

void test_degenerate_inputs()
{
   // two particles on top of each other with a non-zero rest length: no NaN
   World world;
   const auto a = world.add_particle({0, 0, 0});
   const auto b = world.add_particle({1, 0, 0});
   world.add_constraint(a, b);
   world.particles[b].position = world.particles[b].previous = {0, 0, 0};
   for (int i = 0; i < 10; ++i)
   {
      world.step(1.0f / 60.0f);
   }
   check(finite(world), "coincident particles do not produce NaN");

   check(world.step(0.0f) == 0 && world.step(-1.0f) == 0, "non-positive dt is ignored");

   const auto throws = [](auto&& f)
   {
      try
      {
         f();
      }
      catch (const std::invalid_argument&)
      {
         return true;
      }
      return false;
   };
   check(throws(
             [&]
             {
                world.add_constraint(a, 99);
             }),
         "missing particle throws");
   check(throws(
             [&]
             {
                world.add_constraint(a, a);
             }),
         "self constraint throws");
   check(throws(
             [&]
             {
                world.add_constraint(a, b, 0.0f);
             }),
         "zero stiffness throws");
   check(throws(
             [&]
             {
                world.add_particle({}, -1.0f);
             }),
         "negative inverse mass throws");
}

void test_collisions()
{
   World world;
   world.settings.ground = 0.0f;
   world.settings.bounds = tinyverlet::Box{{-1, -10, -1}, {1, 10, 1}};
   world.spheres.push_back({{0, 1, 0}, 0.5f});

   // a cloth dropped on the sphere, plus fast particles thrown at the walls
   tinyverlet::add_cloth(world, {-0.9f, 2, -0.9f}, {0.1f, 0, 0}, {0, 0, 0.1f}, 19, 19);
   const auto thrown = world.add_particle({0, 3, 0});
   world.particles[thrown].previous = {-0.5f, 3, 0.3f};

   bool inside_sphere = false;
   bool outside_bounds = false;
   float centre_after_one_second = 0.0f;
   for (int i = 0; i < 300; ++i)
   {
      world.step(1.0f / 60.0f);
      if (i == 59)
      {
         centre_after_one_second = world.particles[9 * 19 + 9].position.y;
      }
      for (const auto& p : world.particles)
      {
         inside_sphere = inside_sphere || tinyverlet::length(p.position - Vec3{0, 1, 0}) < 0.5f - 1e-3f;
         outside_bounds = outside_bounds || p.position.y < -1e-4f || std::abs(p.position.x) > 1.0f + 1e-4f ||
                          std::abs(p.position.z) > 1.0f + 1e-4f;
      }
   }
   check(!inside_sphere, "nothing ends a step inside the sphere");
   check(!outside_bounds, "everything stays above the ground and inside the box");
   check(finite(world), "collisions stay finite");

   // the centre of the cloth lands on top of the sphere (later it may slide off,
   // as Gauss-Seidel ordering slowly breaks the symmetry)
   check(near(centre_after_one_second, 1.5f, 0.02f), "cloth drapes over the sphere");
}

void test_restitution()
{
   const auto bounce_height = [](float restitution)
   {
      World world;
      world.settings.ground = 0.0f;
      world.settings.damping = 1.0f;
      world.settings.restitution = restitution;
      world.add_particle({0, 1, 0});
      float peak = 0.0f;
      bool landed = false;
      for (int i = 0; i < 240; ++i)
      {
         world.step(1.0f / 240.0f);
         const float y = world.particles[0].position.y;
         landed = landed || y <= 1e-6f;
         if (landed)
         {
            peak = std::max(peak, y);
         }
      }
      return peak;
   };

   const float dead = bounce_height(0.0f);
   const float bouncy = bounce_height(0.8f);
   check(dead < 0.01f, "restitution 0 does not bounce");
   check(bouncy > 0.4f && bouncy < 0.8f, "restitution 0.8 bounces to roughly 0.64 of the drop");
}

void test_cloth_and_tearing()
{
   World world;
   const auto grid = tinyverlet::add_cloth(world, {}, {1, 0, 0}, {0, 0, 1}, 4, 3);
   check(world.particles.size() == 12, "4 x 3 particles");
   // structural 3*3 + 4*2 = 17, shear 2 * 3 * 2 = 12
   check(world.constraints.size() == 29, "structural and shear constraints, each pair once");
   check(grid.triangles(world).size() == 12, "two triangles per cell");

   World bent;
   tinyverlet::add_cloth(bent, {}, {1, 0, 0}, {0, 0, 1}, 4, 3, {.shear = false, .bend = true});
   // bend: 2 per row of 4 (3 rows) and 1 per column of 3 (4 columns)
   check(bent.constraints.size() == 17 + 6 + 4, "bend constraints skip one particle");

   // pin the top row and yank the bottom row away: the cloth tears
   World tearing;
   tearing.settings.tear_ratio = 1.5f;
   tearing.settings.gravity = {};
   const auto sheet = tinyverlet::add_cloth(tearing, {}, {0.1f, 0, 0}, {0, -0.1f, 0}, 5, 5);
   for (std::uint32_t c = 0; c < 5; ++c)
   {
      tearing.particles[sheet.at(c, 0)].inverse_mass = 0.0f;
      auto& bottom = tearing.particles[sheet.at(c, 4)];
      bottom.inverse_mass = 0.0f;
      bottom.position.y -= 1.0f;
   }
   const auto before = sheet.triangles(tearing).size();
   const auto torn = tearing.step(1.0f / 60.0f);
   check(torn > 0, "overstretched constraints are removed");
   check(sheet.triangles(tearing).size() < before, "torn cells are no longer rendered");
}

void test_rope_box_wind()
{
   World world;
   world.settings.damping = 0.9f;  // settle quickly; we only check where it comes to rest
   const auto rope = tinyverlet::add_rope(world, {0, 2, 0}, {2, 2, 0}, 10);
   check(rope.size() == 11 && world.constraints.size() == 10, "rope of 10 segments");
   world.particles[rope.front()].inverse_mass = 0.0f;
   for (int i = 0; i < 600; ++i)
   {
      world.step(1.0f / 60.0f);
   }
   const auto& end = world.particles[rope.back()].position;
   check(near(end.x, 0.0f, 0.01f) && near(end.y, 0.0f, 0.05f), "rope pinned at one end hangs straight down");

   World boxes;
   boxes.settings.ground = 0.0f;
   const auto first = tinyverlet::add_box(boxes, {0, 2, 0}, {1, 1, 1});
   check(boxes.constraints.size() == 28, "box corners are connected pairwise");
   for (int i = 0; i < 240; ++i)
   {
      boxes.step(1.0f / 60.0f);
   }
   const float edge = tinyverlet::length(boxes.particles[first + 1].position - boxes.particles[first].position);
   check(near(edge, 1.0f, 0.02f), "stiff box keeps its shape after landing");

   // wind pushes a sheet along its normal
   World windy;
   windy.settings.gravity = {};
   const auto sheet = tinyverlet::add_cloth(windy, {}, {1, 0, 0}, {0, 1, 0}, 3, 3);
   const auto triangles = sheet.triangles(windy);
   windy.settings.damping = 1.0f;
   tinyverlet::add_wind(windy, triangles, {0, 0, 2});
   windy.step(0.1f);
   bool all_pushed = true;
   for (const auto& p : windy.particles)
   {
      all_pushed = all_pushed && near(p.position.z, 2.0f * 0.01f);
   }
   check(all_pushed, "a sheet facing the wind gets exactly the wind acceleration");

   World edge_on;
   edge_on.settings.gravity = {};
   const auto side = tinyverlet::add_cloth(edge_on, {}, {1, 0, 0}, {0, 1, 0}, 3, 3);
   tinyverlet::add_wind(edge_on, side.triangles(edge_on), {2, 0, 0});
   edge_on.step(0.1f);
   check(edge_on.particles[4].position == Vec3{1, 1, 0}, "wind along the sheet does not push it");
}

void test_determinism()
{
   const auto run = []
   {
      World world;
      world.settings.ground = 0.0f;
      world.spheres.push_back({{0, 1, 0}, 0.6f});
      tinyverlet::add_cloth(world, {-1, 2, -1}, {0.1f, 0, 0}, {0, 0, 0.1f}, 21, 21);
      for (int i = 0; i < 120; ++i)
      {
         world.step(1.0f / 60.0f);
      }
      return world.particles;
   };
   const auto a = run();
   const auto b = run();
   bool same = a.size() == b.size();
   for (std::size_t i = 0; same && i < a.size(); ++i)
   {
      same = a[i].position == b[i].position;
   }
   check(same, "same input, same result");
}

}  // namespace

int main()
{
   test_free_fall();
   test_pinned_and_forces();
   test_constraint_converges();
   test_degenerate_inputs();
   test_collisions();
   test_restitution();
   test_cloth_and_tearing();
   test_rope_box_wind();
   test_determinism();

   if (failures > 0)
   {
      std::println(stderr, "{} check(s) failed", failures);
      return 1;
   }
   std::println("all checks passed");
   return 0;
}
