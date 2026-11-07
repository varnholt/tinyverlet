# tinyverlet

**[Interactive Rust demo](https://varnholt.github.io/tinyverlet/rust/)** — [run it natively or build for the browser](https://github.com/varnholt/tinyverlet/blob/rust-v0.1.1/rust/demo/README.md).

Native Rust Verlet particles, distance constraints, cloth, ropes, soft boxes and collisions. No dependencies, no C++ toolchain, and no unsafe code. MIT licensed. Requires Rust 1.85 or newer.

```rust
use tinyverlet::{add_rope, Vec3, World};

let mut world = World::default();
let rope = add_rope(
    &mut world, Vec3::new(0., 2., 0.), Vec3::new(1., 2., 0.), 10, 1.
)?;
world.particles[rope[0] as usize].inverse_mass = 0.;
for _ in 0..240 {
    world.step(1. / 120.)?;
}
# Ok::<(), tinyverlet::Error>(())
```

## Use

```toml
[dependencies]
tinyverlet = "0.1"
```

The crate lives alongside the C++ implementation in `rust/`. To use a local checkout, replace the version with `{ path = "/path/to/tinyverlet/rust" }`.

[crates.io](https://crates.io/crates/tinyverlet) | [API documentation](https://docs.rs/tinyverlet)

## API

| Operation | Behavior |
|---|---|
| `World::add_particle(position, inverse_mass)` | Add a particle at rest; zero inverse mass pins it |
| `World::add_constraint(a, b, stiffness)` | Connect particles at their current distance |
| `World::add_force(particle, force)` | Accumulate force for the next step |
| `World::step(dt)` | Integrate, solve constraints and collisions, tear stretched constraints; return the torn count |
| `add_cloth`, `add_rope`, `add_box` | Construct particle shapes |
| `Grid::triangles(&world)` | Render intact cloth cells, leaving holes at torn sides |
| `add_wind(&mut world, triangles, wind)` | Apply area-weighted surface acceleration |

Particle positions, previous positions, forces, constraints and spheres are public. `step` validates settings and edited arrays before indexing them. `Vec3` converts to/from `[f32; 3]`; particle and force methods also accept convertible vector types.

Settings include gravity, damping, iteration count, restitution, friction, tearing, a ground plane, box bounds, and particle radius. A positive radius enables spatial-hash self-collision; short connected neighbors are excluded. The solver uses stable pair ordering. Rust and C++ results are compared with floating-point tolerances, rather than promising identical bits across platforms.

Use fixed small steps, such as 1/120 second. This is particle collision, not triangle collision or continuous collision detection; fast particles can tunnel. A zero step does nothing. Negative/nonfinite time steps and invalid settings return errors. Numerical overflow can return an error after the state has advanced; reduce the step or simulation scale before continuing. The separate [interactive demo](https://github.com/varnholt/tinyverlet/blob/rust-v0.1.1/rust/demo/README.md) includes a renderer. The library has no engine integration.

## Check and run

Run from `rust/`:

```sh
cargo test
cargo run --example rope
cargo clippy --all-targets -- -D warnings
cargo package --allow-dirty
```

Tests cover force/mass behavior, pins, rope length, sphere/ground/box contacts, self-collision, wind, topology and tearing. A C++ reference cloth simulation checks positions after 120 steps. Regenerate the samples with `python tools/generate_reference.py` and a C++23 compiler (`CXX` selects it). `--check` verifies stored samples on the same toolchain. Normal Cargo builds do not need C++.

CI checks native platforms, Rust 1.85, WebAssembly, packaging and freshly generated C++ reference samples.
