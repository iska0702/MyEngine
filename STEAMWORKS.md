# Steamworks SDK 1.65

Windows Editor and Release builds use `ThirdParty/Valve/steamworks_sdk_165/sdk` by default.

```powershell
./build.bat editor steam
./build.bat release steam
```

Headers: `sdk/public/steam`; import library and runtime: `sdk/redistributable_bin/win64`.
Only the runtime DLL is copied into each build at `ThirdParty/Steamworks/steam_api64.dll`.
The editor console records initialization and Steam connection errors.

The current development App ID is 480 (Spacewar); use your own Steam App ID for distribution. A successful Steam connection requires the Steam client and a signed-in account. If initialization fails, the existing offline fallback remains available.

Explicit builds without Steam:

```powershell
./build.bat editor standalone
./build.bat release standalone
```

`MYENGINE_ENABLE_STEAM=0` disables Steam in default builds. IncrementalBuild.ps1 forwards to a full build with the selected platform. Compiler object caching is disabled; compiler .obj files are deleted after compilation.
