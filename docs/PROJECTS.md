# Projects (.idkp)

Open `Projects/Test/Test.idkp` with `EditorBuild/Idontknow.exe`. Starting the editor without a file shows Open/Create Project. Opening a project registers the .idkp file association for the current Windows user.

```powershell
./EditorBuild/Idontknow.exe --project "D:\MyEngine\Projects\Test\Test.idkp"
./EditorBuild/Idontknow.exe --create-project "D:\MyEngine\Projects\NewGame\NewGame.idkp"
```

Project > Project Settings configures the name, first .level/.pak, main-menu HTML, splash HTML/image/video, splash duration, initial menu visibility, window dimensions and store platform. Paths are relative to the .idkp directory. An empty first-level setting uses the saved editor scene. Startup settings apply on the next launch. Save the project settings before closing.

Additional project settings: developer, version and description; gravity XYZ, simulation frequency and maximum substeps; player walking/running/crouching speeds, jump velocity, push force and gravity; camera FOV; ragdoll total mass, bone collider radius, angular damping and joint swing/twist limit. Old descriptors use compatible defaults. Settings are validated and stored as tuning_* fields. Runtime defaults apply on the next launch.

Inspector > Collider & Rigidbody exposes mass in kilograms, friction, bounce, gravity, CCD, linear/angular damping and force/impulse XYZ controls for dynamic bodies. A force button applies one force sample; an impulse changes momentum immediately. These controls use the actual PhysX body. Rigidbody mass uses setMassAndUpdateInertia rather than treating kilograms as density.

Select an imported skeletal character in Scene hierarchy, then open Inspector > Ragdoll. Enable ragdoll physics starts simulation bodies and temporarily replaces the mesh Rigidbody; Disable restores that Rigidbody. The same panel is available in the Character inspector. At least five humanoid body bones must be mapped in Bone Mapper. Ordinary objects use Collider & Rigidbody instead.

Each object can inherit project ragdoll defaults or override total mass, bone collider radius, angular damping and joint limits. Linear damping, gravity and CCD are configurable too. Edits update active PhysX bodies immediately, without resetting their pose or velocity. Settings are saved with scene objects and level entities. Ragdoll activation is a temporary simulation state. TAB runs the simulation; Step physics advances one physics step while editing. Apply ragdoll impulse applies the specified total impulse across the bodies.

Ragdoll uses sphere colliders on body joints and D6 joints with angular limits; unmapped bones follow their nearest simulated ancestor. Transform editing is disabled in Inspector during simulation. Stop ragdoll before editing its transform. This is a generic starting rig, without a per-bone collider authoring tool.

The editor scene and its ImGui layout are stored in Saved/Editor.save. Exported games keep runtime saves under %LOCALAPPDATA%/Idontknow/Games/<installation>/Saved, independently of the editor scene. File-based loaders and WebView use a per-build cache in the same user directory, populated from the PAK files on first launch.

Build Game saves the scene and opens a build console. Output is <project>/Build. No global ReleaseBuild is used. Its root contains only Idontknow.exe, Contents and ThirdParty. Contents contains only content01.pak through content10.pak. Project settings, resources and the scene snapshot are packed into content01–09.pak; content10.pak holds DirectStorage streaming textures. Distribute the entire Build directory.

```powershell
./tools/Build/BuildProject.ps1 -ProjectFile "D:\MyEngine\Projects\Test\Test.idkp"
./build.bat release standalone "D:\MyEngine\Projects\Test\Test.idkp"
```

BuildProject uses the platform in the project file unless explicitly overridden. Compilation uses a temporary .ProjectBuild staging folder, protected by a single-build lock. A failed compilation preserves the last successful Build. Previous successful builds are archived under Diagnostics/Archive/ProjectBuilds. Compiler .obj files are deleted.

New projects copy generic UI resources from Templates/Default/Content. Project Content and Saved directories are isolated from other projects. Test was seeded with a copy of the existing editor scene; the old Documents save remains unchanged.

The descriptor is UTF-8 text: IDKP 1 followed by named settings. String values are quoted. Unsupported versions, missing startup assets and paths outside the project folder are rejected.

Verified locally: descriptor round-trip and malformed archive guards; project creation/opening with Unicode and spaces; overwrite protection; the project launcher waits without opening a world; a configured first level loads from a Unicode project path; Test renders and exits normally in deployment smoke mode without changing Saved/Editor.save. No global ReleaseBuild or staging directory remains after a successful build.

Optional deployment check: `Projects/Test/Build/Idontknow.exe --smoke-test` renders a short test and exits without saving runtime state.

## Tab and editor scene markers

Tab switches between editing and play. Editing pauses PhysX without calling ResetSimulation: object positions, rotations and rigid-body state remain at the simulation result. Returning to play resumes that state. The explicit Reset Simulation action remains available when a reset is actually wanted. Moving a body with the editor transform tools still explicitly sets its actor pose.

Window → Scene gizmos (editor only) controls the default camera/light/audio overlays. Cameras show a body icon, direction and short FOV frustum; selecting their badge opens Camera Studio. Point lights show icons and, when selected, range circles. Spot lights show direction/cone previews and their full range when selected. Disabled lights remain locatable as gray markers. Clicking a light badge selects its Inspector entry. The Sun marker opens lighting settings. Spatial-audio nodes receive location badges. Markers are drawn behind UI windows and are absent in play mode and exported games.

The editor viewport is detached from CameraSystem profiles while editing, so navigating the editor does not drag the game camera marker along. Camera profile angles use degrees, with explicit conversion to/from Camera's radians. EditorCameraRegression checks angle roundtrip, camera creation, detached navigation and synchronization in play. The combined editor-UI/lighting benchmark installs temporary camera/light fixtures without saving them into the user's project.
