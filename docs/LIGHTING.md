# Windows lighting

Raster and hybrid DXR use Cook–Torrance direct lighting, the same material Fresnel base reflectance and the same sky palette. Local point/spot lights are uploaded every frame to both GPU constant buffers. Disabled lights are excluded and unused slots are cleared.

Raster directional shadows use a 2048×2048 depth texture with 16-tap PCF. The texture explicitly uses one sample (a zero sample count previously caused allocation to fail silently), and its shader enable flag follows the shadow setting. Allocation failures are logged. Cloud shadows modulate direct sunlight in both modes, so strong cloud cover naturally reduces the contrast of the sun's object shadows.

Global illumination in the graphics panel controls screen-space diffuse bounce in raster mode and eight stratified cosine-weighted diffuse rays per primary surface in hybrid DXR. Intensity is saved as GI_INTENSITY in scene settings. Raster GI is not dispatched while DXR replaces the geometry image.

Raster SSGI uses the actual jittered camera projection, diffuse material albedo, 12 distance steps for each of two hemisphere samples, binary refinement of depth crossings, thickness/back-face rejection and an edge-aware 3×3 filter. Its private resource descriptors are refreshed after a resize or upscaler change. Its sources must be visible on screen; it cannot transport light from hidden geometry. SSAO affects the diffuse environment and indirect bounce, rather than multiplying direct lights or emission.

Hybrid DXR traces one diffuse bounce against scene geometry, including surfaces outside the screen. Terminal GI hits return shadowed direct lighting and emission without an unoccluded sky ambient term. Metal surfaces have no diffuse GI. GI is written to its own UAV, reusing the SSGI texture, and filtered with 81 contiguous samples over a 9×9 footprint, guided by surface plane, normals and diffuse albedo. Direct lighting, textures and reflections are composited separately. Reflection recursion is bounded to leave room for shadow rays within the four-level pipeline limit; very rough surfaces skip specular traces. This is one-bounce GI with temporal accumulation and spatial filtering, rather than a multi-bounce/probe solution. Difficult lighting can still require more samples.

The iterative path tracer evaluates direct PBR lighting in RayGen. ClosestHit returns material/emissive data without recursively evaluating the hybrid lighting path. Solar disk energy is excluded from diffuse environment samples to avoid counting the sun twice.

Regression checks: EditorDataRegression covers light upload, disabled lights, cleared slots, light limits and zero spot-direction fallback. Shader compilation and native raster/DXR benchmark runs are checked separately. Use --benchmark-gi 0 or --benchmark-gi 1 with the same scene/camera to compare indirect lighting. --benchmark-editor-lighting installs a temporary two-lamp, low-sun fixture without saving it into the project.

For shader-only iteration, close the editor and run tools/Content/RebuildEditorShaders.ps1. It compiles the Windows shader list from build.bat, stages fresh PAK files, regenerates the DLL integrity manifest, preserves an archive of the previous PAK files and replaces the editor archives without compiling C++ or generating .obj files. Project games are exported through BuildProject as usual.

## Hybrid artifact correction — 21.10.2026

Hybrid primary/reflection rays and path tracing use opaque two-sided intersections, matching the raster pipeline's CullMode NONE. Back-face culling previously selected the far side of some sphere meshes; face-forward normals then pointed into the object and GI self-occluded, making light-colored small objects black. Geometry winding in existing user assets is preserved.

Hybrid GI uses eight stratified cosine samples with a stable per-pixel rotation. Its spatial filter visits every pixel in the 9×9 footprint instead of a sparse 25-tap grid, retaining normal/plane/albedo edge rejection. This reduces stochastic grain without blurring the direct-light/material image. The additional temporal history described below addresses remaining low-frequency variance.

The Test scene also contains a sphere with zero albedo and metallic=1; it is black in raster and DXR. That authored material is preserved. Verification images are in Diagnostics/Benchmarks/EditorBuild/hybrid_*; native DXR, GI off and raster were compared at the same camera. On the clear-sky fixture, median GPU time changed from about 3.5 ms to 4.2 ms. This is an added GI sampling/filtering cost, not a general FPS estimate.

## Temporal hybrid GI

GI now changes its stratified sample rotation each frame and accumulates irradiance in a separate full-resolution RGBA16F history, independent of DLSS RR. Previous view-projection and camera position reproject the hit into history; stored ray distance rejects disocclusions. The exponential history blend keeps 94% of accepted history. The existing edge-aware spatial pass follows accumulation; direct lighting, albedo and reflections are not accumulated by this GI filter.

History is reset on texture recreation/resize, skipped DXR frames, mode changes, material/light settings changes and TLAS instance changes (including physics movement). This conservative reset avoids leaving old GI on moving objects; rapidly changing scenes can remain noisier. Hardware depth/distance rejection is approximate, so it is not equivalent to a production motion-vector/normal-guided denoiser. Screenshot metadata includes `gi_history allocated/valid/reused` for runtime verification.

Inspector → Collider & Rigidbody now has an explicit Enable physics button for the selected object. It enables collision, selects a dynamic body and exposes the existing mass, gravity, CCD, damping, collider shape/size, friction, restitution, force and impulse settings. Disable physics makes the collider static. These fields are persisted by the existing scene/level serialization. Run physics / Pause physics and Simulate one step control the global physics clock and clearly show its paused state; they do not change the persisted per-object body type. Ragdoll ownership still blocks conflicting rigid-body edits.
