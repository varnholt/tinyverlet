use tinyverlet::*;
#[test]
fn cloth_contacts_and_wind_match_cpp() {
    let mut world = World::default();
    world.settings.ground = Some(0.);
    world.settings.particle_radius = 0.035;
    world.spheres.push(Sphere {
        center: Vec3::new(0.25, 0.3, 0.25),
        radius: 0.25,
    });
    let grid = add_cloth(
        &mut world,
        Vec3::new(0., 1., 0.),
        Vec3::new(0.1, 0., 0.),
        Vec3::new(0., 0., 0.1),
        6,
        6,
        ClothOptions::default(),
    )
    .unwrap();
    world.particles[0].inverse_mass = 0.;
    world.particles[5].inverse_mass = 0.;
    for _ in 0..120 {
        let triangles = grid.triangles(&world).unwrap();
        add_wind(&mut world, &triangles, Vec3::new(0.2, 0.1, 0.3)).unwrap();
        world.step(1. / 120.).unwrap();
    }
    let mut words = include_str!("data/cpp_reference.txt").split_whitespace();
    for (i, p) in world.particles.iter().enumerate() {
        for actual in <[f32; 3]>::from(p.position) {
            let expected: f32 = words.next().unwrap().parse().unwrap();
            assert!(
                (actual - expected).abs() < 1e-4,
                "particle {i}: {actual} != {expected}"
            );
        }
    }
    assert!(words.next().is_none());
}
