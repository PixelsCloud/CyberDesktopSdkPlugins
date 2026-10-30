// Private C ABI. Salva owns pressure, neighbor search, viscosity and tension.
use salva2d::parry::{
    math::{Pose, Vector as PVector},
    query::{Ray, RayCast},
    shape::{Ball, CompositeShapeRef, Compound, SharedShape},
};
use salva2d::{
    coupling::CouplingManager,
    geometry::{HGrid, HGridEntry},
    math::Vector,
    object::{
        interaction_groups::InteractionGroups, Boundary, BoundaryHandle, BoundarySet, Fluid,
        FluidHandle,
    },
    solver::{Akinci2013SurfaceTension, IISPHSolver, XSPHViscosity},
    LiquidWorld, TimestepManager,
};
use std::{
    collections::BTreeMap,
    panic::{catch_unwind, AssertUnwindSafe},
    sync::RwLock,
};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Vec2 {
    pub x: f32,
    pub y: f32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Sample {
    pub position: Vec2,
    pub velocity: Vec2,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct Reaction {
    pub impulse: Vec2,
    pub moment: f32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Settings {
    pub radius: f32,
    pub density: f32,
    pub viscosity: f32,
    pub tension: f32,
    pub adhesion: f32,
    pub substeps: u32,
}

pub struct Backend {
    world: LiquidWorld,
    fluid: FluidHandle,
    boundaries: BTreeMap<u64, BoundaryHandle>,
    solids: BTreeMap<u64, SolidBoundary>,
    reactions: BTreeMap<u64, Reaction>,
    substeps: u32,
}

struct SolidBoundary {
    shape: Compound,
    positions: Vec<Vector<f32>>,
    velocities: Vec<Vector<f32>>,
}

fn vector(p: Vec2) -> Vector<f32> {
    Vector::new(p.x, p.y)
}
fn point(p: &Vector<f32>) -> Vec2 {
    Vec2 { x: p.x, y: p.y }
}

fn attempt(f: impl FnOnce()) -> i32 {
    if catch_unwind(AssertUnwindSafe(f)).is_ok() {
        0
    } else {
        1
    }
}

struct Coupling<'a> {
    handles: &'a BTreeMap<u64, BoundaryHandle>,
    reactions: &'a mut BTreeMap<u64, Reaction>,
    steps: usize,
}

impl CouplingManager for Coupling<'_> {
    fn update_boundaries(
        &mut self,
        _: &TimestepManager,
        _: f32,
        _: f32,
        _: &HGrid<HGridEntry>,
        _: &mut [Fluid],
        boundaries: &mut BoundarySet,
    ) {
        self.steps += 1;
        assert!(self.steps <= 16, "fluid substep budget exceeded");
        for boundary in boundaries.values_mut() {
            boundary.clear_forces(false);
        }
    }

    fn transmit_forces(
        &mut self,
        timestep: &TimestepManager,
        boundaries: &BoundarySet,
        coefficient: f32,
    ) {
        for (id, handle) in self.handles {
            let boundary = &boundaries[*handle];
            let forces = boundary.forces.as_ref().unwrap().read().unwrap();
            let value = self.reactions.entry(*id).or_default();
            for (force, position) in forces.iter().zip(&boundary.positions) {
                let impulse = force * (timestep.dt() * coefficient);
                value.impulse.x += impulse.x;
                value.impulse.y += impulse.y;
                value.moment += position.x * impulse.y - position.y * impulse.x;
            }
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_create(settings: *const Settings) -> *mut Backend {
    if settings.is_null() {
        return std::ptr::null_mut();
    }
    catch_unwind(AssertUnwindSafe(|| {
        let s = *settings;
        assert!((1..=16).contains(&s.substeps));
        let mut solver: IISPHSolver = IISPHSolver::new();
        solver.max_pressure_iter = 30;
        let mut world = LiquidWorld::new(solver, s.radius, 2.0, 1.0);
        let mut fluid = Fluid::new(Vec::new(), s.radius, s.density, InteractionGroups::all());
        fluid
            .nonpressure_forces
            .push(Box::new(XSPHViscosity::new(s.viscosity, s.viscosity)));
        if s.tension > 0.0 || s.adhesion > 0.0 {
            fluid
                .nonpressure_forces
                .push(Box::new(Akinci2013SurfaceTension::new(
                    s.tension, s.adhesion,
                )));
        }
        let fluid = world.add_fluid(fluid);
        Box::into_raw(Box::new(Backend {
            world,
            fluid,
            boundaries: BTreeMap::new(),
            solids: BTreeMap::new(),
            reactions: BTreeMap::new(),
            substeps: s.substeps,
        }))
    }))
    .unwrap_or(std::ptr::null_mut())
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_destroy(backend: *mut Backend) {
    if !backend.is_null() {
        let _ = catch_unwind(AssertUnwindSafe(|| drop(Box::from_raw(backend))));
    }
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_add(
    backend: *mut Backend,
    samples: *const Sample,
    count: u32,
) -> i32 {
    if backend.is_null() || samples.is_null() || count > 4096 {
        return 2;
    }
    attempt(|| {
        let b = &mut *backend;
        let samples = std::slice::from_raw_parts(samples, count as usize);
        let positions: Vec<_> = samples.iter().map(|s| vector(s.position)).collect();
        let velocities: Vec<_> = samples.iter().map(|s| vector(s.velocity)).collect();
        b.world.fluids_mut()[b.fluid].add_particles(&positions, Some(&velocities));
    })
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_boundary(
    backend: *mut Backend,
    id: u64,
    samples: *const Sample,
    count: u32,
) -> i32 {
    if backend.is_null() || samples.is_null() || count == 0 || count > 8192 {
        return 2;
    }
    attempt(|| {
        let b = &mut *backend;
        let samples = std::slice::from_raw_parts(samples, count as usize);
        let positions: Vec<_> = samples.iter().map(|s| vector(s.position)).collect();
        let velocities: Vec<_> = samples.iter().map(|s| vector(s.velocity)).collect();
        let ball = SharedShape::new(Ball::new(b.world.particle_radius() * 2.0));
        let solid = Compound::new(
            positions
                .iter()
                .map(|p| (Pose::translation(p.x, p.y), ball.clone()))
                .collect(),
        );
        if let Some(handle) = b.boundaries.get(&id) {
            let boundary = &mut b.world.boundaries_mut()[*handle];
            assert_eq!(boundary.positions.len(), positions.len());
            boundary.positions = positions;
            boundary.velocities = velocities;
        } else {
            let mut boundary = Boundary::new(positions, InteractionGroups::all());
            boundary.velocities = velocities;
            boundary.forces = Some(RwLock::new(vec![Vector::zeros(); count as usize]));
            let handle = b.world.add_boundary(boundary);
            b.boundaries.insert(id, handle);
        }
        let boundary = &b.world.boundaries()[b.boundaries[&id]];
        b.solids.insert(
            id,
            SolidBoundary {
                shape: solid,
                positions: boundary.positions.clone(),
                velocities: boundary.velocities.clone(),
            },
        );
    })
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_remove_boundary(backend: *mut Backend, id: u64) -> i32 {
    if backend.is_null() {
        return 2;
    }
    attempt(|| {
        let b = &mut *backend;
        if let Some(handle) = b.boundaries.remove(&id) {
            b.world.remove_boundary(handle);
        }
        b.reactions.remove(&id);
        b.solids.remove(&id);
    })
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_step(backend: *mut Backend, seconds: f32, gravity: Vec2) -> i32 {
    if backend.is_null() || !seconds.is_finite() || seconds <= 0.0 || seconds > 0.04 {
        return 2;
    }
    attempt(|| {
        let b = &mut *backend;
        b.reactions.clear();
        // Salva 0.10 disables its adaptive CFL controller. Use a fixed, bounded
        // number of equal IISPH substeps (default 480 Hz) and swept solid guards.
        // Keeping dt constant also preserves the solver's pressure warm start.
        let gravity = vector(gravity);
        let dt = seconds / b.substeps as f32;
        for _ in 0..b.substeps {
            let previous = b.world.fluids()[b.fluid].positions.clone();
            {
                let mut coupling = Coupling {
                    handles: &b.boundaries,
                    reactions: &mut b.reactions,
                    steps: 0,
                };
                b.world.step_with_coupling(dt, &gravity, &mut coupling);
            }
            // Pressure boundaries alone do not exclude isolated spray particles.
            // Use Parry's BVH over sampled solid discs for a hard contact guard,
            // retaining equal/opposite contact impulses for rigid-body coupling.
            let radius = b.world.particle_radius();
            let fluid = &mut b.world.fluids_mut()[b.fluid];
            for (id, solid) in &b.solids {
                let positions = &solid.positions;
                let velocities = &solid.velocities;
                for n in 0..fluid.positions.len() {
                    let start = PVector::new(previous[n].x, previous[n].y);
                    let delta = PVector::new(
                        fluid.positions[n].x - previous[n].x,
                        fluid.positions[n].y - previous[n].y,
                    );
                    if delta.length_squared() > 1.0e-12 {
                        if let Some(hit) = solid.shape.cast_local_ray_and_get_normal(
                            &Ray::new(start, delta),
                            1.0,
                            true,
                        ) {
                            if hit.time_of_impact > 1.0e-6 {
                                let p = start + delta * hit.time_of_impact;
                                if let Some((part, _)) = CompositeShapeRef(&solid.shape)
                                    .project_local_point(p, radius * 0.01, false)
                                {
                                    let normal = Vector::new(hit.normal.x, hit.normal.y);
                                    fluid.positions[n] =
                                        Vector::new(p.x, p.y) + normal * (radius * 0.001);
                                    let incoming = (fluid.velocities[n]
                                        - velocities[part as usize])
                                        .dot(&normal);
                                    if incoming < 0.0 {
                                        let impulse = normal * (incoming * fluid.particle_mass(n));
                                        fluid.velocities[n] -= normal * incoming;
                                        let reaction = b.reactions.entry(*id).or_default();
                                        reaction.impulse.x += impulse.x;
                                        reaction.impulse.y += impulse.y;
                                        reaction.moment += fluid.positions[n].x * impulse.y
                                            - fluid.positions[n].y * impulse.x;
                                    }
                                }
                            }
                        }
                    }
                    for _ in 0..4 {
                        let p = PVector::new(fluid.positions[n].x, fluid.positions[n].y);
                        let Some((part, projection)) =
                            CompositeShapeRef(&solid.shape).project_local_point(p, 1.0e-6, true)
                        else {
                            break;
                        };
                        if !projection.is_inside {
                            break;
                        }
                        let offset = fluid.positions[n] - positions[part as usize];
                        let normal = offset
                            .try_normalize(1.0e-8)
                            .unwrap_or(Vector::new(0.0, -1.0));
                        fluid.positions[n] = positions[part as usize] + normal * (radius * 2.001);
                        let incoming =
                            (fluid.velocities[n] - velocities[part as usize]).dot(&normal);
                        if incoming < 0.0 {
                            let impulse = normal * (incoming * fluid.particle_mass(n));
                            fluid.velocities[n] -= normal * incoming;
                            let reaction = b.reactions.entry(*id).or_default();
                            reaction.impulse.x += impulse.x;
                            reaction.impulse.y += impulse.y;
                            reaction.moment +=
                                fluid.positions[n].x * impulse.y - fluid.positions[n].y * impulse.x;
                        }
                    }
                }
            }
        }
        let fluid = &b.world.fluids()[b.fluid];
        assert!(
            fluid
                .positions
                .iter()
                .chain(&fluid.velocities)
                .all(|p| p.x.is_finite() && p.y.is_finite()),
            "non-finite fluid state"
        );
    })
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_copy(
    backend: *mut Backend,
    samples: *mut Sample,
    count: u32,
) -> i32 {
    if backend.is_null() || samples.is_null() {
        return 2;
    }
    attempt(|| {
        let b = &*backend;
        let fluid = &b.world.fluids()[b.fluid];
        assert_eq!(fluid.positions.len(), count as usize);
        let out = std::slice::from_raw_parts_mut(samples, count as usize);
        for ((out, position), velocity) in
            out.iter_mut().zip(&fluid.positions).zip(&fluid.velocities)
        {
            *out = Sample {
                position: point(position),
                velocity: point(velocity),
            };
        }
    })
}

#[no_mangle]
pub unsafe extern "C" fn cc_fluid_reaction(
    backend: *mut Backend,
    id: u64,
    out: *mut Reaction,
) -> i32 {
    if backend.is_null() || out.is_null() {
        return 2;
    }
    attempt(|| {
        *out = (&*backend).reactions.get(&id).copied().unwrap_or_default();
    })
}
