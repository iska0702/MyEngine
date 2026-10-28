# create_initial_save.ps1
# Creates rich compressed & encrypted Save1.save in C:\Users\iskan\Documents\Idontknow\STEAMUSER\

$steamUser = "C:\Users\iskan\Documents\Idontknow\STEAMUSER"
if (!(Test-Path $steamUser)) {
    New-Item -ItemType Directory -Force -Path $steamUser | Out-Null
}

$sceneData = @"
LIGHT_DIR -0.5 -0.8 -0.3
LIGHT_COLOR 1.2 1.2 1.1
SKY_COLOR 0.35 0.65 0.95
MODEL Floor
INDEX_COUNT 6
START_INDEX 0
BASE_VERTEX 0
VISIBLE 1
COLLISION 1
ALBEDO_INDEX 0
NORMAL_INDEX 0
RMA_INDEX 0
POS 0 0 0
ROT 0 0 0
SCALE 25 1 25
PRESET 0
MAT_ALBEDO 0.6 0.65 0.7
MAT_ROUGHNESS 0.7
MAT_METALLIC 0.1
MAT_REFLECTANCE 0.04
MAT_EMISSION 0
MAT_IOR 1.5
UV_TILING 5 5
PHYSICS_BODY 0
PHYSICS_SHAPE 1
PHYSICS_MASS 0
PHYSICS_FRICTION 0.6
PHYSICS_RESTITUTION 0.1
PHYSICS_GRAVITY 0
PHYSICS_CCD 0
MODEL House Walls
INDEX_COUNT 24
START_INDEX 6
BASE_VERTEX 4
VISIBLE 1
COLLISION 1
ALBEDO_INDEX 0
NORMAL_INDEX 0
RMA_INDEX 0
POS 0 0 8
ROT 0 0 0
SCALE 1 1 1
PRESET 0
MAT_ALBEDO 0.88 0.85 0.80
MAT_ROUGHNESS 0.65
MAT_METALLIC 0.0
MAT_REFLECTANCE 0.04
MAT_EMISSION 0
MAT_IOR 1.5
UV_TILING 1 1
PHYSICS_BODY 0
PHYSICS_SHAPE 1
PHYSICS_MASS 0
PHYSICS_FRICTION 0.6
PHYSICS_RESTITUTION 0.1
PHYSICS_GRAVITY 0
PHYSICS_CCD 0
MODEL House Roof
INDEX_COUNT 12
START_INDEX 30
BASE_VERTEX 20
VISIBLE 1
COLLISION 1
ALBEDO_INDEX 0
NORMAL_INDEX 0
RMA_INDEX 0
POS 0 0 8
ROT 0 0 0
SCALE 1 1 1
PRESET 0
MAT_ALBEDO 0.75 0.28 0.18
MAT_ROUGHNESS 0.50
MAT_METALLIC 0.0
MAT_REFLECTANCE 0.04
MAT_EMISSION 0
MAT_IOR 1.5
UV_TILING 1 1
PHYSICS_BODY 0
PHYSICS_SHAPE 1
PHYSICS_MASS 0
PHYSICS_FRICTION 0.6
PHYSICS_RESTITUTION 0.1
PHYSICS_GRAVITY 0
PHYSICS_CCD 0
RAIN_ENABLED 0
RAIN_INTENSITY 1
RAIN_PARTICLES 10000
RAIN_AREA 50
RAIN_DROP_SPEED 25
RAIN_WIND_ENABLED 1
RAIN_WIND_STRENGTH 5
RAIN_WIND_DIR 1 0 0
RAIN_SURFACE_INTERACTION 1
RAIN_WETNESS_ENABLED 1
RAIN_WETNESS_STRENGTH 0.5
RAIN_PUDDLES_ENABLED 1
RAIN_PUDDLES_ACCUM 0.3
RAIN_SPLASH_ENABLED 1
RAIN_SPLASH_INTENSITY 1
RAIN_TRAILS_ENABLED 1
RAIN_TRAIL_STRENGTH 1
RAIN_CAMERA_DROPS 1
RAIN_CAMERA_INTENSITY 0.5
RAIN_QUALITY 2
"@

$imguiSettings = @"
[Window][Debug##Default]
Pos=20,20
Size=420,500
Collapsed=0

[Window][🛡 Steam & DRM Protection Studio (SDK 1.65)]
Pos=460,20
Size=540,500
Collapsed=0

[Window][Physics Studio (PhysX 3.4)]
Pos=20,540
Size=420,400
Collapsed=0
"@

# SAVE v2: Deflate + AES-256-GCM, compatible with SaveManager.
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'Security/ContentCrypto.ps1')
$key=Get-ContentEncryptionKey
$rawBytes=[Text.Encoding]::UTF8.GetBytes("===SCENE_START===`n"+$sceneData+"`n===SCENE_END===`n===IMGUI_START===`n"+$imguiSettings+"`n===IMGUI_END===`n")
$stream=[IO.MemoryStream]::new()
$deflate=[IO.Compression.DeflateStream]::new($stream,[IO.Compression.CompressionLevel]::Optimal,$true)
$deflate.Write($rawBytes,0,$rawBytes.Length);$deflate.Dispose();$compressed=$stream.ToArray();$stream.Dispose()
$record=Encrypt-Data $compressed $key ("save/2/"+$rawBytes.Length+"/"+$compressed.Length)
$savePath=Join-Path $steamUser 'Save1.save'
if(Test-Path -LiteralPath $savePath){throw 'Save already exists; refusing to overwrite it'}
$writer=[IO.BinaryWriter]::new([IO.File]::Open($savePath,[IO.FileMode]::CreateNew))
try{$writer.Write([uint32]0x45564153);$writer.Write([uint32]2);$writer.Write($record.IV);$writer.Write([uint32]$rawBytes.Length);$writer.Write([uint32]$compressed.Length);$writer.Write([uint32]$record.Cipher.Length);$writer.Write($record.Cipher)}finally{$writer.Dispose()}
Write-Host "Created AES-256-GCM save: $savePath"
