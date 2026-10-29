# Idontknow Engine

**An experimental C++ game engine and editor built around DirectX 12.**

Idontknow Engine (repository: `MyEngine`) is a Windows-first engine project focused on real-time rendering, editor tooling, project packaging, and experimentation with modern graphics technologies. It includes a native editor, a project workflow, rendering and shader systems, diagnostics, and integrations with third-party graphics and platform SDKs.

The Windows build requires Visual Studio with C++ tools, a Windows SDK, and the third-party SDKs expected by `build.bat` under `ThirdParty`. The repository keeps `ThirdParty/Libraries/EngineDependencies` and `ThirdParty/Libraries/miniz`; the other SDKs, their binaries, and local build tools are not included and must be installed separately. Paths to Visual Studio and the Windows SDK in `build.bat` may need to be adjusted for your machine.

> **Project status:** Experimental and actively evolving. Not every subsystem is production-ready, every optional SDK is available on every GPU, or every platform build is self-contained. See the documentation and build scripts for feature-specific requirements.

Repository: https://github.com/iska0702/MyEngine

## Contents

- [Highlights](#highlights)
- [Rendering and graphics](#rendering-and-graphics)
- [Editor and project workflow](#editor-and-project-workflow)
- [Technology stack](#technology-stack)
- [Repository layout](#repository-layout)
- [Requirements](#requirements)
- [Build the Windows editor](#build-the-windows-editor)
- [Build a game project](#build-a-game-project)
- [Graphics features and compatibility](#graphics-features-and-compatibility)
- [Diagnostics and testing](#diagnostics-and-testing)
- [Documentation](#documentation)
- [Third-party dependencies](#third-party-dependencies)
- [Security notes](#security-notes)
- [Known limitations](#known-limitations)
- [Contributing](#contributing)
- [License](#license)

## Highlights

- **Native editor and engine** written primarily in C++ with DirectX 12.
- **Real-time rendering experiments**, including rasterization, optional DXR paths, mesh shaders, compute passes, lighting, shadows, fog, and post-processing.
- **Graphics SDK integrations** for NVIDIA DLSS/Streamline, AMD FidelityFX Super Resolution, Intel XeSS, and supported frame-generation/latency features.
- **Project packaging workflow** for producing standalone game builds from `.idkp` projects.
- **Content pipeline** with packaged content, texture mip preparation, and optional GDeflate/DirectStorage-related paths.
- **Weather rendering experiments**, including volumetric-style clouds, cloud shadows, rain impacts, and surface splashes.
- **Editor diagnostics** for render-pipeline state, SDK status, logs, and benchmark metadata.
- **Automated checks and technical notes** maintained alongside the implementation.

Feature availability depends on the current code path, build configuration, SDK versions, drivers, and hardware. The presence of an integration does not mean that every vendor feature is supported on every GPU.

## Rendering and graphics

The renderer is based on **Direct3D 12** and HLSL shaders. Documented areas of work include:

- Raster rendering with a capability-based fallback when mesh shaders are unavailable.
- Optional DirectX Raytracing (DXR) paths for features that require supported ray-tracing hardware.
- Mesh-shader rendering and meshlet visibility culling.
- Compute-shader passes and explicit Direct3D 12 resource transitions.
- Lighting, shadows, fog, temporal anti-aliasing, and rendering experiments such as screen-space/global-illumination helpers.
- Pipeline State Object (PSO) tracking and a pipeline-state library for supported engine-owned pipelines.
- Cloud and rain rendering experiments, including collision-anchored rain splashes.
- Texture mip preparation and content packaging.

Some systems are prototypes or have deliberately limited implementations. For example, the renderer documentation notes that hierarchical GPU occlusion culling is not implemented, the Hi-Z depth-pyramid builder is not itself an occlusion system, and some helper passes are not enabled in the main rendering path.

### Upscaling, anti-aliasing, and frame generation

The editor exposes graphics options for supported technologies, including:

- TAA
- NVIDIA DLAA and DLSS through Streamline, where supported
- AMD FSR temporal upscaling / native anti-aliasing
- Intel XeSS / native anti-aliasing
- Vendor-specific frame-generation and latency integrations where the corresponding SDK and hardware report support

The engine checks SDK-reported capability instead of assuming that every mode works on every GPU. Availability, quality, performance, and generated-frame support vary by device and driver. Do not interpret the presence of a menu entry or SDK integration as a guarantee of support.

See [`INTEGRATIONS.md`](INTEGRATIONS.md) and [`ANTI_ALIASING.md`](ANTI_ALIASING.md) for implementation details and limitations.

## Editor and project workflow

The repository includes a Windows editor and an example project under `Projects/Test`.

The editor's project workflow supports opening `.idkp` projects and building a game through **Project → Build game** (also available from Project Settings). Depending on the project and the installed/prepared runtime, build targets include:

- Standalone
- Steam
- Epic Online Services

The project builder runs asynchronously, reports progress and logs, supports cancellation, and preserves the previous successful build when a new build fails or is cancelled. A packaged project contains the game executable, content packages, and the runtime libraries required by the selected target.

The project format supplies settings such as project name, startup scene/level, menus, splash screen, and physics configuration. Content and project data are packaged by the engine; consult the security and project-building documentation before distributing a build.

### Command-line project build

Once the editor and compatible runtime support are prepared, a project can be built from the command line:

```powershell
.\EditorBuild\Idontknow.exe --build-project "D:\Projects\Game\Game.idkp"
```

An explicit target can be supplied:

```powershell
.\EditorBuild\Idontknow.exe --build-project "D:\Projects\Game\Game.idkp" --build-platform standalone
```

The `--build-platform` argument is optional when the project already defines a target. See [`PROJECT_BUILDING.md`](PROJECT_BUILDING.md) for the full workflow and runtime-SDK preparation notes.

## Technology stack

| Area | Technology |
|---|---|
| Primary language | C++ |
| Graphics API | Direct3D 12 |
| Shader language | HLSL |
| Windows toolchain | MSVC / Visual Studio and Windows SDK |
| Build and project tooling | CMake, batch scripts, PowerShell tools |
| Editor UI | Dear ImGui is included in the project dependencies |
| Model/animation and supporting systems | Third-party libraries and SDKs tracked or referenced by the project |
| Graphics integrations | NVIDIA Streamline/DLSS, AMD FidelityFX, Intel XeSS, depending on build and hardware |
| Optional platform integrations | Steamworks and Epic Online Services, subject to their SDK requirements |

This table describes the technologies visible in the repository and its documentation; it is not a claim that every optional dependency is bundled or enabled in every build.

## Repository layout

The main top-level areas include:

| Path | Purpose |
|---|---|
| `src/` | Engine and editor source code |
| `shaders/` | Shader source and related graphics resources |
| `cmake/` and `CMakeLists.txt` | Build configuration |
| `ThirdParty/` | Included dependencies and locations/documentation for external SDKs |
| `Projects/Test/` | Test/example project content |
| `tests/` | Automated tests and test support |
| `tools/` | Build, packaging, diagnostics, and maintenance scripts |
| `.github/workflows/` | GitHub Actions workflows |
| `*.md` documentation files | Feature notes, implementation details, checks, and limitations |

Some external SDKs, prebuilt binaries, local tools, and generated files are not included in Git and must be installed or prepared separately.

## Requirements

### For building the Windows editor

- Windows
- Visual Studio with the **Desktop development with C++** workload and MSVC toolchain
- A compatible Windows SDK
- Git, with Git LFS if repository assets use LFS
- CMake and any additional tools required by the active build scripts
- The third-party SDKs expected by `build.bat` for the selected target

A DirectX 12-capable GPU is needed to run the editor's graphics features. DXR, mesh shaders, DLSS, XeSS, FSR frame generation, and other optional features have additional hardware, driver, or SDK requirements.

> The repository does not include every external SDK or binary dependency. A successful checkout alone may not be enough to compile all targets.

## Build the Windows editor

Clone the repository and its submodules:

```powershell
git clone --recurse-submodules https://github.com/iska0702/MyEngine.git
cd MyEngine
```

The current repository documentation specifies the included test project's content as the content source for a standalone editor build:

```powershell
$env:IDK_PROJECT_CONTENT = (Resolve-Path '.\Projects\Test\Content').Path
.\build.bat editor standalone
```

The documented output path is:

```text
EditorBuild/Idontknow.exe
```

Build scripts may expect external SDKs and binaries under `ThirdParty` or at paths configured for the developer's machine. If the build fails, check the first compiler or dependency error in the log, confirm the relevant SDK is installed, and inspect `build.bat` for target-specific path assumptions.

### GitHub Actions

The repository contains `.github/workflows/`. CI workflows depend on the runner image, installed Visual Studio components, SDK availability, submodules, Git LFS assets, and any external dependencies required by the build. A workflow being queued or starting is not proof that the project compiles successfully; verify the final job result and uploaded artifact.

## Build a game project

1. Build or obtain a compatible editor/runtime SDK.
2. Open a `.idkp` project in the editor.
3. Select **Project → Build game** or open the same action from Project Settings.
4. Select a supported target: Standalone, Steam, or Epic Online Services.
5. Review the build log and launch the result from the build output.

The packaged result is placed beside the project in a `Build/` directory. Its exact contents depend on the selected runtime target. Read [`PROJECT_BUILDING.md`](PROJECT_BUILDING.md) before preparing a distributable build.

## Graphics features and compatibility

The engine contains integrations and experiments whose availability is determined at runtime. Important distinctions:

- **DirectX 12** is the primary rendering API.
- **DXR** is optional for the renderer; ray-tracing-dependent features require a compatible adapter and driver.
- **Mesh shaders** are capability-dependent; a vertex-pulling raster fallback is documented for devices without mesh-shader support.
- **DLSS / DLAA / Ray Reconstruction / NVIDIA Frame Generation** depend on the relevant NVIDIA hardware, driver, and Streamline SDK support.
- **FSR and XeSS** depend on their respective SDK integrations and the selected mode.
- **Steamworks and Epic Online Services** builds require their applicable SDKs and configuration.
- **DirectStorage/GDeflate** paths require compatible content and platform support; ordinary image fallbacks may be used for unprepared assets.

The project documentation explicitly identifies several prototype or incomplete areas, including parts of virtual shadow maps / virtual texturing and certain GPU culling or rendering paths. Consult the individual technical notes rather than assuming that a named technique is complete.

## Diagnostics and testing

The repository includes tests and diagnostic scripts for different engine areas. Depending on the test, these can cover project building, input handling, rendering transforms, content loading, network/server behavior, vehicle dynamics, editor workflow, or SDK integration status.

Useful entry points include:

- `tests/`
- `tools/Diagnostics/`
- `INTEGRATIONS.md`
- `GRAPHICS_CHECK.md`
- `FIX_VALIDATION.md`
- `INPUT_RELEASE_CHECK.md`
- `TECHNOLOGY_AUDIT.md`

Run only the checks appropriate to your environment and installed SDKs. Results from one GPU, driver, build, or scene should not be treated as a universal performance benchmark.

## Documentation

The repository has dedicated technical notes. Start with the documents most relevant to your task:

| Document | Subject |
|---|---|
| [`EDITOR_WORKFLOW.md`](EDITOR_WORKFLOW.md) | Editor workflow |
| [`PROJECTS.md`](PROJECTS.md) | Project organization |
| [`PROJECT_BUILDING.md`](PROJECT_BUILDING.md) | Building `.idkp` projects and runtime support |
| [`RENDER_BACKEND.md`](RENDER_BACKEND.md) | Direct3D 12 renderer, mesh shaders, PSO and compute-path notes |
| [`ANTI_ALIASING.md`](ANTI_ALIASING.md) | Anti-aliasing and upscaling modes |
| [`INTEGRATIONS.md`](INTEGRATIONS.md) | Graphics SDK integrations and compatibility notes |
| [`LIGHTING.md`](LIGHTING.md) | Lighting implementation notes |
| [`WEATHER_RENDERING.md`](WEATHER_RENDERING.md) | Clouds, rain, and weather rendering |
| [`TESSELLATION.md`](TESSELLATION.md) | Tessellation notes |
| [`VEHICLES.md`](VEHICLES.md) | Vehicle systems and validation |
| [`OPTIMIZATION.md`](OPTIMIZATION.md) | Optimization notes |
| [`PERFORMANCE_CLEANUP.md`](PERFORMANCE_CLEANUP.md) | Performance and cleanup work |
| [`FILE_SECURITY.md`](FILE_SECURITY.md) | File and content security |
| [`NETWORK_SECURITY.md`](NETWORK_SECURITY.md) | Network-security notes |
| [`ANTICHEAT.md`](ANTICHEAT.md) | Anti-cheat-related design notes |
| [`STEAMWORKS.md`](STEAMWORKS.md) | Steamworks integration |
| [`ADVANCED_SHADER_DELIVERY.md`](ADVANCED_SHADER_DELIVERY.md) | Shader delivery |
| [`TECHNOLOGY_AUDIT.md`](TECHNOLOGY_AUDIT.md) | Technology audit and validation notes |

Some documents describe design goals, prototypes, or tests performed on a specific machine. Check the document's date and stated limitations when relying on a result.

## Third-party dependencies

MyEngine uses third-party libraries and SDKs. Some are present in the repository; others must be obtained separately and may have their own license terms, redistribution requirements, and platform restrictions.

Before distributing the engine or a game made with it:

1. Review the license of every bundled dependency.
2. Confirm that external SDK redistribution is permitted.
3. Include required third-party notices.
4. Do not commit private SDK packages, credentials, encryption keys, generated key headers, or private game content.

## Security notes

The repository contains documentation for content packaging and security-related systems. These features should not be interpreted as a guarantee that a game is impossible to reverse-engineer or cheat in. Security depends on implementation, key management, deployment, server-side validation, and the threat model.

Keep secrets outside source control. The repository documentation notes that local encryption keys, generated key headers, build outputs, project saves, diagnostics, and recovered backups are excluded from Git. Review `.gitignore` and the relevant security documents before publishing changes.

## Known limitations

- The project is experimental and its architecture and APIs may change.
- Not all third-party SDKs or binary dependencies are included in the repository.
- Optional rendering features vary by GPU, driver, SDK version, and build configuration.
- Some rendering and streaming systems are prototypes or are not wired into the main rendering path.
- Successful compilation does not guarantee that every optional feature works on all supported hardware.
- No universal FPS, image-quality, or compatibility guarantee is implied.

## Contributing

Issues and focused pull requests are welcome, subject to the project's current scope.

When reporting a bug, include:

- Operating system and GPU
- Driver version
- Build configuration and commit
- Relevant SDK versions
- Steps to reproduce
- The first useful compiler/runtime error and a relevant log or capture

For code changes, keep the patch focused and mention which tests or diagnostics were run. Avoid committing generated build folders, private assets, credentials, or SDK files that are not permitted for redistribution.

## License

Check the repository for a `LICENSE` file before copying, modifying, or redistributing this project. If no license is present, do not assume that the code is open source merely because the repository is public.
