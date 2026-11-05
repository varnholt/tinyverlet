#include <tinyverlet/tinyverlet.hpp>

int main()
{
   tinyverlet::World world;
   world.settings.ground = 0.0f;
   world.add_particle({0, 1, 0});
   for (int i = 0; i < 120; ++i)
   {
      world.step(1.0f / 60.0f);
   }
   return world.particles[0].position.y >= 0.0f ? 0 : 1;
}
