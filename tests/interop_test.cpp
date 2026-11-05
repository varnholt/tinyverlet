// Vec3 converts from and to the user's own vector types.
#include <tinyverlet/tinyverlet.hpp>

#include <array>
#include <print>

namespace
{

struct PlainVec  // aggregate, like raylib's Vector3
{
   float x, y, z;
};

class GlmLikeVec  // constructor from three floats, like glm::vec3
{
public:
   float x = 0, y = 0, z = 0;
   GlmLikeVec() = default;
   GlmLikeVec(float x_, float y_, float z_) : x(x_), y(y_), z(z_)
   {
   }
};

struct DoubleVec  // other scalar types convert too
{
   double x, y, z;
};

struct NotAVector
{
   float a, b;
};

}  // namespace

static_assert(std::is_convertible_v<PlainVec, tinyverlet::Vec3>);
static_assert(std::is_convertible_v<tinyverlet::Vec3, PlainVec>);
static_assert(std::is_convertible_v<GlmLikeVec, tinyverlet::Vec3>);
static_assert(std::is_convertible_v<tinyverlet::Vec3, GlmLikeVec>);
static_assert(std::is_convertible_v<DoubleVec, tinyverlet::Vec3>);
static_assert(std::is_convertible_v<std::array<float, 3>, tinyverlet::Vec3>);
static_assert(std::is_convertible_v<tinyverlet::Vec3, std::array<float, 3>>);
static_assert(!std::is_convertible_v<NotAVector, tinyverlet::Vec3>);
static_assert(!std::is_convertible_v<tinyverlet::Vec3, NotAVector>);
static_assert(!std::is_convertible_v<tinyverlet::Vec3, float>);

int main()
{
   tinyverlet::World world;
   world.settings.gravity = GlmLikeVec{0.0f, -1.0f, 0.0f};
   const auto a = world.add_particle(PlainVec{1, 2, 3});
   const auto b = world.add_particle(GlmLikeVec{4, 5, 6});
   const auto c = world.add_particle(DoubleVec{7, 8, 9});
   const auto d = world.add_particle(std::array{0.0f, 0.0f, 1.0f});
   world.add_constraint(a, b);

   const PlainVec pa = world.particles[a].position;
   const GlmLikeVec pb = world.particles[b].position;
   const std::array<float, 3> pd = world.particles[d].position;
   const bool ok = pa.x == 1 && pa.z == 3 && pb.y == 5 && world.particles[c].position.x == 7.0f && pd[2] == 1.0f;
   std::println("{}", ok ? "interop ok" : "interop FAILED");
   return ok ? 0 : 1;
}
