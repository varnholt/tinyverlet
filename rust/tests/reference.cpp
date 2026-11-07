#include <tinyverlet/tinyverlet.hpp>
#include <iomanip>
#include <iostream>
int main() {
    tinyverlet::World world;
    world.settings.ground=0.0f;
    world.settings.particle_radius=0.035f;
    world.spheres.push_back({{0.25f,0.3f,0.25f},0.25f});
    const auto grid=tinyverlet::add_cloth(world,{0,1,0},{0.1f,0,0},{0,0,0.1f},6,6);
    world.particles[0].inverse_mass=0;
    world.particles[5].inverse_mass=0;
    for(int i=0;i<120;++i) {
        tinyverlet::add_wind(world,grid.triangles(world),{0.2f,0.1f,0.3f});
        world.step(1.0f/120.0f);
    }
    std::cout << std::setprecision(9);
    for(const auto& p:world.particles) std::cout << p.position.x << ' ' << p.position.y << ' ' << p.position.z << '\n';
}
