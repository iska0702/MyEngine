# Idontknow Engine

Experimental game engine and editor built primarily in C++ with DirectX 12.

## Windows build

The Windows build requires Visual Studio with C++ tools, a Windows SDK, and the third-party SDKs expected by `build.bat` under `ThirdParty`. The repository keeps `ThirdParty/EngineDependencies` and `ThirdParty/miniz`; the other SDKs, their binaries, and local build tools are not included and must be installed separately. Paths to Visual Studio and the Windows SDK in `build.bat` may need to be adjusted for your machine.

To build the standalone editor from a PowerShell prompt at the repository root:

```powershell
$env:IDK_PROJECT_CONTENT = (Resolve-Path '.\Projects\Test\Content').Path
.\build.bat editor standalone
```

The output is `EditorBuild/Idontknow.exe`. The default template content is not currently present, so this command uses the included Test project content. Platform builds and SDK features require their corresponding dependencies; see the project documentation and build scripts.

Local encryption keys, generated key headers, build outputs, project saves, diagnostics and recovered backups are excluded from Git. Do not publish credentials or private game content.
