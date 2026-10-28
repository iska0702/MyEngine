# D3D12 render backend

Open **Debug → Render Pipeline State**, or press **F8** in the editor. The panel
lists engine-owned graphics, mesh and compute PSOs, creation time, library hits
and errors, plus mesh/vertex/compute dispatch counts and named compute passes.

## Mesh shaders

The amplification shader tests meshlet bounding spheres against all six clip
planes before compacting the visible meshlets into its payload. Clip planes
are transformed into object space, retaining conservative bounds under
non-uniform and negative scale. Main rendering and sun shadows use the same
culling code with their respective view/projection matrices. Meshlets contain
at most 42 triangles/126 output vertices, matching the mesh shader output budget.
Two-sided materials retain two-sided rendering; normal-cone rejection is not
enabled. Hierarchical GPU LOD and HiZ occlusion rejection are not implemented.

Capability detection selects a vertex-pulling raster pipeline when mesh shaders
are unavailable. It preserves material shading, motion vectors, normals,
lighting guides and sun shadows. DXR is optional: without raytracing tier 1.1,
the renderer retains raster/compute rendering and disables its RT-dependent GI.
Shader Model 6.5 is still required. Forced fallback tests do not substitute for
testing physically unsupported hardware.

## Compute shaders

The renderer's VSM, surface-cache, SSGI, fog, TAA and SSR helper dispatches use
one named dispatch path, with zero-size and D3D12 group-limit checks. Existing
resource transitions and pass dependencies remain explicit. This uses the direct
queue; a separate asynchronous compute queue is not implemented.

The optional **Build hierarchical depth pyramid** switch runs the HiZ compute
pipeline. Mip zero copies depth; later mips conservatively reduce maximum depth,
including odd texture dimensions. Per-mip SRV/UAV views and subresource barriers
order reads and writes. It is disabled by default because GPU HiZ occlusion is
not yet a consumer. Enabling the builder does not enable SSR or occlusion.

The existing SSR helper is not enabled by this integration. The diagnostics
distinguish created pipelines from passes that actually dispatched.

## Pipeline state library

Engine-owned main, mesh, tessellation, post-processing, rain, particle and terrain
PSOs use one Pipeline Library manager. Cache keys include the executable build,
pipeline role, shader bytecode and registered root-signature contents. The D3D12
library also validates the full requested pipeline descriptor before loading;
an incompatible entry falls back to normal creation. Immutable state variants
must use distinct pipeline role names. Root/shader changes require fresh ASD
captures independently of this local cache.

Libraries live under `%LOCALAPPDATA%/Idontknow/ShaderCache/<project GUID>/<adapter>/`.
They are local to the executable build and validated by D3D12 for driver/runtime
and adapter compatibility. Source builds and project exports do not include them.
The loaded blob remains alive for the library's entire lifetime. Serialization
uses an exclusive writer lock and atomic replacement; readers can still create
pipelines if another process owns the writer lock. Corrupt/incompatible blobs
are discarded. Cache reads/writes are capped at 256 MiB. Existing build caches
are retained; no automatic directory-wide deletion is performed.

SDK-internal and ImGui pipelines retain their vendor implementations. DXR state
objects retain the existing creation path and ASD coverage; they are not PSOs
stored in ID3D12PipelineLibrary. ASD and this driver-local library serve different
delivery/workflow purposes.

## Verification

`./tools/Diagnostics/CheckRenderBackend.ps1` exercises hardware mesh and compute statistics,
forced vertex/no-DXR capability paths, out-of-frustum AS rejection, second-launch
cache hits and deliberately corrupted-cache recovery. It preserves the source
scene and modifies only the current executable's cache during the recovery test.
GPU queries sample the first mesh/compute dispatch, rather than claiming a
whole-frame count. Counts and timings in the editor distinguish those roles.

Validated on RTX 3060 / NVIDIA 621.01: first startup created and stored 26 PSOs;
the second loaded all 26 with zero creation/storage failures. The first sampled
mesh draw executed 32 AS invocations, 128 MS invocations and produced 8 primitives.
The sampled compute dispatch executed 921600 invocations. Moving the benchmark
geometry outside the frustum retained AS execution and produced zero MS work.
Forced vertex/no-DXR mode rendered successfully; a deliberately truncated cache
was rebuilt successfully. Tessellation, editor workflow, material preview and
normal DXR rendering checks passed. Editor and Test project exports built; the
project executable rendered and exited successfully. Authored scene data was
preserved. These checks do not establish a general FPS improvement.

References: [Microsoft pipeline libraries](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device1-createpipelinelibrary),
[Mesh Shader specification](https://microsoft.github.io/DirectX-Specs/d3d/MeshShader.html).
