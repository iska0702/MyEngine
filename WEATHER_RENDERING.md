# Clouds and rain impacts

The cloud renderer and raster/DXR cloud shadows share weather-scale coverage,
wind and volumetric density. A broad billow field, height profile and fine
silhouette erosion replace the single undifferentiated noise field. Lighting
integrates three samples toward the sun plus an approximate multiple-scattering
term and height-dependent ambient illumination. Extinction uses Beer–Lambert
integration; premultiplied cloud radiance is composited once. Ray-march sampling
adapts to distance and uses stable pixel jitter to suppress horizon banding.
This remains an approximate real-time volumetric model, without a full spectral
atmosphere or dynamically simulated cloud microphysics.

Rain impacts emit an expanding surface ring, a crown and three ballistic beads.
Their positions are anchored to collision hits. The finite 8192-entry pool is
reused, including after disabling and re-enabling rain. Rings lie in the hit
surface tangent plane. Existing collision-box roof shelter and ground fallback
remain; detailed triangle-level rain collisions are not implemented.

Verification on 2026-10-26:

- tools/Diagnostics/CheckVehicleDynamics.ps1: neutral, drivetrain, contact stability,
  straight-line driving at 0/90 degrees, wet braking, timestep comparison,
  airborne wheels and curb traversal passed.
- tools/Diagnostics/CheckEditorWorkflow.ps1: vehicle integration and repeated rain
  disable/enable with surviving impact splashes passed; saved scene preserved.
- Raster and Hybrid DXR rain captures were visually inspected at 1280x720.
  Revised clouds had no broad horizon bands or blown-out white lighting;
  visible ground rings and splash particles remained anchored to impact sites.

Frames and timing logs are under Diagnostics/Benchmarks/EditorBuild/
weather_splashes_raster_v2 and weather_splashes_dxr.

## Elevated clouds and directional sunset (2026-10-26)

Cloud bases now range from 1400 m in fair weather to 1000 m in storms, with
650–900 m layer depth. Larger billows and weather coverage preserve useful
angular scale at the increased altitude. Raster and DXR shadows use the same
layer bounds and wind. Solar extinction sampling distances were scaled with
the deeper layer; the visible march is bounded to 40 km.

At low sun elevations the solar horizon receives a warm directional lobe,
while the opposite hemisphere keeps a dimmer blue twilight palette. Cloud
sunset radiance and ambient illumination vary with solar azimuth instead of
applying the same warm colour to every view direction. The opposite sky retains
faint ambient light; it does not become a black hemisphere.

The explicit editor diagnostic option --benchmark-sky takes sun elevation,
sun azimuth and view yaw in degrees. It is applied only to bounded benchmark
runs and does not change the project's saved environment.

Directional sunset verification: tools/Diagnostics/CheckSunsetSky.ps1 compares captured
solar-facing and opposite sky regions at 5 degrees sun elevation. Both raster
and Hybrid DXR passed: solar-facing/opposite luminance ratios 1.72 and 1.71,
respectively, with a red/blue warmth ratio difference of 1.46. The saved scene
hash is checked before and after the run. Captures are in
Diagnostics/Benchmarks/EditorBuild/sunset_toward, sunset_away,
sunset_dxr_toward and sunset_dxr_away.

## Surface celestial-disk artifact

Surface environment sampling now excludes the analytic sun/moon disk. It had
been added to the raster specular environment and DXR sky-miss fallback as a
bright circular patch, independently of the direct-light BRDF. The path tracer
also excludes that analytic disk after the primary camera ray. Primary sky
visibility and direct lighting keep their normal solar/lunar representation.
