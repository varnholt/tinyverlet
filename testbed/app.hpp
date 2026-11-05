#pragma once

#include "gl.hpp"
#include "renderer.hpp"
#include "scenes.hpp"

#include <tinyverlet/tinyverlet.hpp>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>

namespace testbed
{

constexpr float step_dt = 1.0f / 120.0f;  // fixed simulation step
constexpr int max_steps_per_frame = 8;    // drop time rather than spiral when a frame is slow

struct Grab
{
   std::uint32_t particle = 0;
   float distance = 0.0f;      // along the mouse ray
   float inverse_mass = 1.0f;  // restored on release
};

struct App
{
   GLFWwindow* window = nullptr;
   std::optional<Renderer> renderer;
   std::size_t scene_index = 0;
   Scene scene;
   Camera camera;
   Display display;

   bool paused = false;
   bool wind_enabled = false;
   float wind_strength = 0.0f;
   double time = 0.0;  // simulated seconds
   double accumulator = 0.0;
   std::size_t torn = 0;
   double step_milliseconds = 0.0;
   bool show_fps = true;
   std::string status;

   // mouse
   bool orbiting = false;
   std::optional<Grab> grab;
   double last_x = 0.0;
   double last_y = 0.0;

   void load_scene(std::size_t index)
   {
      scene_index = index;
      scene = make_scene(index);
      camera.target = scene.camera_target;
      camera.distance = scene.camera_distance;
      wind_strength = scene.wind;
      display.constraints = scene.show_constraints;
      display.particles = scene.show_particles;
      wind_enabled = scene.wind > 0.0f;
      time = 0.0;
      accumulator = 0.0;
      torn = 0;
      grab.reset();
      renderer->set_scene(scene);
   }

   void step()
   {
      if (wind_enabled && wind_strength > 0.0f)
      {
         // gusty breeze from the front, slowly turning
         const auto t = static_cast<float>(time);
         const float gust = 1.0f + 0.6f * std::sin(t * 1.3f) + 0.3f * std::sin(t * 3.7f);
         const tinyverlet::Vec3 wind{0.4f * std::sin(t * 0.5f), 0.0f, 1.0f};
         for (const auto& grid : scene.cloths)
         {
            tinyverlet::add_wind(scene.world, grid.triangles(scene.world), wind * (wind_strength * gust));
         }
      }

      const auto start = std::chrono::steady_clock::now();
      torn += scene.world.step(step_dt);
      const auto elapsed = std::chrono::steady_clock::now() - start;
      step_milliseconds = 0.9 * step_milliseconds + 0.1 * std::chrono::duration<double, std::milli>(elapsed).count();
      time += step_dt;
   }

   // advances by real time in fixed steps
   void advance(double seconds)
   {
      if (paused)
      {
         return;
      }
      accumulator += seconds;
      int steps = 0;
      while (accumulator >= step_dt && steps < max_steps_per_frame)
      {
         step();
         accumulator -= step_dt;
         ++steps;
      }
      if (steps == max_steps_per_frame)
      {
         accumulator = 0.0;
      }
   }

   // mouse ray through window coordinates
   [[nodiscard]] std::pair<tinyverlet::Vec3, tinyverlet::Vec3> mouse_ray(double x, double y) const
   {
      int width = 0;
      int height = 0;
      glfwGetWindowSize(window, &width, &height);
      const auto w = static_cast<float>(std::max(width, 1));
      const auto h = static_cast<float>(std::max(height, 1));
      const float ndc_x = 2.0f * static_cast<float>(x) / w - 1.0f;
      const float ndc_y = 1.0f - 2.0f * static_cast<float>(y) / h;
      return {camera.eye(), camera.ray(ndc_x, ndc_y, w / h)};
   }

   // grabs the particle closest to the camera among those near the ray
   bool try_grab(double x, double y)
   {
      const auto [origin, direction] = mouse_ray(x, y);
      std::optional<std::uint32_t> best;
      float best_t = 1e30f;
      const float tolerance = 0.04f * camera.distance;
      const auto& particles = scene.world.particles;
      for (std::uint32_t i = 0; i < particles.size(); ++i)
      {
         const auto offset = particles[i].position - origin;
         const float t = tinyverlet::dot(offset, direction);
         const auto closest = origin + direction * t;
         if (t > 0.0f && t < best_t && tinyverlet::length(particles[i].position - closest) < tolerance)
         {
            best = i;
            best_t = t;
         }
      }
      if (!best)
      {
         return false;
      }
      grab = Grab{*best, best_t, particles[*best].inverse_mass};
      scene.world.particles[*best].inverse_mass = 0.0f;  // held by the mouse
      return true;
   }

   void drag(double x, double y)
   {
      if (!grab)
      {
         return;
      }
      const auto [origin, direction] = mouse_ray(x, y);
      auto& p = scene.world.particles[grab->particle];
      p.previous = p.position;  // so a release flings it with the last mouse motion
      p.position = origin + direction * grab->distance;
   }

   void release()
   {
      if (grab && grab->particle < scene.world.particles.size())
      {
         scene.world.particles[grab->particle].inverse_mass = grab->inverse_mass;
      }
      grab.reset();
   }
};

// the ImGui control panel
void draw_ui(App& app);

}  // namespace testbed
