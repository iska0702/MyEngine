# Tessellation

Select an object and open **Inspector → Tessellation**. Enable tessellation and
adjust factor (1–16), surface smoothing (0–1), full-detail distance and
minimum-detail distance. Settings belong to the material and persist in scene
saves, Undo/Redo, material assets and prefab settings. Old IDKMAT1 materials
remain readable; new material files use IDKMAT2.

This is hardware triangle tessellation using vertex, hull and domain shaders.
The domain shader applies Phong smoothing by projecting points onto the vertex
tangent planes. A smoothing value of zero subdivides the original triangles
without changing their surface. Shared edges use symmetric midpoint-distance
factors and fractional-odd partitioning; distant edges fade to factor one.
Matching edge positions, normals and settings are required to avoid seams
between separate meshes or hard normal splits.

Enabled objects use the tessellation pipeline in both the raster main pass and
sun shadow pass. Other objects retain the mesh-shader pipeline. Color, motion
vectors, normals and lighting guide targets retain the existing PBR formats.

DXR currently uses the original geometry and automatically bypasses tessellation
for the whole frame. Hull/domain output is not baked into BLAS. Colliders also
retain their authored geometry. Height-map displacement, editor wireframe
preview, collision baking and Metal tessellation are not implemented.

Shaders are precompiled by build.bat and included in existing content PAKs.
After changing shaders, including this integration, recapture the project's ASD
database before evaluating PSDB coverage. Previous databases describe older
pipeline/root-signature combinations.

For bounded validation use `--benchmark-tessellation` with the existing benchmark
flags. It enables tessellation only on the in-memory benchmark scene; it does
not save those changes. The log records tessellated draws and GPU hull/domain
invocations for the first tessellated draw. Normal launches keep tessellation
disabled until enabled on a material.

Validation on RTX 3060: `./tools/Diagnostics/CheckTessellation.ps1` passed. The first
measured draw reported 8 hull invocations and 584 domain invocations for
8 input patches. Raster sessions drew tessellated objects; DXR sessions used
zero tessellated draws. Material and scene roundtrips, legacy material loading,
the editor workflow and material texture preview passed. The authored source
scene hash was preserved. This validates functionality, not a general FPS gain.

Reference: [Microsoft tessellation stages](https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-advanced-stages-tessellation),
[mesh-shader pipeline restrictions](https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html).
