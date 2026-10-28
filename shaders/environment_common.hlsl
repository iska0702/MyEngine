#ifndef ENGINE_ENVIRONMENT_COMMON
#define ENGINE_ENVIRONMENT_COMMON
// Surface lighting excludes the solar disk; direct sun is evaluated separately.
float3 EnvironmentSky(float3 dir,float3 lightDir,float3 lightColor,bool disk){
    float intensity=dot(lightColor,0.333333f.xxx);
    if(intensity<=.0001f)return 0;
    float elevation=-lightDir.y;
    float night=max(saturate((.15f-intensity)*8),saturate(-elevation*5));
    float daylight=(1-night)*saturate(elevation*5+.1f);
    float sunset=saturate(1-smoothstep(-.05f,.35f,elevation))*daylight;
    float t=pow(saturate(dir.y),.6f);
    float3 sky=lerp(float3(.35,.45,.60),float3(.12,.22,.42),t)*(daylight-sunset);
    sky+=lerp(float3(.65,.32,.12),float3(.15,.12,.25),t)*sunset;
    sky+=lerp(float3(.012,.018,.035),float3(.006,.010,.020),t)*(night*2);
    if(disk){float facing=dot(dir,normalize(-lightDir));sky+=8*smoothstep(.999f,.9995f,facing)*daylight+3*smoothstep(.9982f,.9992f,facing)*night;}
    return max(sky*lightColor,0);
}
float3 DiffuseEnvironment(float3 normal,float3 diffuseAlbedo,float3 lightDir,float3 lightColor){
    float3 sky=EnvironmentSky(float3(0,1,0),lightDir,lightColor,false);
    float3 ground=EnvironmentSky(float3(0,-1,0),lightDir,lightColor,false)*.35f;
    return diffuseAlbedo*lerp(ground,sky,saturate(normal.y*.5f+.5f))*.45f;
}
#endif
