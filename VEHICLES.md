# Vehicle physics

Create > Vehicle (car), or Spawner > + Vehicle, creates a chassis, cabin, four
wheels, lamps and spokes. The assembly uses persistent UUID links. The default
chassis has a mass of 1200 kg, CCD, a cabin collision box and an adjustable centre
of mass. Visual wheels are not separate rigid bodies.

The engine uses PhysX Vehicle SDK engine, clutch, gearbox, limited-slip
differential and combined-slip tire solvers. Its custom suspension and convex
wheel sweeps feed those solvers and the PhysX chassis. This is not the complete
stock Vehicle component sequence. Wheel sweeps model tire width and radius;
contact queries exclude the chassis and trigger shapes. Suspension includes
spring, compression/rebound damping, bump stops and front/rear antiroll bars.
Opposite contact impulses act on dynamic ground.

Vehicles advance the scene with inner steps no longer than 1/240 second, with
finer drivetrain and tire integration. External forces are retained through those inner steps
and cleared afterward, preserving their intended duration. Existing bodies
already using retained accelerations keep that setting. Scenes without vehicle
states retain their usual configured physics timestep.

TAB switches edit/play and preserves the simulated pose. In play, V within six
metres enters/exits a car. W accelerates, S brakes before selecting reverse;
A/D steer; Space applies the rear handbrake. With automatic gears disabled,
PageUp/PageDown select reverse, neutral and gears 1–6. The camera follows the
car. Driving requires play mode and captured input. Gamepad driving is not
implemented. Unattended vehicles apply the parking brake.

Inspector > Vehicle exposes torque, RPM limits, engine inertia, clutch,
gear ratios, final drive, shift time, FWD/RWD/AWD balance, differential bias,
wheel mass/width, tire stiffness, grip, brake balance, ABS, traction control,
antiroll bars, centre of mass, drag/downforce and Ackermann steering. Existing
suspension and speed-limit parameters remain. Chassis mass, gravity, CCD,
damping and collider are in Physics. Units are metres, kilograms and seconds.
Weather grip combines the configured wetness with accumulated rain wetness.
The inspector displays speed, RPM, gear, contact count and tire slip.

Vehicle settings and part links persist in editor saves, levels and prefabs.
VEHICLE_DYNAMICS version 2 adds the advanced settings and weather coupling;
legacy VEHICLE entries load with compatible defaults. Prefab instances receive
new assembly UUIDs. Inspector deletion of the chassis also removes its visual
assembly. The explicit --add-vehicle editor command adds a car if absent or
upgrades the existing car settings and saves the opened project.

Run tools/Diagnostics/CheckVehicleDynamics.ps1 for real PhysX tests of neutral/free-rev,
redline, Ackermann steering, settings roundtrips, four contacts, dry/wet braking,
60/120 Hz consistency, airborne wheels and a 12 cm curb. Measurements are written
to Diagnostics/vehicle-dynamics-measurements.csv. Run
tools/Diagnostics/CheckEditorWorkflow.ps1 for assembly creation, scene/prefab roundtrips,
suspension, acceleration, braking, steering and reverse in the actual editor.
The editor check preserves the user's saved scene.

The default car is a tunable baseline, not a calibrated replica of a specific
vehicle. Tires have no deformation, temperature, wear or hydroplaning model;
there is no damage/deformable chassis or independently simulated wheel assembly.
Lamps use emissive materials, not projector headlights.

Reference: [PhysX vehicle model](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/docs/Vehicles.html).

Baseline measurements (2026-10-26, 1200 kg, flat box ground, default settings):

| Surface | Outer physics rate | 0–100 km/h | 100–0 km/h distance |
| --- | --- | --- | --- |
| Dry | 60 Hz | 7.80 s | 56.14 m |
| Wet | 60 Hz | 10.80 s | 72.43 m |
| Dry | 120 Hz | 7.79 s | 56.08 m |

These are automated simulation measurements, not real-world certification.
The editor workflow also passed acceleration, braking, steering and reverse;
its settled chassis height was 0.718 m with four wheel contacts.


Straight-line regression (2026-10-26): convex sweeps previously returned support
vertices on the tire rim as the force application point. These shifted between
frames and generated artificial yaw. Contact impulses now act at the projected
patch centre, with finer tire integration. With no steering, six seconds of
acceleration covers approximately 72 m with less than 1 mm lateral displacement
on a flat test surface, including a chassis initially rotated 90 degrees.
