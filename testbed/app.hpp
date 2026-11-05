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
#include <vector>

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

// a sphere thrown from the camera; it moves on its own and pushes particles away
struct Ball
{
   tinyverlet::Vec3 velocity;
   float age = 0.0f;
};

constexpr float ball_radius = 0.22f;
constexpr float ball_speed = 11.0f;
constexpr float ball_lifetime = 5.0f;

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
   float collision_radius = 0.03f;  // remembered while self-collision is switched off
   std::vector<Ball> balls;         // the last balls.size() spheres of the world
   std::size_t scene_spheres = 0;   // spheres that belong to the scene itself
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
      balls.clear();
      scene_spheres = scene.world.spheres.size();
      if (scene.world.settings.particle_radius > 0.0f)
      {
         collision_radius = scene.world.settings.particle_radius;
      }
      renderer->set_scene(scene);
   }

   void throw_ball()
   {
      // lob it so that it arrives at the camera target despite gravity:
      // d = v t + g t^2 / 2  =>  v = d / t - g t / 2
      const auto eye = camera.eye();
      const auto towards = camera.target - eye;
      const float distance = tinyverlet::length(towards);
      const auto start = eye + towards * (0.5f / distance);
      const auto flight = camera.target - start;
      const float t = tinyverlet::length(flight) / ball_speed;
      scene.world.spheres.push_back({start, ball_radius});
      balls.push_back({flight * (1.0f / t) - scene.world.settings.gravity * (0.5f * t)});
   }

   void move_balls()
   {
      auto& spheres = scene.world.spheres;
      const auto& settings = scene.world.settings;
      for (std::size_t i = 0; i < balls.size(); ++i)
      {
         auto& ball = balls[i];
         auto& sphere = spheres[scene_spheres + i];
         ball.velocity += settings.gravity * step_dt;
         sphere.center += ball.velocity * step_dt;
         ball.age += step_dt;
         if (settings.ground && sphere.center.y < *settings.ground + sphere.radius)
         {
            sphere.center.y = *settings.ground + sphere.radius;
            ball.velocity = {ball.velocity.x * 0.9f, -ball.velocity.y * 0.5f, ball.velocity.z * 0.9f};
         }
      }

      // drop expired balls together with their spheres
      for (std::size_t i = balls.size(); i-- > 0;)
      {
         if (balls[i].age > ball_lifetime)
         {
            balls.erase(balls.begin() + static_cast<std::ptrdiff_t>(i));
            spheres.erase(spheres.begin() + static_cast<std::ptrdiff_t>(scene_spheres + i));
         }
      }
   }

   void step()
   {
      move_balls();

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
