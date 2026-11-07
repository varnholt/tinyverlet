use tinyverlet::{add_rope, Vec3, World};
fn main() -> Result<(), tinyverlet::Error> {
    let mut world = World::default();
    let ids = add_rope(
        &mut world,
        Vec3::new(0., 2., 0.),
        Vec3::new(1., 2., 0.),
        12,
        1.,
    )?;
    world.particles[ids[0] as usize].inverse_mass = 0.;
    for _ in 0..240 {
        world.step(1. / 120.)?;
    }
    println!(
        "rope end: {:?}",
        world.particles[*ids.last().unwrap() as usize].position
    );
    Ok(())
}
