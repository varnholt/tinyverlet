// tinyverlet.hpp - Verlet integration with distance constraints, C++23, single header
//
// https://github.com/varnholt/tinyverlet
//
// SPDX-License-Identifier: MIT
//
// Usage:
//    tinyverlet::World world;
//    world.settings.ground = 0.0f;
//    const auto cloth = tinyverlet::add_cloth(world, {-1, 2, -1}, {0.1f, 0, 0}, {0, 0, 0.1f}, 21, 21);
//    world.spheres.push_back({{0, 1, 0}, 0.5f});
//    for (int i = 0; i < 120; ++i)
//    {
//       world.step(1.0f / 60.0f);
//    }
//
// Each step does position Verlet integration
//
//    x' = x + (x - x_prev) * damping + (gravity + force * inverse_mass) * dt^2
//
// and then `iterations` rounds of constraint solving and collision handling.
// Constraints are solved one after another (Gauss-Seidel), each moving its two
// particles by their inverse mass; a particle with inverse mass 0 is pinned.
// Units are up to you; the default gravity assumes metres and seconds, y up.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>
#include <vector>

#define TINYVERLET_VERSION_MAJOR 1
#define TINYVERLET_VERSION_MINOR 0
#define TINYVERLET_VERSION_PATCH 0

namespace tinyverlet
{

struct Vec3;

// a 3D vector type of your own: glm::vec3, raylib's Vector3, a plain struct ...
template <class T>
concept xyz_vector = !std::same_as<std::remove_cvref_t<T>, Vec3> && requires(const T& v) {
   { v.x } -> std::convertible_to<float>;
   { v.y } -> std::convertible_to<float>;
   { v.z } -> std::convertible_to<float>;
};

// Plain 3D vector. It converts implicitly from and to your own vector type, so
// you can pass glm::vec3 (or anything with x, y, z) in and read positions back
// out as your type:
//
//    world.add_particle(glm::vec3{0, 1, 0});
//    glm::vec3 p = world.particles[0].position;
struct Vec3
{
   float x = 0.0f;
   float y = 0.0f;
   float z = 0.0f;

   constexpr Vec3() noexcept = default;

   constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_)
   {
   }

   template <xyz_vector T>
   constexpr Vec3(const T& v) noexcept(noexcept(static_cast<float>(v.x)))  // NOLINT: implicit on purpose
       : x(static_cast<float>(v.x)), y(static_cast<float>(v.y)), z(static_cast<float>(v.z))
   {
   }

   constexpr Vec3(const std::array<float, 3>& v) noexcept : x(v[0]), y(v[1]), z(v[2])  // NOLINT: implicit on purpose
   {
   }

   template <xyz_vector T>
      requires std::constructible_from<T, float, float, float> || requires { T{0.0f, 0.0f, 0.0f}; }
   constexpr operator T() const  // NOLINT: implicit on purpose
   {
      if constexpr (std::constructible_from<T, float, float, float>)
      {
         return T(x, y, z);
      }
      else
      {
         return T{x, y, z};
      }
   }

   constexpr operator std::array<float, 3>() const noexcept  // NOLINT: implicit on purpose
   {
      return {x, y, z};
   }

   [[nodiscard]] friend constexpr Vec3 operator+(const Vec3& a, const Vec3& b) noexcept
   {
      return {a.x + b.x, a.y + b.y, a.z + b.z};
   }

   [[nodiscard]] friend constexpr Vec3 operator-(const Vec3& a, const Vec3& b) noexcept
   {
      return {a.x - b.x, a.y - b.y, a.z - b.z};
   }

   [[nodiscard]] friend constexpr Vec3 operator-(const Vec3& a) noexcept
   {
      return {-a.x, -a.y, -a.z};
   }

   [[nodiscard]] friend constexpr Vec3 operator*(const Vec3& a, float s) noexcept
   {
      return {a.x * s, a.y * s, a.z * s};
   }

   constexpr Vec3& operator+=(const Vec3& b) noexcept
   {
      x += b.x;
      y += b.y;
      z += b.z;
      return *this;
   }

   constexpr Vec3& operator-=(const Vec3& b) noexcept
   {
      x -= b.x;
      y -= b.y;
      z -= b.z;
      return *this;
   }

   [[nodiscard]] friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
};

[[nodiscard]] constexpr float dot(const Vec3& a, const Vec3& b) noexcept
{
   return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] constexpr Vec3 cross(const Vec3& a, const Vec3& b) noexcept
{
   return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

[[nodiscard]] inline float length(const Vec3& a) noexcept
{
   return std::sqrt(dot(a, a));
}

using Triangle = std::array<std::uint32_t, 3>;

struct Particle
{
   Vec3 position;
   Vec3 previous;              // position one step ago; position - previous is the velocity times dt
   Vec3 force;                 // accumulated for the next step, then cleared
   float inverse_mass = 1.0f;  // 0 pins the particle in place

   [[nodiscard]] constexpr bool pinned() const noexcept
   {
      return inverse_mass == 0.0f;
   }
};

// keeps two particles at rest_length; stiffness in (0, 1] is the fraction of
// the error corrected per iteration
struct Constraint
{
   std::uint32_t a = 0;
   std::uint32_t b = 0;
   float rest_length = 0.0f;
   float stiffness = 1.0f;
};

// particles are pushed out of spheres
struct Sphere
{
   Vec3 center;
   float radius = 1.0f;
};

// particles are kept inside the box
struct Box
{
   Vec3 min;
   Vec3 max;
};

struct Settings
{
   Vec3 gravity{0.0f, -9.81f, 0.0f};
   float damping = 0.995f;       // fraction of the velocity kept per step
   int iterations = 8;           // constraint and collision rounds per step
   float restitution = 0.3f;     // bounce on collisions: 0 none, 1 elastic
   float friction = 0.3f;        // fraction of the sliding velocity lost on contact
   float tear_ratio = 0.0f;      // > 1 removes constraints stretched beyond rest_length * tear_ratio
   std::optional<float> ground;  // plane y = ground, particles stay above it
   std::optional<Box> bounds;    // particles stay inside
};

namespace detail
{

// moves a particle to `corrected` on the surface with outward normal `n` and
// rewrites its previous position so the implied velocity bounces and slides
inline void resolve_contact(Particle& p, const Vec3& corrected, const Vec3& n, const Settings& settings) noexcept
{
   const Vec3 velocity = p.position - p.previous;
   const float into = dot(velocity, n);
   p.position = corrected;
   if (into < 0.0f)
   {
      const Vec3 normal = n * into;
      const Vec3 tangent = velocity - normal;
      p.previous = p.position - (tangent * (1.0f - settings.friction) - normal * settings.restitution);
   }
}

[[nodiscard]] constexpr std::uint64_t edge_key(std::uint32_t a, std::uint32_t b) noexcept
{
   const auto [lo, hi] = std::minmax(a, b);
   return (std::uint64_t{lo} << 32) | hi;
}

}  // namespace detail

class World
{
public:
   Settings settings;
   std::vector<Particle> particles;
   std::vector<Constraint> constraints;
   std::vector<Sphere> spheres;

   // adds a particle at rest and returns its index
   std::uint32_t add_particle(const Vec3& position, float inverse_mass = 1.0f)
   {
      if (particles.size() >= std::numeric_limits<std::uint32_t>::max())
      {
         throw std::length_error("tinyverlet: too many particles");
      }
      if (!(inverse_mass >= 0.0f))
      {
         throw std::invalid_argument("tinyverlet: inverse_mass must be >= 0");
      }
      particles.push_back({position, position, {}, inverse_mass});
      return static_cast<std::uint32_t>(particles.size() - 1);
   }

   // connects two particles at their current distance and returns the constraint index
   std::uint32_t add_constraint(std::uint32_t a, std::uint32_t b, float stiffness = 1.0f)
   {
      if (a >= particles.size() || b >= particles.size())
      {
         throw std::invalid_argument(std::format("tinyverlet: constraint {}-{} references a missing particle", a, b));
      }
      if (a == b)
      {
         throw std::invalid_argument("tinyverlet: a constraint needs two different particles");
      }
      if (!(stiffness > 0.0f && stiffness <= 1.0f))
      {
         throw std::invalid_argument("tinyverlet: stiffness must be in (0, 1]");
      }
      constraints.push_back({a, b, length(particles[b].position - particles[a].position), stiffness});
      return static_cast<std::uint32_t>(constraints.size() - 1);
   }

   void add_force(std::uint32_t particle, const Vec3& force)
   {
      particles.at(particle).force += force;
   }

   // advances the simulation by dt and returns the number of constraints torn
   std::size_t step(float dt)
   {
      if (!(dt > 0.0f))
      {
         return 0;
      }

      integrate(dt);
      for (int i = 0; i < settings.iterations; ++i)
      {
         solve_constraints();
         collide();
      }
      return tear();
   }

private:
   void integrate(float dt) noexcept
   {
      const float dt2 = dt * dt;
      for (auto& p : particles)
      {
         if (!p.pinned())
         {
            const Vec3 velocity = (p.position - p.previous) * settings.damping;
            p.previous = p.position;
            p.position += velocity + (settings.gravity + p.force * p.inverse_mass) * dt2;
         }
         p.force = {};
      }
   }

   void solve_constraints() noexcept
   {
      for (const auto& c : constraints)
      {
         auto& pa = particles[c.a];
         auto& pb = particles[c.b];
         const float total = pa.inverse_mass + pb.inverse_mass;
         if (total == 0.0f)
         {
            continue;
         }

         const Vec3 delta = pb.position - pa.position;
         const float distance = length(delta);
         if (distance < 1e-9f)
         {
            continue;  // no direction to push in; neighbours will separate them
         }

         const Vec3 correction = delta * ((distance - c.rest_length) / (distance * total) * c.stiffness);
         pa.position += correction * pa.inverse_mass;
         pb.position -= correction * pb.inverse_mass;
      }
   }

   void collide() noexcept
   {
      for (auto& p : particles)
      {
         if (p.pinned())
         {
            continue;
         }

         for (const auto& sphere : spheres)
         {
            const Vec3 offset = p.position - sphere.center;
            const float distance = length(offset);
            if (distance < sphere.radius)
            {
               const Vec3 n = distance > 1e-9f ? offset * (1.0f / distance) : Vec3{0.0f, 1.0f, 0.0f};
               detail::resolve_contact(p, sphere.center + n * sphere.radius, n, settings);
            }
         }

         if (settings.ground && p.position.y < *settings.ground)
         {
            detail::resolve_contact(p, {p.position.x, *settings.ground, p.position.z}, {0.0f, 1.0f, 0.0f}, settings);
         }

         if (settings.bounds)
         {
            const auto& box = *settings.bounds;
            constexpr std::array<float Vec3::*, 3> axes{&Vec3::x, &Vec3::y, &Vec3::z};
            for (const auto axis : axes)
            {
               Vec3 n{};
               if (p.position.*axis < box.min.*axis)
               {
                  n.*axis = 1.0f;
                  Vec3 corrected = p.position;
                  corrected.*axis = box.min.*axis;
                  detail::resolve_contact(p, corrected, n, settings);
               }
               else if (p.position.*axis > box.max.*axis)
               {
                  n.*axis = -1.0f;
                  Vec3 corrected = p.position;
                  corrected.*axis = box.max.*axis;
                  detail::resolve_contact(p, corrected, n, settings);
               }
            }
         }
      }
   }

   std::size_t tear()
   {
      if (!(settings.tear_ratio > 1.0f))
      {
         return 0;
      }
      return std::erase_if(constraints,
                           [this](const Constraint& c)
                           {
                              const float distance = length(particles[c.b].position - particles[c.a].position);
                              return distance > c.rest_length * settings.tear_ratio;
                           });
   }
};

// a rectangular grid of particles, e.g. cloth
struct Grid
{
   std::uint32_t first = 0;  // index of the particle at (0, 0)
   std::uint32_t columns = 0;
   std::uint32_t rows = 0;

   [[nodiscard]] constexpr std::uint32_t at(std::uint32_t column, std::uint32_t row) const noexcept
   {
      return first + row * columns + column;
   }

   // two triangles per cell whose four sides are still connected, so torn
   // cloth leaves holes; counter-clockwise when seen against v x u
   [[nodiscard]] std::vector<Triangle> triangles(const World& world) const
   {
      std::unordered_set<std::uint64_t> connected;
      connected.reserve(world.constraints.size());
      for (const auto& c : world.constraints)
      {
         connected.insert(detail::edge_key(c.a, c.b));
      }
      const auto linked = [&](std::uint32_t a, std::uint32_t b)
      {
         return connected.contains(detail::edge_key(a, b));
      };

      std::vector<Triangle> result;
      for (std::uint32_t r = 0; r + 1 < rows; ++r)
      {
         for (std::uint32_t c = 0; c + 1 < columns; ++c)
         {
            const auto p00 = at(c, r);
            const auto p10 = at(c + 1, r);
            const auto p01 = at(c, r + 1);
            const auto p11 = at(c + 1, r + 1);
            if (linked(p00, p10) && linked(p10, p11) && linked(p11, p01) && linked(p01, p00))
            {
               result.push_back({p00, p01, p11});
               result.push_back({p00, p11, p10});
            }
         }
      }
      return result;
   }
};

struct ClothOptions
{
   float stiffness = 1.0f;
   bool shear = true;  // diagonal constraints, resist skewing
   bool bend = false;  // constraints to the second neighbour, resist folding
   float inverse_mass = 1.0f;
};

// adds columns x rows particles at origin + c * u + r * v, connected to their
// neighbours (each pair once)
inline Grid add_cloth(World& world, const Vec3& origin, const Vec3& u, const Vec3& v, std::uint32_t columns,
                      std::uint32_t rows, const ClothOptions& options = {})
{
   if (columns < 2 || rows < 2)
   {
      throw std::invalid_argument("tinyverlet: cloth needs at least 2 x 2 particles");
   }

   Grid grid{static_cast<std::uint32_t>(world.particles.size()), columns, rows};
   for (std::uint32_t r = 0; r < rows; ++r)
   {
      for (std::uint32_t c = 0; c < columns; ++c)
      {
         world.add_particle(origin + u * static_cast<float>(c) + v * static_cast<float>(r), options.inverse_mass);
      }
   }

   for (std::uint32_t r = 0; r < rows; ++r)
   {
      for (std::uint32_t c = 0; c < columns; ++c)
      {
         const auto p = grid.at(c, r);
         if (c + 1 < columns)
         {
            world.add_constraint(p, grid.at(c + 1, r), options.stiffness);
         }
         if (r + 1 < rows)
         {
            world.add_constraint(p, grid.at(c, r + 1), options.stiffness);
         }
         if (options.shear && c + 1 < columns && r + 1 < rows)
         {
            world.add_constraint(p, grid.at(c + 1, r + 1), options.stiffness);
            world.add_constraint(grid.at(c + 1, r), grid.at(c, r + 1), options.stiffness);
         }
         if (options.bend && c + 2 < columns)
         {
            world.add_constraint(p, grid.at(c + 2, r), options.stiffness);
         }
         if (options.bend && r + 2 < rows)
         {
            world.add_constraint(p, grid.at(c, r + 2), options.stiffness);
         }
      }
   }
   return grid;
}

// adds a chain of segments + 1 particles from `from` to `to`; returns their indices
inline std::vector<std::uint32_t> add_rope(World& world, const Vec3& from, const Vec3& to, std::uint32_t segments,
                                           float stiffness = 1.0f)
{
   if (segments == 0)
   {
      throw std::invalid_argument("tinyverlet: a rope needs at least one segment");
   }
   std::vector<std::uint32_t> indices;
   indices.reserve(segments + 1);
   for (std::uint32_t i = 0; i <= segments; ++i)
   {
      indices.push_back(
          world.add_particle(from + (to - from) * (static_cast<float>(i) / static_cast<float>(segments))));
      if (i > 0)
      {
         world.add_constraint(indices[i - 1], indices[i], stiffness);
      }
   }
   return indices;
}

// adds the 8 corners of an axis-aligned box connected pairwise (28 constraints),
// a rigid-ish body for stiffness 1 and a soft one below; returns the first corner
inline std::uint32_t add_box(World& world, const Vec3& center, const Vec3& size, float stiffness = 1.0f)
{
   const auto first = static_cast<std::uint32_t>(world.particles.size());
   for (int i = 0; i < 8; ++i)
   {
      const Vec3 corner{(i & 1) != 0 ? 0.5f : -0.5f, (i & 2) != 0 ? 0.5f : -0.5f, (i & 4) != 0 ? 0.5f : -0.5f};
      world.add_particle({center.x + corner.x * size.x, center.y + corner.y * size.y, center.z + corner.z * size.z});
   }
   for (std::uint32_t a = 0; a < 8; ++a)
   {
      for (std::uint32_t b = a + 1; b < 8; ++b)
      {
         world.add_constraint(first + a, first + b, stiffness);
      }
   }
   return first;
}

// Aerodynamic push on a surface for the next step. `wind` is the acceleration a
// sheet facing it squarely gets; a sheet seen edge-on gets none. Each particle
// receives the area-weighted average of n * dot(n, wind) over its triangles, so
// the result does not depend on the cloth resolution or the particle masses.
inline void add_wind(World& world, std::span<const Triangle> triangles, const Vec3& wind)
{
   std::vector<Vec3> push(world.particles.size());
   std::vector<float> area_sum(world.particles.size(), 0.0f);

   for (const auto& [a, b, c] : triangles)
   {
      const auto& pa = world.particles.at(a).position;
      const Vec3 area_normal = cross(world.particles.at(b).position - pa, world.particles.at(c).position - pa) * 0.5f;
      const float area = length(area_normal);
      if (area <= 0.0f)
      {
         continue;
      }
      const Vec3 n = area_normal * (1.0f / area);
      const Vec3 weighted = n * (dot(n, wind) * area);
      for (const auto index : {a, b, c})
      {
         push[index] += weighted;
         area_sum[index] += area;
      }
   }

   for (std::size_t i = 0; i < world.particles.size(); ++i)
   {
      auto& p = world.particles[i];
      if (area_sum[i] > 0.0f && !p.pinned())
      {
         p.force += push[i] * (1.0f / (area_sum[i] * p.inverse_mass));  // force = mass * acceleration
      }
   }
}

}  // namespace tinyverlet
