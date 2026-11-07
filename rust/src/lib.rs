//! Dependency-free Verlet particles, distance constraints, cloth and ropes.
//!
//! ```
//! use tinyverlet::{World, Vec3, add_rope};
//! let mut world = World::default();
//! let rope = add_rope(&mut world, Vec3::new(0., 2., 0.), Vec3::new(1., 2., 0.), 10, 1.)?;
//! world.particles[rope[0] as usize].inverse_mass = 0.;
//! for _ in 0..120 { world.step(1. / 120.)?; }
//! # Ok::<(), tinyverlet::Error>(())
//! ```
use std::collections::{HashMap, HashSet};
use std::fmt;
use std::ops::{Add, AddAssign, Mul, Neg, Sub, SubAssign};

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}
impl Vec3 {
    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }
    fn finite(self) -> bool {
        self.x.is_finite() && self.y.is_finite() && self.z.is_finite()
    }
}
impl From<[f32; 3]> for Vec3 {
    fn from([x, y, z]: [f32; 3]) -> Self {
        Self { x, y, z }
    }
}
impl From<Vec3> for [f32; 3] {
    fn from(v: Vec3) -> Self {
        [v.x, v.y, v.z]
    }
}
impl Add for Vec3 {
    type Output = Self;
    fn add(self, b: Self) -> Self {
        Self::new(self.x + b.x, self.y + b.y, self.z + b.z)
    }
}
impl Sub for Vec3 {
    type Output = Self;
    fn sub(self, b: Self) -> Self {
        Self::new(self.x - b.x, self.y - b.y, self.z - b.z)
    }
}
impl Mul<f32> for Vec3 {
    type Output = Self;
    fn mul(self, s: f32) -> Self {
        Self::new(self.x * s, self.y * s, self.z * s)
    }
}
impl Neg for Vec3 {
    type Output = Self;
    fn neg(self) -> Self {
        self * -1.
    }
}
impl AddAssign for Vec3 {
    fn add_assign(&mut self, b: Self) {
        *self = *self + b;
    }
}
impl SubAssign for Vec3 {
    fn sub_assign(&mut self, b: Self) {
        *self = *self - b;
    }
}
pub fn dot(a: Vec3, b: Vec3) -> f32 {
    a.x * b.x + a.y * b.y + a.z * b.z
}
pub fn cross(a: Vec3, b: Vec3) -> Vec3 {
    Vec3::new(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    )
}
pub fn length(v: Vec3) -> f32 {
    dot(v, v).sqrt()
}
pub type Triangle = [u32; 3];
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Particle {
    pub position: Vec3,
    pub previous: Vec3,
    pub force: Vec3,
    pub inverse_mass: f32,
}
impl Particle {
    pub fn pinned(&self) -> bool {
        self.inverse_mass == 0.
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Constraint {
    pub a: u32,
    pub b: u32,
    pub rest_length: f32,
    pub stiffness: f32,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Sphere {
    pub center: Vec3,
    pub radius: f32,
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Box {
    pub min: Vec3,
    pub max: Vec3,
}
#[derive(Clone, Debug, PartialEq)]
pub struct Settings {
    pub gravity: Vec3,
    pub damping: f32,
    pub iterations: u32,
    pub restitution: f32,
    pub friction: f32,
    pub tear_ratio: f32,
    pub ground: Option<f32>,
    pub bounds: Option<Box>,
    pub particle_radius: f32,
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            gravity: Vec3::new(0., -9.81, 0.),
            damping: 0.995,
            iterations: 8,
            restitution: 0.3,
            friction: 0.3,
            tear_ratio: 0.,
            ground: None,
            bounds: None,
            particle_radius: 0.,
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidParticle,
    InvalidConstraint,
    InvalidSettings,
    InvalidTimeStep,
    InvalidGeometry,
    CapacityExceeded,
}
impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(match self {
            Self::InvalidParticle => "missing particle or invalid particle state",
            Self::InvalidConstraint => "invalid distance constraint",
            Self::InvalidSettings => "invalid simulation settings",
            Self::InvalidTimeStep => "time step must be finite and nonnegative",
            Self::InvalidGeometry => "invalid shape dimensions or coordinates",
            Self::CapacityExceeded => "particle or constraint capacity exceeded",
        })
    }
}
impl std::error::Error for Error {}
fn stiffness_valid(v: f32) -> bool {
    v.is_finite() && v > 0. && v <= 1.
}
fn mass_valid(v: f32) -> bool {
    v.is_finite() && v >= 0.
}
fn edge(a: u32, b: u32) -> (u32, u32) {
    (a.min(b), a.max(b))
}
/// Public particle/constraint arrays can be edited between steps. `step` validates them before indexing.
#[derive(Clone, Debug, Default)]
pub struct World {
    pub settings: Settings,
    pub particles: Vec<Particle>,
    pub constraints: Vec<Constraint>,
    pub spheres: Vec<Sphere>,
}
impl World {
    pub fn add_particle(
        &mut self,
        position: impl Into<Vec3>,
        inverse_mass: f32,
    ) -> Result<u32, Error> {
        let position = position.into();
        if !position.finite() || !mass_valid(inverse_mass) {
            return Err(Error::InvalidParticle);
        }
        if self.particles.len() >= u32::MAX as usize {
            return Err(Error::CapacityExceeded);
        }
        let id = self.particles.len() as u32;
        self.particles.push(Particle {
            position,
            previous: position,
            force: Vec3::default(),
            inverse_mass,
        });
        Ok(id)
    }
    pub fn add_constraint(&mut self, a: u32, b: u32, stiffness: f32) -> Result<u32, Error> {
        if a == b || !stiffness_valid(stiffness) {
            return Err(Error::InvalidConstraint);
        }
        let pa = self
            .particles
            .get(a as usize)
            .ok_or(Error::InvalidParticle)?;
        let pb = self
            .particles
            .get(b as usize)
            .ok_or(Error::InvalidParticle)?;
        let rest_length = length(pb.position - pa.position);
        if !rest_length.is_finite() {
            return Err(Error::InvalidConstraint);
        }
        if self.constraints.len() >= u32::MAX as usize {
            return Err(Error::CapacityExceeded);
        }
        let id = self.constraints.len() as u32;
        self.constraints.push(Constraint {
            a,
            b,
            rest_length,
            stiffness,
        });
        Ok(id)
    }
    pub fn add_force(&mut self, particle: u32, force: impl Into<Vec3>) -> Result<(), Error> {
        let force = force.into();
        if !force.finite() {
            return Err(Error::InvalidParticle);
        }
        let p = self
            .particles
            .get_mut(particle as usize)
            .ok_or(Error::InvalidParticle)?;
        if !(p.force + force).finite() {
            return Err(Error::InvalidParticle);
        }
        p.force += force;
        Ok(())
    }
    pub fn validate(&self) -> Result<(), Error> {
        let s = &self.settings;
        if !s.gravity.finite()
            || ![s.damping, s.restitution, s.friction]
                .iter()
                .all(|v| v.is_finite() && (0.0..=1.0).contains(v))
            || !s.tear_ratio.is_finite()
            || s.tear_ratio < 0.
            || !s.particle_radius.is_finite()
            || s.particle_radius < 0.
            || !(s.particle_radius * 3.).is_finite()
            || s.ground.is_some_and(|v| !v.is_finite())
        {
            return Err(Error::InvalidSettings);
        }
        if let Some(b) = s.bounds {
            if !b.min.finite()
                || !b.max.finite()
                || b.min.x > b.max.x
                || b.min.y > b.max.y
                || b.min.z > b.max.z
            {
                return Err(Error::InvalidSettings);
            }
        }
        for p in &self.particles {
            if !p.position.finite()
                || !p.previous.finite()
                || !p.force.finite()
                || !mass_valid(p.inverse_mass)
            {
                return Err(Error::InvalidParticle);
            }
        }
        for c in &self.constraints {
            if c.a == c.b
                || c.a as usize >= self.particles.len()
                || c.b as usize >= self.particles.len()
                || !stiffness_valid(c.stiffness)
                || !c.rest_length.is_finite()
                || c.rest_length < 0.
            {
                return Err(Error::InvalidConstraint);
            }
        }
        for s in &self.spheres {
            if !s.center.finite() || !s.radius.is_finite() || s.radius < 0. {
                return Err(Error::InvalidGeometry);
            }
        }
        Ok(())
    }
    /// Advance by seconds and return the number of torn constraints. Use a fixed small step.
    /// Self-collision is particle against particle and does not prevent all tunneling.
    pub fn step(&mut self, dt: f32) -> Result<usize, Error> {
        if !dt.is_finite() || dt < 0. || !(dt * dt).is_finite() {
            return Err(Error::InvalidTimeStep);
        }
        if dt == 0. {
            return Ok(0);
        }
        self.validate()?;
        for p in &mut self.particles {
            if !p.pinned() {
                let velocity = (p.position - p.previous) * self.settings.damping;
                p.previous = p.position;
                p.position +=
                    velocity + (self.settings.gravity + p.force * p.inverse_mass) * (dt * dt);
            }
            p.force = Vec3::default();
        }
        let pairs = if self.settings.particle_radius > 0. {
            self.collision_candidates()
        } else {
            Vec::new()
        };
        for i in 0..self.settings.iterations {
            self.solve_constraints();
            self.collide_particles(&pairs, i + 1 == self.settings.iterations);
            self.collide();
        }
        let before = self.constraints.len();
        let ratio = self.settings.tear_ratio;
        if ratio > 1. {
            let particles = &self.particles;
            self.constraints.retain(|c| {
                length(particles[c.b as usize].position - particles[c.a as usize].position)
                    <= c.rest_length * ratio
            });
        }
        if self
            .particles
            .iter()
            .any(|p| !p.position.finite() || !p.previous.finite())
        {
            return Err(Error::InvalidParticle);
        }
        Ok(before - self.constraints.len())
    }
    fn solve_constraints(&mut self) {
        for c in &self.constraints {
            let (pa, pb) = two_mut(&mut self.particles, c.a as usize, c.b as usize);
            let total = pa.inverse_mass + pb.inverse_mass;
            if total == 0. {
                continue;
            }
            let delta = pb.position - pa.position;
            let distance = length(delta);
            if distance < 1e-9 {
                continue;
            }
            let correction =
                delta * ((distance - c.rest_length) / (distance * total) * c.stiffness);
            pa.position += correction * pa.inverse_mass;
            pb.position -= correction * pb.inverse_mass;
        }
    }
    fn collide(&mut self) {
        let s = &self.settings;
        for p in &mut self.particles {
            if p.pinned() {
                continue;
            }
            for sphere in &self.spheres {
                let offset = p.position - sphere.center;
                let distance = length(offset);
                if distance < sphere.radius {
                    let n = if distance > 1e-9 {
                        offset * (1. / distance)
                    } else {
                        Vec3::new(0., 1., 0.)
                    };
                    resolve_contact(p, sphere.center + n * sphere.radius, n, s);
                }
            }
            if let Some(ground) = s.ground {
                if p.position.y < ground {
                    resolve_contact(
                        p,
                        Vec3::new(p.position.x, ground, p.position.z),
                        Vec3::new(0., 1., 0.),
                        s,
                    );
                }
            }
            if let Some(b) = s.bounds {
                for axis in 0..3 {
                    let pos: [f32; 3] = p.position.into();
                    let min: [f32; 3] = b.min.into();
                    let max: [f32; 3] = b.max.into();
                    let mut corrected = pos;
                    let mut normal = [0.; 3];
                    if pos[axis] < min[axis] {
                        corrected[axis] = min[axis];
                        normal[axis] = 1.;
                    } else if pos[axis] > max[axis] {
                        corrected[axis] = max[axis];
                        normal[axis] = -1.;
                    } else {
                        continue;
                    }
                    resolve_contact(p, corrected.into(), normal.into(), s);
                }
            }
        }
    }
    fn collision_candidates(&self) -> Vec<(usize, usize)> {
        let diameter = 2. * self.settings.particle_radius;
        let reach = 1.5 * diameter;
        let inverse = 1. / reach;
        let connected: HashSet<_> = self
            .constraints
            .iter()
            .filter(|c| c.rest_length < diameter)
            .map(|c| edge(c.a, c.b))
            .collect();
        let cell_of = |p: Vec3| [p.x, p.y, p.z].map(|v| (v * inverse).floor() as i64);
        let mut grid: HashMap<[i64; 3], Vec<usize>> = HashMap::new();
        for (i, p) in self.particles.iter().enumerate() {
            grid.entry(cell_of(p.position)).or_default().push(i);
        }
        let mut pairs = Vec::new();
        for (i, pi) in self.particles.iter().enumerate() {
            let c = cell_of(pi.position);
            for dx in -1i64..=1 {
                for dy in -1i64..=1 {
                    for dz in -1i64..=1 {
                        if let Some(ids) = grid.get(&[
                            c[0].saturating_add(dx),
                            c[1].saturating_add(dy),
                            c[2].saturating_add(dz),
                        ]) {
                            for &j in ids {
                                if j <= i
                                    || pi.inverse_mass + self.particles[j].inverse_mass == 0.
                                    || connected.contains(&edge(i as u32, j as u32))
                                {
                                    continue;
                                }
                                let delta = self.particles[j].position - pi.position;
                                if dot(delta, delta) < reach * reach {
                                    pairs.push((i, j));
                                }
                            }
                        }
                    }
                }
            }
        }
        pairs.sort_unstable();
        pairs.dedup();
        pairs
    }
    fn collide_particles(&mut self, pairs: &[(usize, usize)], friction: bool) {
        let diameter = 2. * self.settings.particle_radius;
        for &(i, j) in pairs {
            let (pi, pj) = two_mut(&mut self.particles, i, j);
            let delta = pj.position - pi.position;
            let d2 = dot(delta, delta);
            if d2 >= diameter * diameter || d2 < 1e-18 {
                continue;
            }
            let total = pi.inverse_mass + pj.inverse_mass;
            let distance = d2.sqrt();
            let correction = delta * ((diameter - distance) / (distance * total));
            pi.position -= correction * pi.inverse_mass;
            pj.position += correction * pj.inverse_mass;
            if friction && self.settings.friction > 0. {
                let n = delta * (1. / distance);
                let relative = (pj.position - pj.previous) - (pi.position - pi.previous);
                let sliding = relative - n * dot(relative, n);
                let change = sliding * (self.settings.friction / total);
                pi.previous -= change * pi.inverse_mass;
                pj.previous += change * pj.inverse_mass;
            }
        }
    }
}
fn two_mut<T>(v: &mut [T], a: usize, b: usize) -> (&mut T, &mut T) {
    if a < b {
        let (left, right) = v.split_at_mut(b);
        (&mut left[a], &mut right[0])
    } else {
        let (left, right) = v.split_at_mut(a);
        (&mut right[0], &mut left[b])
    }
}
fn resolve_contact(p: &mut Particle, corrected: Vec3, n: Vec3, s: &Settings) {
    let velocity = p.position - p.previous;
    let into = dot(velocity, n);
    p.position = corrected;
    if into < 0. {
        let normal = n * into;
        let tangent = velocity - normal;
        p.previous = p.position - (tangent * (1. - s.friction) - normal * s.restitution);
    }
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Grid {
    pub first: u32,
    pub columns: u32,
    pub rows: u32,
}
impl Grid {
    pub fn at(&self, column: u32, row: u32) -> Option<u32> {
        if column >= self.columns || row >= self.rows {
            return None;
        }
        self.first
            .checked_add(row.checked_mul(self.columns)?)?
            .checked_add(column)
    }
    /// Two triangles per intact cloth cell. A torn side removes the entire cell.
    pub fn triangles(&self, world: &World) -> Result<Vec<Triangle>, Error> {
        let last = self
            .rows
            .checked_sub(1)
            .and_then(|r| self.columns.checked_sub(1).and_then(|c| self.at(c, r)))
            .ok_or(Error::InvalidGeometry)?;
        if last as usize >= world.particles.len() {
            return Err(Error::InvalidParticle);
        }
        let connected: HashSet<_> = world.constraints.iter().map(|c| edge(c.a, c.b)).collect();
        let mut result = Vec::new();
        for r in 0..self.rows - 1 {
            for c in 0..self.columns - 1 {
                let a = self.at(c, r).unwrap();
                let b = self.at(c + 1, r).unwrap();
                let d = self.at(c, r + 1).unwrap();
                let e = self.at(c + 1, r + 1).unwrap();
                if [(a, b), (b, e), (e, d), (d, a)]
                    .iter()
                    .all(|&(a, b)| connected.contains(&edge(a, b)))
                {
                    result.extend([[a, d, e], [a, e, b]]);
                }
            }
        }
        Ok(result)
    }
}
#[derive(Clone, Copy, Debug)]
pub struct ClothOptions {
    pub stiffness: f32,
    pub shear: bool,
    pub bend: bool,
    pub inverse_mass: f32,
}
impl Default for ClothOptions {
    fn default() -> Self {
        Self {
            stiffness: 1.,
            shear: true,
            bend: false,
            inverse_mass: 1.,
        }
    }
}
fn reserve_particles(world: &mut World, count: usize) -> Result<(), Error> {
    if world
        .particles
        .len()
        .checked_add(count)
        .is_none_or(|n| n > u32::MAX as usize)
    {
        return Err(Error::CapacityExceeded);
    }
    world
        .particles
        .try_reserve(count)
        .map_err(|_| Error::CapacityExceeded)
}
/// Add a cloth grid. Its corners are `origin + c*u + r*v`; pin particles by setting inverse mass to zero.
#[allow(clippy::too_many_arguments)]
pub fn add_cloth(
    world: &mut World,
    origin: Vec3,
    u: Vec3,
    v: Vec3,
    columns: u32,
    rows: u32,
    options: ClothOptions,
) -> Result<Grid, Error> {
    if columns < 2
        || rows < 2
        || !origin.finite()
        || !u.finite()
        || !v.finite()
        || !(origin + u * (columns - 1) as f32 + v * (rows - 1) as f32).finite()
    {
        return Err(Error::InvalidGeometry);
    }
    if !stiffness_valid(options.stiffness) || !mass_valid(options.inverse_mass) {
        return Err(Error::InvalidConstraint);
    }
    let count = (columns as usize)
        .checked_mul(rows as usize)
        .ok_or(Error::CapacityExceeded)?;
    reserve_particles(world, count)?;
    let grid = Grid {
        first: world.particles.len() as u32,
        columns,
        rows,
    };
    for r in 0..rows {
        for c in 0..columns {
            world.add_particle(origin + u * c as f32 + v * r as f32, options.inverse_mass)?;
        }
    }
    for r in 0..rows {
        for c in 0..columns {
            let p = grid.at(c, r).unwrap();
            if c + 1 < columns {
                world.add_constraint(p, grid.at(c + 1, r).unwrap(), options.stiffness)?;
            }
            if r + 1 < rows {
                world.add_constraint(p, grid.at(c, r + 1).unwrap(), options.stiffness)?;
            }
            if options.shear && c + 1 < columns && r + 1 < rows {
                world.add_constraint(p, grid.at(c + 1, r + 1).unwrap(), options.stiffness)?;
                world.add_constraint(
                    grid.at(c + 1, r).unwrap(),
                    grid.at(c, r + 1).unwrap(),
                    options.stiffness,
                )?;
            }
            if options.bend && c + 2 < columns {
                world.add_constraint(p, grid.at(c + 2, r).unwrap(), options.stiffness)?;
            }
            if options.bend && r + 2 < rows {
                world.add_constraint(p, grid.at(c, r + 2).unwrap(), options.stiffness)?;
            }
        }
    }
    Ok(grid)
}
pub fn add_rope(
    world: &mut World,
    from: Vec3,
    to: Vec3,
    segments: u32,
    stiffness: f32,
) -> Result<Vec<u32>, Error> {
    if segments == 0 || !from.finite() || !to.finite() || !(to - from).finite() {
        return Err(Error::InvalidGeometry);
    }
    if !stiffness_valid(stiffness) {
        return Err(Error::InvalidConstraint);
    }
    let count = (segments as usize)
        .checked_add(1)
        .ok_or(Error::CapacityExceeded)?;
    reserve_particles(world, count)?;
    let mut indices = Vec::with_capacity(count);
    for i in 0..=segments {
        let id = world.add_particle(from + (to - from) * (i as f32 / segments as f32), 1.)?;
        if let Some(&previous) = indices.last() {
            world.add_constraint(previous, id, stiffness)?;
        }
        indices.push(id);
    }
    Ok(indices)
}
/// Eight corners connected pairwise (28 constraints). Returns the first particle index.
pub fn add_box(world: &mut World, center: Vec3, size: Vec3, stiffness: f32) -> Result<u32, Error> {
    if !center.finite()
        || !size.finite()
        || size.x < 0.
        || size.y < 0.
        || size.z < 0.
        || !(center + size * 0.5).finite()
        || !(center - size * 0.5).finite()
    {
        return Err(Error::InvalidGeometry);
    }
    if !stiffness_valid(stiffness) {
        return Err(Error::InvalidConstraint);
    }
    reserve_particles(world, 8)?;
    let first = world.particles.len() as u32;
    for i in 0..8 {
        world.add_particle(
            center
                + Vec3::new(
                    if i & 1 != 0 { 0.5 } else { -0.5 } * size.x,
                    if i & 2 != 0 { 0.5 } else { -0.5 } * size.y,
                    if i & 4 != 0 { 0.5 } else { -0.5 } * size.z,
                ),
            1.,
        )?;
    }
    for a in 0..8 {
        for b in a + 1..8 {
            world.add_constraint(first + a, first + b, stiffness)?;
        }
    }
    Ok(first)
}
/// Area-weighted wind acceleration, independent of cloth resolution and particle mass.
pub fn add_wind(world: &mut World, triangles: &[Triangle], wind: Vec3) -> Result<(), Error> {
    if !wind.finite() {
        return Err(Error::InvalidGeometry);
    }
    if triangles
        .iter()
        .flatten()
        .any(|&i| i as usize >= world.particles.len())
    {
        return Err(Error::InvalidParticle);
    }
    let mut push = vec![Vec3::default(); world.particles.len()];
    let mut area_sum = vec![0.; world.particles.len()];
    for &[a, b, c] in triangles {
        let p = world.particles[a as usize].position;
        let area_normal = cross(
            world.particles[b as usize].position - p,
            world.particles[c as usize].position - p,
        ) * 0.5;
        let area = length(area_normal);
        if area <= 0. {
            continue;
        }
        let n = area_normal * (1. / area);
        let weighted = n * (dot(n, wind) * area);
        for i in [a, b, c] {
            push[i as usize] += weighted;
            area_sum[i as usize] += area;
        }
    }
    for (i, p) in world.particles.iter_mut().enumerate() {
        if area_sum[i] > 0. && !p.pinned() {
            p.force += push[i] * (1. / (area_sum[i] * p.inverse_mass));
        }
    }
    Ok(())
}
