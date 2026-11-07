use tinyverlet::*;
#[test]
fn forces_mass_and_pins() {
    let mut w = World::default();
    w.settings.gravity = Vec3::default();
    w.settings.iterations = 0;
    let a = w.add_particle([0., 0., 0.], 2.).unwrap();
    let b = w.add_particle([1., 0., 0.], 0.).unwrap();
    w.add_force(a, [3., 0., 0.]).unwrap();
    w.add_force(b, [99., 0., 0.]).unwrap();
    w.step(0.5).unwrap();
    assert_eq!(w.particles[0].position, Vec3::new(1.5, 0., 0.));
    assert_eq!(w.particles[1].position, Vec3::new(1., 0., 0.));
    assert_eq!(w.particles[0].force, Vec3::default());
}
#[test]
fn rope_preserves_lengths_and_anchor() {
    let mut w = World::default();
    w.settings.iterations = 30;
    let rope = add_rope(&mut w, Vec3::new(0., 2., 0.), Vec3::new(1., 2., 0.), 10, 1.).unwrap();
    w.particles[0].inverse_mass = 0.;
    for _ in 0..600 {
        w.step(1. / 120.).unwrap();
    }
    assert_eq!(w.particles[0].position, Vec3::new(0., 2., 0.));
    assert_eq!(rope.len(), 11);
    for c in &w.constraints {
        let d = length(w.particles[c.a as usize].position - w.particles[c.b as usize].position);
        assert!((d - c.rest_length).abs() < 0.002);
    }
}
#[test]
fn ground_restitution_and_bounds() {
    let mut w = World::default();
    w.settings.gravity = Vec3::default();
    w.settings.damping = 1.;
    w.settings.ground = Some(0.);
    w.settings.restitution = 1.;
    w.settings.friction = 0.;
    w.add_particle([0., 0.1, 0.], 1.).unwrap();
    w.particles[0].previous.y = 0.3;
    w.step(0.1).unwrap();
    assert_eq!(w.particles[0].position.y, 0.);
    assert!(w.particles[0].previous.y < 0.);
    w.settings.bounds = Some(Box {
        min: Vec3::new(-1., 0., -1.),
        max: Vec3::new(1., 1., 1.),
    });
    w.particles[0].position.x = 2.;
    w.step(0.01).unwrap();
    assert!(w.particles[0].position.x <= 1.);
}
#[test]
fn sphere_and_self_collision() {
    let mut w = World::default();
    w.settings.gravity = Vec3::default();
    w.settings.particle_radius = 0.5;
    w.add_particle([0., 2., 0.], 0.).unwrap();
    w.add_particle([0.25, 2., 0.], 1.).unwrap();
    w.step(0.01).unwrap();
    assert!((length(w.particles[1].position - w.particles[0].position) - 1.).abs() < 1e-6);
    w.spheres.push(Sphere {
        center: Vec3::new(0., 0., 0.),
        radius: 1.,
    });
    w.add_particle([0., 0., 0.], 1.).unwrap();
    w.step(0.01).unwrap();
    assert!(length(w.particles[2].position) >= 0.999);
}
#[test]
fn connected_particles_do_not_self_collide() {
    let mut w = World::default();
    w.settings.gravity = Vec3::default();
    w.settings.particle_radius = 1.;
    w.add_particle([0., 0., 0.], 1.).unwrap();
    w.add_particle([0.5, 0., 0.], 1.).unwrap();
    w.add_constraint(0, 1, 1.).unwrap();
    w.step(0.1).unwrap();
    assert_eq!(w.particles[1].position.x, 0.5);
}
#[test]
fn cloth_topology_wind_and_tearing() {
    let mut w = World::default();
    let g = add_cloth(
        &mut w,
        Vec3::new(0., 2., 0.),
        Vec3::new(1., 0., 0.),
        Vec3::new(0., 0., 1.),
        3,
        3,
        ClothOptions {
            shear: false,
            ..ClothOptions::default()
        },
    )
    .unwrap();
    assert_eq!(w.particles.len(), 9);
    assert_eq!(w.constraints.len(), 12);
    let t = g.triangles(&w).unwrap();
    assert_eq!(t.len(), 8);
    add_wind(&mut w, &t, Vec3::new(0., 2., 0.)).unwrap();
    for p in &w.particles {
        assert!((p.force.y - 2.).abs() < 1e-6);
    }
    w.settings.iterations = 0;
    w.settings.gravity = Vec3::default();
    w.settings.tear_ratio = 1.5;
    w.particles[0].position.x = -10.;
    assert!(w.step(0.001).unwrap() > 0);
    assert!(g.triangles(&w).unwrap().len() < 8);
}
#[test]
fn soft_box_and_invalid_inputs() {
    let mut w = World::default();
    assert_eq!(
        add_box(&mut w, Vec3::default(), Vec3::new(1., 1., 1.), 0.8).unwrap(),
        0
    );
    assert_eq!(w.constraints.len(), 28);
    let count = w.particles.len();
    assert!(add_rope(&mut w, Vec3::default(), Vec3::default(), 2, f32::NAN).is_err());
    assert_eq!(w.particles.len(), count);
    assert_eq!(w.step(f32::NAN), Err(Error::InvalidTimeStep));
    w.constraints[0].a = 999;
    assert_eq!(w.step(0.1), Err(Error::InvalidConstraint));
}
