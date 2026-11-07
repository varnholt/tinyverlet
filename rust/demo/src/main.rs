mod platform;
mod scenes;
mod viewer;
use eframe::egui;
use glam::Vec3;
use tinyverlet::{add_wind, Triangle};

struct Grab {
    index: usize,
    inverse_mass: f32,
    point: Vec3,
}
struct App {
    scene: scenes::Scene,
    preset: usize,
    viewer: viewer::Viewer,
    playing: bool,
    wind: f32,
    speed: f32,
    accumulator: f32,
    show_constraints: bool,
    show_particles: bool,
    self_collision: bool,
    tear: bool,
    grab: Option<Grab>,
    error: String,
    step_ms: f64,
    time: f32,
}
impl App {
    fn new(
        cc: &eframe::CreationContext<'_>,
    ) -> Result<Self, Box<dyn std::error::Error + Send + Sync>> {
        platform::theme(&cc.egui_ctx);
        let preset = platform::option("scene")
            .and_then(|s| s.parse::<usize>().ok())
            .unwrap_or(0)
            .min(5);
        let scene = scenes::make(preset);
        let mut viewer = viewer::Viewer::new(cc)?;
        viewer.camera.target = Vec3::from_array(scene.target);
        viewer.camera.distance = scene.distance;
        let mut app = Self {
            wind: scene.wind,
            show_constraints: scene.constraints,
            show_particles: scene.particles,
            tear: scene.world.settings.tear_ratio > 1.,
            scene,
            preset,
            viewer,
            playing: platform::option("paused").as_deref() != Some("1"),
            speed: 1.,
            accumulator: 0.,
            self_collision: true,
            grab: None,
            error: String::new(),
            step_ms: 0.,
            time: 0.,
        };
        let warmup = platform::option("warmup")
            .and_then(|s| s.parse::<usize>().ok())
            .unwrap_or(0)
            .min(600);
        for _ in 0..warmup {
            app.step();
        }
        app.redraw();
        Ok(app)
    }
    fn reset(&mut self) {
        self.scene = scenes::make(self.preset);
        self.wind = self.scene.wind;
        self.show_constraints = self.scene.constraints;
        self.show_particles = self.scene.particles;
        self.tear = self.scene.world.settings.tear_ratio > 1.;
        self.self_collision = true;
        self.viewer.camera.target = Vec3::from_array(self.scene.target);
        self.viewer.camera.distance = self.scene.distance;
        self.accumulator = 0.;
        self.error.clear();
        self.grab = None;
        self.time = 0.;
    }
    fn triangles(&self) -> Vec<Triangle> {
        self.scene
            .cloths
            .iter()
            .flat_map(|g| g.triangles(&self.scene.world).unwrap_or_default())
            .collect()
    }
    fn step(&mut self) {
        let start = platform::now();
        let triangles = self.triangles();
        let wind = tinyverlet::Vec3::new(self.wind * (self.time * 0.7).sin() * 0.25, 0., self.wind);
        if let Err(e) = add_wind(&mut self.scene.world, &triangles, wind)
            .and_then(|()| self.scene.world.step(1. / 120.).map(|_| ()))
        {
            self.error = e.to_string();
            self.playing = false;
        }
        self.time += 1. / 120.;
        self.step_ms = platform::now() - start;
    }
    fn redraw(&mut self) {
        let mut g = viewer::Geometry::default();
        g.grid(10, -0.005, 0.5);
        for sphere in &self.scene.world.spheres {
            g.sphere(
                Vec3::from_array(sphere.center.into()),
                sphere.radius * 0.985,
                [0.41, 0.49, 0.59],
            );
        }
        let colors = [[0.16, 0.73, 0.68], [0.96, 0.59, 0.32], [0.42, 0.56, 0.86]];
        let world = &self.scene.world;
        let p: Vec<_> = world
            .particles
            .iter()
            .map(|p| Vec3::from_array(p.position.into()))
            .collect();
        for (k, cloth) in self.scene.cloths.iter().enumerate() {
            let triangles = cloth.triangles(world).unwrap_or_default();
            let mut normals = vec![Vec3::ZERO; p.len()];
            for &[a, b, c] in &triangles {
                let [a, b, c] = [a, b, c].map(|i| i as usize);
                let n = (p[b] - p[a]).cross(p[c] - p[a]);
                for i in [a, b, c] {
                    normals[i] += n;
                }
            }
            for [a, b, c] in triangles {
                let ids = [a, b, c].map(|i| i as usize);
                g.smooth_triangle(
                    ids.map(|i| p[i]),
                    ids.map(|i| normals[i].normalize_or_zero()),
                    colors[k % 3],
                );
            }
        }
        for (k, &first) in self.scene.boxes.iter().enumerate() {
            for t in scenes::box_triangles(first) {
                g.triangle(t.map(|i| p[i as usize]), colors[k % 3]);
            }
        }
        if self.show_constraints {
            for c in &world.constraints {
                g.line(p[c.a as usize], p[c.b as usize], [0.74, 0.77, 0.58]);
            }
        }
        for (i, particle) in world.particles.iter().enumerate() {
            if self.show_particles || particle.pinned() {
                g.point(
                    p[i],
                    if particle.pinned() {
                        [1., 0.55, 0.22]
                    } else {
                        [0.85, 0.89, 0.95]
                    },
                );
            }
        }
        self.viewer.set_geometry(g);
    }
    fn pick(&self, pointer: egui::Pos2, rect: egui::Rect) -> Option<usize> {
        self.scene
            .world
            .particles
            .iter()
            .enumerate()
            .filter_map(|(i, p)| {
                self.viewer
                    .camera
                    .project(Vec3::from_array(p.position.into()), rect)
                    .map(|screen| (i, screen.distance_sq(pointer)))
            })
            .filter(|&(_, d)| d < 22. * 22.)
            .min_by(|a, b| a.1.total_cmp(&b.1))
            .map(|(i, _)| i)
    }
}
impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        let mut reset = false;
        let mut single = false;
        platform::controls(ui, |ui| {
            platform::heading(ui, "tinyverlet", "Cloth, ropes and soft bodies");
            let before = self.preset;
            egui::ComboBox::from_id_salt("preset")
                .selected_text(scenes::NAMES[self.preset])
                .show_ui(ui, |ui| {
                    for (i, name) in scenes::NAMES.iter().enumerate() {
                        ui.selectable_value(&mut self.preset, i, *name);
                    }
                });
            reset |= self.preset != before;
            ui.horizontal(|ui| {
                if ui
                    .button(if self.playing { "Pause" } else { "Play" })
                    .clicked()
                {
                    self.playing = !self.playing;
                }
                single = ui.button("Step").clicked();
                reset |= ui.button("Reset").clicked();
            });
            ui.add(egui::Slider::new(&mut self.speed, 0.1..=2.).text("Speed"));
            ui.add(egui::Slider::new(&mut self.wind, 0.0..=12.).text("Wind"));
            ui.separator();
            ui.label("SOLVER");
            ui.add(
                egui::Slider::new(&mut self.scene.world.settings.iterations, 1..=24)
                    .text("Iterations"),
            );
            ui.add(
                egui::Slider::new(&mut self.scene.world.settings.friction, 0.0..=1.)
                    .text("Friction"),
            );
            ui.checkbox(&mut self.self_collision, "Particle self-collision");
            ui.checkbox(&mut self.tear, "Enable tearing");
            self.scene.world.settings.particle_radius = if self.self_collision {
                self.scene.radius
            } else {
                0.
            };
            self.scene.world.settings.tear_ratio = if self.tear { 1.35 } else { 0. };
            ui.separator();
            ui.checkbox(&mut self.show_constraints, "Constraints");
            ui.checkbox(&mut self.show_particles, "Particles");
            ui.checkbox(&mut self.viewer.orbit, "Auto orbit");
            ui.label(format!(
                "{} particles  |  {} constraints",
                self.scene.world.particles.len(),
                self.scene.world.constraints.len()
            ));
            ui.small(format!(
                "Time {:.2} s  |  step {:.2} ms",
                self.time, self.step_ms
            ));
            ui.separator();
            ui.small("Shift + drag: grab a particle\nCtrl + click: cut connected threads\nDrag: orbit  |  Right drag: pan\nScroll: zoom");
            if !self.error.is_empty() {
                ui.colored_label(egui::Color32::LIGHT_RED, &self.error);
            }
            ui.hyperlink_to(
                "Library & source",
                "https://github.com/varnholt/tinyverlet/tree/master/rust",
            );
        });
        if reset {
            self.reset();
        }
        if single {
            self.step();
            self.playing = false;
        }
        if self.playing {
            self.accumulator = (self.accumulator
                + ctx.input(|i| i.stable_dt.min(0.05)) * self.speed)
                .min(8. / 120.);
            let start = platform::now();
            while self.accumulator >= 1. / 120. && platform::now() - start < 18. {
                self.step();
                self.accumulator -= 1. / 120.;
            }
            ctx.request_repaint();
        }
        self.redraw();
        egui::CentralPanel::default().show(ui, |ui| {
            let response = self.viewer.show(ui);
            let modifiers = ctx.input(|i| i.modifiers);
            let pointer = ctx.input(|i| i.pointer.interact_pos());
            if response.drag_started_by(egui::PointerButton::Primary) && modifiers.shift {
                if let Some(index) = pointer.and_then(|pos| self.pick(pos, response.rect)) {
                    let p = &mut self.scene.world.particles[index];
                    self.grab = Some(Grab {
                        index,
                        inverse_mass: p.inverse_mass,
                        point: Vec3::from_array(p.position.into()),
                    });
                    p.inverse_mass = 0.;
                }
            }
            if let Some(grab) = &self.grab {
                if let Some(pointer) = pointer {
                    let p = &mut self.scene.world.particles[grab.index];
                    p.position = self
                        .viewer
                        .camera
                        .drag_point(pointer, response.rect, grab.point)
                        .to_array()
                        .into();
                    p.previous = p.position;
                    ctx.request_repaint();
                }
            }
            if !ctx.input(|i| i.pointer.primary_down()) {
                if let Some(grab) = self.grab.take() {
                    self.scene.world.particles[grab.index].inverse_mass = grab.inverse_mass;
                }
            }
            if response.clicked() && modifiers.ctrl {
                if let Some(index) = pointer.and_then(|pos| self.pick(pos, response.rect)) {
                    self.scene
                        .world
                        .constraints
                        .retain(|c| c.a as usize != index && c.b as usize != index);
                    ctx.request_repaint();
                }
            }
        });
    }
    fn on_exit(&mut self, gl: Option<&eframe::glow::Context>) {
        if let Some(gl) = gl {
            self.viewer.destroy(gl);
        }
    }
}
#[cfg(not(target_arch = "wasm32"))]
fn main() -> eframe::Result {
    platform::run(
        "tinyverlet  |  Rust demo",
        Box::new(|cc| Ok(Box::new(App::new(cc)?))),
    )
}
#[cfg(target_arch = "wasm32")]
fn main() {
    platform::run("tinyverlet", Box::new(|cc| Ok(Box::new(App::new(cc)?))));
}
