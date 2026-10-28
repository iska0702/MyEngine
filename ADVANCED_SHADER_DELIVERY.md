# Advanced Shader Delivery

Idontknow uses Microsoft Agility SDK 1.619.6 (SDK 619). Each project has a stable
shaderAppId GUID. The renderer supplies its application identity before every
D3D12 device probe. Project Settings displays the actual default-PSDB status
queried from ID3D12DeviceStatistics; SDK presence is not treated as activation.

## Capture, compile, verify, connect

Run in the engine directory with the editor/game closed:

```powershell
./tools/Graphics/AdvancedShaderDelivery.ps1 -Action Support
./tools/Graphics/AdvancedShaderDelivery.ps1 -Action Capture -ProjectFile ./Projects/Test/Test.idkp
./tools/Graphics/AdvancedShaderDelivery.ps1 -Action Compile -Database '<capture.sodb>'
./tools/Graphics/AdvancedShaderDelivery.ps1 -Action Test -Database '<capture.psdb>'
./tools/Graphics/InstallAdvancedShaderDelivery.ps1 -Database '<capture.psdb>'
./tools/Graphics/InstallAdvancedShaderDelivery.ps1 -Action Status
```

Capture/Test exercise raster, DXR, forced Vertex rendering and tessellation.
Use -Modes raster,dxr to select fewer modes. These checks set
IDK_ASD_VERIFICATION in child processes to bypass the local Pipeline Library:
its hits must not mask PSDB coverage. The variable and Direct3D settings are
restored on success/failure. The scripts share an exclusive lock and reject
concurrent engine instances. Capture/Test reject other configured d3dconfig
apps because their temporary device options apply to the complete app list.

Captures are under Projects/<project>/Saved/ASD. Compile logs warn about partial
driver output even when Microsoft's compiler returns exit code zero. Test checks
application identity and defaultPSDB=1 for each mode. Inspect the final object
counts, PSDB misses and unknown results; unknown SO results are not cache hits.
Captures cover exercised paths, not all hardware, content or SDK permutations.

Install tries native current-user registration first. On the observed registration
error 0x80070006 it can instead connect a persistent **local developer override**
using Microsoft's d3dconfig XML import. This is explicitly reported as
DeveloperOverride, not successful native registration. Add -RequireRegistration
to prohibit this fallback (for installer/production validation).

By default Install binds the exact editor executable path and this project's
Build/Idontknow.exe if present. -Executables selects explicit targets. A stable
copy of the PSDB is retained in the project's Saved/ASD/Installed/<SHA256>.
The SODB, when alongside its source PSDB, is retained there too. The ownership
record is Diagnostics/asd-local-install.json. Only one managed local installation
is active at a time: remove it before changing project/database.

The fallback changes only the ASD fields of selected app entries in the exported
XML. Other applications and their options are preserved. No persistent global
`device precompiled-db-path=...` command is used by Install. The runtime still
checks database compatibility. Other projects/shader variants can miss this PSDB
and fall back to runtime compilation; they are not silently precompiled.

To disconnect and restore previous settings:

```powershell
./tools/Graphics/InstallAdvancedShaderDelivery.ps1 -Action Remove
```

Remove refuses to overwrite externally changed ASD fields. Non-ASD changes to an
entry added by Install are preserved rather than deleting that entry. Database
files are retained. Install saves before/applied XML for recovery and restores
its snapshot if applying its configuration fails.

After changing shaders, root signatures, pipeline state, identity or driver,
remove the old binding, capture/compile/test again and install the new PSDB.
Rebuilding an EXE at the same path retains the binding; that does not prove the
old PSDB covers the changed pipelines.

## System registration diagnosis

On this machine both Agility 1.619.6 and the inbox Direct3D runtime fail native
RegisterApplication with HRESULT 0x80070006. A small direct-API probe reproduced
the failure independently of the engine. Debugging found RegCreateKeyTransacted
returning ERROR_RM_NOT_ACTIVE (6801), followed by the invalid-handle HRESULT.
No runtime patch, registry schema fabrication, OS reset or reboot was used.
The local developer override avoids this failed registration path.
Diagnostics/asd-probe-debug8.log records the transaction failure; the probe is
only diagnostic code under Diagnostics and is not shipped with the engine.

The offline NVIDIA compiler also rejects some RT pipelines on its offline device.
The engine retains runtime creation/caching for those entries. This integration
cannot manufacture driver support for rejected pipelines.

## Shipping

PSDBs are driver ABI/GPU-family specific. A developer override for two local paths
is not a portable distribution mechanism. For shipping, supply the captured SODB
to an ASD-capable storefront/installer, which compiles/delivers compatible PSDBs
and registers each installed executable. Native Register/Unregister remain
available in AdvancedShaderDelivery.ps1. Store submission, multi-vendor coverage
and successful production registration are not claimed on this machine.
Project Build/Contents retains exactly its ten PAK archives; developer PSDBs,
configuration backups and logs are kept outside the game export.

## Sources

- [Microsoft integration guide](https://github.com/microsoft/DirectX-Graphics-Samples/blob/master/Samples/Desktop/D3D12AdvancedShaderDelivery/readme.md)
- [Application identity](https://microsoft.github.io/DirectX-Specs/d3d/D3D12ApplicationIdentity.html)
- [Shader cache registration API](https://microsoft.github.io/DirectX-Specs/d3d/ShaderCacheRegistrationAPI.html)
- [Agility SDK](https://devblogs.microsoft.com/directx/directx12agility/)

## Verified local launch: current vehicle build

The editor and Test game were rebuilt through build.bat. The Test scene now
contains a car assembly. CheckASDLaunch.ps1 passed all four editor modes with
54/55 known PSO hits and the game smoke test with 52/53. Every run reported
defaultPSDB=1 with the separate Pipeline Library bypassed. The DXR SO result
remained unknown, excluded from PSO ratios. Ordinary process launches used the
persisted Direct3D12 app entries; the checker did not set temporary d3dconfig
paths. Native registration remains unavailable, so the active mode is the local
DeveloperOverride. This is not a claim of storefront/production registration.

The Direct3D12 branch of exported XML is deliberately used; the root Direct3D
application entries configure a different API branch and do not activate this
D3D12 override. Detailed evidence: Diagnostics/asd-vehicle-final-launch-check.log,
Diagnostics/asd-installed-game.log, Diagnostics/vehicle-editor-workflow.log and
Diagnostics/Benchmarks/EditorBuild/editor_workflow_integration/run.log.

Vehicle dynamics update verification (2026-10-25): editor and project game
rebuilt with native PhysX drivetrain/tire solvers. Persisted ASD binding checks
passed raster, DXR, vertex and tessellation modes: 54/55 PSO database hits each.
The exported game rendered and exited 0, defaultPSDB=1 and 52/53 PSO hits.
Existing DeveloperOverride installation remains active; this does not change
the native registration limitation documented above. Details:
Diagnostics/asd-realistic-vehicle-launch-check.log.

Weather/steering update verification (2026-10-26): rebuilt the PSDB after cloud
and rain shader changes. Offline compilation produced 76 PSOs, with one PSO and
one state object rejected by the driver and left for normal runtime compilation.
Persisted editor checks passed raster, DXR, vertex and tessellation at 54/55 PSO
hits each. The new database is installed through the existing per-executable
DeveloperOverride fallback; native registration still returns 0x80070006.
See Diagnostics/asd-weather-editor-check.log and asd-weather-game-check.log.

Raised-cloud/sunset update (2026-10-26): refreshed the local driver PSDB after
shader changes. Raster, DXR and vertex launches obtained 54/55 PSO hits. One
initial tessellation test terminated before renderer initialization; Windows
Application Error 1000 identifies discord_partner_sdk.dll, exception 0xc0000409.
Two subsequent tessellation launches exited 0 with 54/55 PSO hits. The exported
game rendered and exited 0 with defaultPSDB=1 and 52/53 PSO hits. Driver offline
compilation still rejects one PSO and one state object, using runtime fallback.
Persistent installation remains DeveloperOverride because native registration
returns 0x80070006. Logs: Diagnostics/asd-sunset-*.
