# Editor workflow

## Scene editing

Edit > Undo/Redo and Ctrl+Z / Ctrl+Y (Ctrl+Shift+Z also redoes) restore scene documents. A continuous drag is one action. Scene snapshots include objects, transforms, materials, collider/body/ragdoll settings, gameplay settings, lights, graphics, weather, hierarchy (including sockets/attachments) and camera profiles. The history keeps up to 64 documents with a 32 MiB budget. Text entry keeps its own editing shortcuts. Saving a material/prefab resource writes that resource separately; scene undo does not roll back external asset files or project settings.

Tab preserves simulated transforms. Playback provides Pause/Resume physics, Step physics, Stop and keep transforms, and Stop and restore pre-play scene. Restoring is explicit; it does not change normal Tab behavior. Step advances PhysX, not every game subsystem.

## Inspector resources

Material asset and preview provides a lit swatch, albedo preview, named `.idkmat` save/load and material-file drops. The swatch approximates lighting; the main scene is the final rendered preview. Existing material controls expose roughness, metal, reflectance, emission, IOR, UV tiling and five texture slots. Drop a texture from Content Browser onto a slot's path field. These changes do not add a translucent glass renderer.

Prefab > Save selection as prefab saves a static mesh or imported model with its material, collider/body and gameplay settings. Source links survive scene save/load. Revert settings from prefab discards local settings; Publish settings to linked instances saves the source and updates matching linked objects while preserving their transforms. Unlink prefab keeps the object as an independent copy. Geometry/hierarchy restructuring is not propagated by the settings publishing command. Prefab Studio continues to support hierarchical archetypes and now preserves model settings/custom properties, imported mesh paths and instance transforms.

Content Browser keeps folder navigation, search and imports. Texture thumbnails load asynchronously with a bounded cache. OBJ cards have asynchronous wire previews (bounded file/geometry/cache sizes); other model formats keep type cards. Drag models or prefabs into free viewport space; double-click also instantiates them. Texture/material/audio drops are handled by their Inspector fields.

## Recovery

File > Scene recovery opens six rotating autosave slots plus the five previous saved versions and the atomic-save backup. Autosaves live in the project's `Saved/Recovery`; saved versions are adjacent to the editor scene. File contains an autosave interval control (15–300 seconds; default 60). A newer recovery copy or an unreadable saved scene prompts recovery at startup. Restoring affects the current scene and can be undone. Benchmarks suppress autosave and ordinary scene writes.

## Gameplay components

Inspector > Gameplay components adds proximity triggers, doors, buttons, pickups and activation sounds to static or imported objects. Settings persist in scenes, prefabs and game exports. Components run in Play while physics is running.

- Triggers use a sphere centered on the object origin; the radius is in metres.
- Buttons/pickups activate with E inside that radius.
- A trigger/button targets a door by its unique object name. Doors rotate about the mesh pivot around local Y, with configurable angle/speed.
- Pickups hide the object and remove its collider. This is a pickup action, not an inventory framework.
- Sounds use the spatial audio system, with asset path and volume controls.
- Once/cooldown controls limit activations. A reusable proximity trigger fires again after leaving/re-entering.

## Profiling and checks

Window/Debug > Engine Profiler shows CPU subsystem times and GPU timestamps for acceleration structures, sun shadows, raster/VSM, SSGI, DXR, AO/composite, rain, AA/upscaling/RR, fog/post and UI/FG input preparation. CSV snapshot export writes `Saved/Diagnostics/profiler.csv`. GPU work submitted by FG SDKs on other queues is outside these main-command-list timestamps; CPU Present is nested in Render time.

`tests/EditorWorkflowRegression.cpp` covers history branching/bounds, component serialization/rejection and prefab property persistence. `tools/Diagnostics/CheckEditorWorkflow.ps1` runs real editor scene operations, encrypted save roundtrips, prefab/hierarchy restoration, PhysX falling/pause/step and trigger-door/pickup actions, then validates ten GPU timings and verifies that the original source scene was unchanged. It requires an editor built from the current sources. Test artifacts belong to Diagnostics, not the build folders.

The audio emitter lifecycle is covered by `tests/AudioEmitterRegression.cpp` using a deterministic audio backend: start parameters, stable playback, stop/restart, component removal and node deletion. Interaction uses E; F remains available for the PhysX grabber. Socket definitions, attachment names/local offsets, root transforms and audio playback settings are included in scene snapshots.
