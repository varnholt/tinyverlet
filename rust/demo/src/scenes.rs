use tinyverlet::{add_box, add_cloth, add_rope, Box, ClothOptions, Grid, Sphere, Vec3, World};
pub const NAMES: [&str; 6] = [
    "Cloth on sphere",
    "Three sheets",
    "Curtain in the wind",
    "Tearable cloth",
    "Ropes",
    "Soft boxes",
];
pub struct Scene {
    pub world: World,
    pub cloths: Vec<Grid>,
    pub boxes: Vec<u32>,
    pub target: [f32; 3],
    pub distance: f32,
    pub wind: f32,
    pub constraints: bool,
    pub particles: bool,
    pub radius: f32,
}
pub fn make(index: usize) -> Scene {
    let mut scene = Scene {
        world: World::default(),
        cloths: Vec::new(),
        boxes: Vec::new(),
        target: [0., 1., 0.],
        distance: 4.7,
        wind: 0.,
        constraints: false,
        particles: false,
        radius: 0.03,
    };
    scene.world.settings.ground = Some(0.);
    match index {
        1 => {
            scene.world.spheres.push(Sphere {
                center: Vec3::new(0., 0.7, 0.),
                radius: 0.55,
            });
            let n = 26;
            let spacing = 0.07;
            scene.radius = spacing * 0.45;
            for k in 0..3 {
                let angle = k as f32 * 0.5;
                let u = Vec3::new(angle.cos() * spacing, 0., angle.sin() * spacing);
                let v = Vec3::new(-angle.sin() * spacing, 0., angle.cos() * spacing);
                let origin =
                    Vec3::new(0., 1.6 + 0.5 * k as f32, 0.) - (u + v) * ((n - 1) as f32 * 0.5);
                scene.cloths.push(
                    add_cloth(
                        &mut scene.world,
                        origin,
                        u,
                        v,
                        n,
                        n,
                        ClothOptions::default(),
                    )
                    .unwrap(),
                );
            }
        }
        2 | 3 => {
            let tearable = index == 3;
            let columns = 36;
            let rows = 26;
            let spacing = 0.07;
            scene.radius = spacing * 0.45;
            let grid = add_cloth(
                &mut scene.world,
                Vec3::new(-spacing * (columns - 1) as f32 * 0.5, 2.4, 0.),
                Vec3::new(spacing, 0., 0.),
                Vec3::new(0., -spacing, 0.),
                columns,
                rows,
                ClothOptions {
                    bend: !tearable,
                    ..Default::default()
                },
            )
            .unwrap();
            for c in (0..columns).step_by(if tearable { 1 } else { 2 }) {
                let p = &mut scene.world.particles[grid.at(c, 0).unwrap() as usize];
                p.position.x *= if tearable { 1. } else { 0.65 };
                p.previous = p.position;
                p.inverse_mass = 0.;
            }
            scene.cloths.push(grid);
            scene.target = [0., 1.4, 0.];
            scene.wind = if tearable { 0. } else { 4. };
            if tearable {
                scene.world.settings.tear_ratio = 1.35;
                scene.world.settings.iterations = 12;
            }
        }
        4 => {
            scene.world.spheres.push(Sphere {
                center: Vec3::new(0., 0.6, 0.4),
                radius: 0.4,
            });
            scene.target = [0., 1.4, 0.4];
            scene.distance = 5.7;
            scene.constraints = true;
            scene.particles = true;
            for i in 0..5 {
                let x = -1.2 + 0.6 * i as f32;
                let length = 1.2 + 0.25 * i as f32;
                let rope = add_rope(
                    &mut scene.world,
                    Vec3::new(x, 2.6, 0.),
                    Vec3::new(x, 2.6, length),
                    16,
                    1.,
                )
                .unwrap();
                scene.world.particles[rope[0] as usize].inverse_mass = 0.;
                if i % 2 == 0 {
                    let end = scene.world.particles[*rope.last().unwrap() as usize].position;
                    let first = add_box(
                        &mut scene.world,
                        end + Vec3::new(0., -0.1, 0.),
                        Vec3::new(0.16, 0.16, 0.16),
                        1.,
                    )
                    .unwrap();
                    scene
                        .world
                        .add_constraint(*rope.last().unwrap(), first + 2, 1.)
                        .unwrap();
                    scene
                        .world
                        .add_constraint(*rope.last().unwrap(), first + 7, 1.)
                        .unwrap();
                    scene.boxes.push(first);
                }
            }
        }
        5 => {
            scene.world.settings.bounds = Some(Box {
                min: Vec3::new(-1.5, -1., -1.5),
                max: Vec3::new(1.5, 6., 1.5),
            });
            scene.world.settings.restitution = 0.5;
            scene.world.spheres.push(Sphere {
                center: Vec3::default(),
                radius: 0.7,
            });
            scene.constraints = true;
            scene.target = [0., 1.8, 0.];
            scene.distance = 7.;
            for i in 0..9 {
                let angle = i as f32 * 2.399_963;
                let radius = 0.35 + 0.08 * i as f32;
                let center = Vec3::new(
                    radius * angle.cos(),
                    1.4 + 0.45 * i as f32,
                    radius * angle.sin(),
                );
                let size = 0.3 + 0.05 * (i % 3) as f32;
                let stiffness = [1., 0.25, 0.06][i % 3];
                let first = add_box(
                    &mut scene.world,
                    center,
                    Vec3::new(size, size, size),
                    stiffness,
                )
                .unwrap();
                for corner in 0..8 {
                    if corner & 2 != 0 {
                        scene.world.particles[(first + corner) as usize].previous.x += 0.01;
                    }
                }
                scene.boxes.push(first);
            }
        }
        _ => {
            scene.world.spheres.push(Sphere {
                center: Vec3::new(0., 1., 0.),
                radius: 0.6,
            });
            let n = 32;
            let spacing = 0.065;
            let half = spacing * (n - 1) as f32 * 0.5;
            scene.radius = spacing * 0.45;
            scene.cloths.push(
                add_cloth(
                    &mut scene.world,
                    Vec3::new(-half, 2., -half),
                    Vec3::new(spacing, 0., 0.),
                    Vec3::new(0., 0., spacing),
                    n,
                    n,
                    ClothOptions::default(),
                )
                .unwrap(),
            );
        }
    }
    scene.world.settings.particle_radius = scene.radius;
    scene
}
pub fn box_triangles(first: u32) -> Vec<[u32; 3]> {
    let mut t = Vec::new();
    for [a, b, c, d] in [
        [0, 2, 3, 1],
        [4, 5, 7, 6],
        [0, 4, 6, 2],
        [1, 3, 7, 5],
        [0, 1, 5, 4],
        [2, 6, 7, 3],
    ] {
        t.extend([
            [first + a, first + b, first + c],
            [first + a, first + c, first + d],
        ]);
    }
    t
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_presets_step_and_have_valid_surfaces() {
        for i in 0..6 {
            let mut s = make(i);
            for _ in 0..24 {
                s.world.step(1. / 120.).unwrap();
            }
            for cloth in &s.cloths {
                assert!(!cloth.triangles(&s.world).unwrap().is_empty());
            }
            s.world.validate().unwrap();
            assert!(!s.world.particles.is_empty());
        }
    }
}
