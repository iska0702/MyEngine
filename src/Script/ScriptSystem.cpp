#include "ScriptSystem.h"
#include "Storage/ContentFile.h"

// Lua 5.4.7 headers (source compiled separately as lua54_embed.c in build.bat)
extern "C" {
#include "../../ThirdParty/EngineDependencies/lua54/lua-5.4.7/src/lua.h"
#include "../../ThirdParty/EngineDependencies/lua54/lua-5.4.7/src/lualib.h"
#include "../../ThirdParty/EngineDependencies/lua54/lua-5.4.7/src/lauxlib.h"
}

#include <iostream>
#include <fstream>
#include <sstream>
#include "AI/NavigationSystem.h"
#include "VFX/ParticleSystem.h"
#include "Terrain/TerrainSystem.h"
#include "PostProcess/PostProcessSystem.h"
#include "Prefab/PrefabSystem.h"
#include "Asset/AssetRegistry.h"
#include "Debug/Profiler.h"
#include "Debug/DebugConsole.h"
#include "Input/InputMapping.h"

#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
#include <imgui.h>
#endif

namespace EngineScript {

// ------------------------------------------------------------------
// Global state for Lua callbacks (used in C function pointers)
// ------------------------------------------------------------------
static ScriptSystem* g_instance = nullptr;

// ------------------------------------------------------------------
// Singleton
// ------------------------------------------------------------------
ScriptSystem& ScriptSystem::Get() {
    static ScriptSystem instance;
    g_instance = &instance;
    return instance;
}

ScriptSystem::~ScriptSystem() {
    Shutdown();
}

// ------------------------------------------------------------------
// C API helpers exposed to Lua via 'engine' table
// ------------------------------------------------------------------

// engine.log(msg)
static int lua_engine_log(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    if (g_instance) {
        g_instance->LogConsole(std::string("[LUA] ") + msg);
    }
    return 0;
}

// engine.get_time() -> number
static int lua_engine_get_time(lua_State* L) {
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    double seconds = std::chrono::duration<double>(now).count();
    lua_pushnumber(L, seconds);
    return 1;
}

// engine.call_cpp(name, arg) -> string result
static int lua_engine_call_cpp(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const char* arg  = luaL_optstring(L, 2, "");
    std::string result;
    if (g_instance) {
        result = g_instance->InvokeCallback(name, arg ? arg : "");
    }
    lua_pushstring(L, result.c_str());
    return 1;
}

// engine.play_sound(path, volume)
static int lua_engine_play_sound(lua_State* L) {
    // Stub - integrates with AudioSystem at runtime
    (void)L;
    return 0;
}

// engine.set_node_pos(nodeId, x, y, z)
static int lua_engine_set_node_pos(lua_State* L) {
    // Stub - integrates with SceneGraph at runtime
    (void)L;
    return 0;
}

// engine.get_node_pos(nodeId) -> x, y, z
static int lua_engine_get_node_pos(lua_State* L) {
    (void)L;
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    lua_pushnumber(L, 0.0);
    return 3;
}

// engine.spawn_agent(name, x, y, z, state) -> agentId
static int lua_engine_spawn_agent(lua_State* L) {
    const char* name = luaL_optstring(L, 1, "Lua_Agent");
    float x = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    int stateIdx = static_cast<int>(luaL_optinteger(L, 5, 1)); // Default Patrol
    EngineAI::AgentState state = static_cast<EngineAI::AgentState>(std::clamp(stateIdx, 0, 4));

    auto* agent = EngineAI::NavigationSystem::Get().SpawnAgent(DirectX::XMFLOAT3(x, y, z), state, name);
    lua_pushinteger(L, agent ? agent->GetId() : 0);
    return 1;
}

// engine.set_agent_dest(agentId, x, y, z) -> bool
static int lua_engine_set_agent_dest(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    float x = static_cast<float>(luaL_checknumber(L, 2));
    float y = static_cast<float>(luaL_checknumber(L, 3));
    float z = static_cast<float>(luaL_checknumber(L, 4));

    auto* agent = EngineAI::NavigationSystem::Get().GetAgent(id);
    bool ok = false;
    if (agent) {
        ok = agent->SetDestination(DirectX::XMFLOAT3(x, y, z), EngineAI::NavigationSystem::Get().GetNavMesh());
    }
    lua_pushboolean(L, ok ? 1 : 0);
    return 1;
}

// engine.get_agent_pos(agentId) -> x, y, z
static int lua_engine_get_agent_pos(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    auto* agent = EngineAI::NavigationSystem::Get().GetAgent(id);
    if (agent) {
        const auto& pos = agent->GetPosition();
        lua_pushnumber(L, pos.x);
        lua_pushnumber(L, pos.y);
        lua_pushnumber(L, pos.z);
    } else {
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
        lua_pushnumber(L, 0.0);
    }
    return 3;
}

// engine.get_agent_count() -> integer
static int lua_engine_get_agent_count(lua_State* L) {
    lua_pushinteger(L, EngineAI::NavigationSystem::Get().GetAgents().size());
    return 1;
}

// engine.spawn_explosion(x, y, z, scale)
static int lua_engine_spawn_explosion(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    float scale = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    EngineVFX::ParticleSystem::Get().SpawnExplosion(DirectX::XMFLOAT3(x, y, z), scale);
    return 0;
}

// engine.spawn_fire(x, y, z, intensity)
static int lua_engine_spawn_fire(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    float intensity = static_cast<float>(luaL_optnumber(L, 4, 1.0));
    EngineVFX::ParticleSystem::Get().SpawnFire(DirectX::XMFLOAT3(x, y, z), intensity);
    return 0;
}

// engine.spawn_sparks(x, y, z, dx, dy, dz, count)
static int lua_engine_spawn_sparks(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    float dx = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    float dy = static_cast<float>(luaL_optnumber(L, 5, 1.0));
    float dz = static_cast<float>(luaL_optnumber(L, 6, 0.0));
    int count = static_cast<int>(luaL_optinteger(L, 7, 40));
    EngineVFX::ParticleSystem::Get().SpawnSparks(DirectX::XMFLOAT3(x, y, z), DirectX::XMFLOAT3(dx, dy, dz), count);
    return 0;
}

// engine.spawn_smoke(x, y, z, duration)
static int lua_engine_spawn_smoke(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    float duration = static_cast<float>(luaL_optnumber(L, 4, 6.0));
    EngineVFX::ParticleSystem::Get().SpawnSmoke(DirectX::XMFLOAT3(x, y, z), duration);
    return 0;
}

// engine.spawn_magic(x, y, z, r, g, b, a)
static int lua_engine_spawn_magic(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float y = static_cast<float>(luaL_checknumber(L, 2));
    float z = static_cast<float>(luaL_checknumber(L, 3));
    float r = static_cast<float>(luaL_optnumber(L, 4, 0.3));
    float g = static_cast<float>(luaL_optnumber(L, 5, 0.8));
    float b = static_cast<float>(luaL_optnumber(L, 6, 1.0));
    float a = static_cast<float>(luaL_optnumber(L, 7, 1.0));
    EngineVFX::ParticleSystem::Get().SpawnMagicEnergy(DirectX::XMFLOAT3(x, y, z), DirectX::XMFLOAT4(r, g, b, a));
    return 0;
}

// engine.get_terrain_height(x, z) -> number
static int lua_engine_get_terrain_height(lua_State* L) {
    float x = static_cast<float>(luaL_checknumber(L, 1));
    float z = static_cast<float>(luaL_checknumber(L, 2));
    float h = EngineTerrain::TerrainSystem::Get().GetHeightAt(x, z);
    lua_pushnumber(L, h);
    return 1;
}

// engine.set_exposure(val)
static int lua_engine_set_exposure(lua_State* L) {
    float exp = static_cast<float>(luaL_checknumber(L, 1));
    EnginePostProcess::PostProcessSystem::Get().GetGlobalSettings().exposure = exp;
    return 0;
}

// engine.set_postprocess_preset(name)
static int lua_engine_set_postprocess_preset(lua_State* L) {
    const char* preset = luaL_checkstring(L, 1);
    if (!preset) return 0;
    std::string p(preset);
    if (p == "cinematic" || p == "Cinematic") {
        EnginePostProcess::PostProcessSystem::Get().ApplyPresetCinematic();
    } else if (p == "horror" || p == "Horror") {
        EnginePostProcess::PostProcessSystem::Get().ApplyPresetHorror();
    } else if (p == "cyberpunk" || p == "Cyberpunk") {
        EnginePostProcess::PostProcessSystem::Get().ApplyPresetCyberpunk();
    } else if (p == "retro" || p == "Retro" || p == "vhs" || p == "VHS") {
        EnginePostProcess::PostProcessSystem::Get().ApplyPresetRetroVHS();
    } else if (p == "neutral" || p == "Neutral") {
        EnginePostProcess::PostProcessSystem::Get().ApplyPresetCleanNeutral();
    }
    return 0;
}

// engine.instantiate_prefab(name, x, y, z) -> nodeId
static int lua_engine_instantiate_prefab(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    float x = static_cast<float>(luaL_optnumber(L, 2, 0.0));
    float y = static_cast<float>(luaL_optnumber(L, 3, 0.0));
    float z = static_cast<float>(luaL_optnumber(L, 4, 0.0));
    auto node = EnginePrefab::PrefabSystem::Get().Instantiate(name, nullptr, DirectX::XMFLOAT3(x, y, z));
    lua_pushinteger(L, node ? node->GetId() : 0);
    return 1;
}

// engine.save_prefab(nodeId, name) -> bool
static int lua_engine_save_prefab(lua_State* L) {
    uint32_t id = static_cast<uint32_t>(luaL_checkinteger(L, 1));
    const char* name = luaL_checkstring(L, 2);
    auto node = EngineScene::SceneGraph::Get().FindNodeById(id);
    if (node) {
        auto pfb = EnginePrefab::PrefabSystem::Get().CreatePrefabFromNode(node.get(), name);
        lua_pushboolean(L, pfb != nullptr ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// engine.get_asset_guid(path) -> guid
static int lua_engine_get_asset_guid(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);
    auto* asset = EngineAsset::AssetRegistry::Get().FindAssetByPath(path);
    if (asset) {
        lua_pushstring(L, asset->guid.c_str());
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

// engine.rescan_assets() -> count
static int lua_engine_rescan_assets(lua_State* L) {
    EngineAsset::AssetRegistry::Get().ScanDirectory(EngineAsset::AssetRegistry::Get().GetContentRoot());
    lua_pushinteger(L, EngineAsset::AssetRegistry::Get().GetAssetCount());
    return 1;
}

// engine.log_info(msg)
static int lua_engine_log_info(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    EngineDebug::DebugConsole::Get().LogInfo(msg ? msg : "");
    return 0;
}

// engine.log_warning(msg)
static int lua_engine_log_warning(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    EngineDebug::DebugConsole::Get().LogWarning(msg ? msg : "");
    return 0;
}

// engine.log_error(msg)
static int lua_engine_log_error(lua_State* L) {
    const char* msg = luaL_checkstring(L, 1);
    EngineDebug::DebugConsole::Get().LogError(msg ? msg : "");
    return 0;
}

// engine.execute_console(cmd)
static int lua_engine_execute_console(lua_State* L) {
    const char* cmd = luaL_checkstring(L, 1);
    if (cmd) EngineDebug::DebugConsole::Get().Execute(cmd);
    return 0;
}

// engine.get_fps() -> number
static int lua_engine_get_fps(lua_State* L) {
    lua_pushnumber(L, EngineDebug::Profiler::Get().GetCurrentFPS());
    return 1;
}

// engine.get_frametime() -> number
static int lua_engine_get_frametime(lua_State* L) {
    lua_pushnumber(L, EngineDebug::Profiler::Get().GetCurrentFrameTimeMs());
    return 1;
}

// engine.is_action_down(name) -> bool
static int lua_engine_is_action_down(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool down = name ? EngineInput::InputMapping::Get().IsActionDown(name) : false;
    lua_pushboolean(L, down ? 1 : 0);
    return 1;
}

// engine.is_action_pressed(name) -> bool
static int lua_engine_is_action_pressed(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool pressed = name ? EngineInput::InputMapping::Get().IsActionPressed(name) : false;
    lua_pushboolean(L, pressed ? 1 : 0);
    return 1;
}

// engine.is_action_released(name) -> bool
static int lua_engine_is_action_released(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    bool rel = name ? EngineInput::InputMapping::Get().IsActionReleased(name) : false;
    lua_pushboolean(L, rel ? 1 : 0);
    return 1;
}

// engine.get_action_axis(name) -> number
static int lua_engine_get_action_axis(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    float val = name ? EngineInput::InputMapping::Get().GetActionAxis(name) : 0.0f;
    lua_pushnumber(L, val);
    return 1;
}

// engine.get_action_axis_2d(name) -> x, y
static int lua_engine_get_action_axis_2d(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    DirectX::XMFLOAT2 v = name ? EngineInput::InputMapping::Get().GetActionAxis2D(name) : DirectX::XMFLOAT2(0, 0);
    lua_pushnumber(L, v.x);
    lua_pushnumber(L, v.y);
    return 2;
}

// ------------------------------------------------------------------
static const luaL_Reg s_engineLib[] = {
    { "log",                    lua_engine_log                    },
    { "get_time",               lua_engine_get_time               },
    { "call_cpp",               lua_engine_call_cpp               },
    { "play_sound",             lua_engine_play_sound             },
    { "set_node_pos",           lua_engine_set_node_pos           },
    { "get_node_pos",           lua_engine_get_node_pos           },
    { "spawn_agent",            lua_engine_spawn_agent            },
    { "set_agent_dest",         lua_engine_set_agent_dest         },
    { "get_agent_pos",          lua_engine_get_agent_pos          },
    { "get_agent_count",        lua_engine_get_agent_count        },
    { "spawn_explosion",        lua_engine_spawn_explosion        },
    { "spawn_fire",             lua_engine_spawn_fire             },
    { "spawn_sparks",           lua_engine_spawn_sparks           },
    { "spawn_smoke",            lua_engine_spawn_smoke            },
    { "spawn_magic",            lua_engine_spawn_magic            },
    { "get_terrain_height",     lua_engine_get_terrain_height     },
    { "set_exposure",           lua_engine_set_exposure           },
    { "set_postprocess_preset", lua_engine_set_postprocess_preset },
    { "instantiate_prefab",     lua_engine_instantiate_prefab     },
    { "save_prefab",            lua_engine_save_prefab            },
    { "get_asset_guid",         lua_engine_get_asset_guid         },
    { "rescan_assets",          lua_engine_rescan_assets          },
    { "log_info",               lua_engine_log_info               },
    { "log_warning",            lua_engine_log_warning            },
    { "log_error",              lua_engine_log_error              },
    { "execute_console",        lua_engine_execute_console        },
    { "get_fps",                lua_engine_get_fps                },
    { "get_frametime",          lua_engine_get_frametime          },
    { "is_action_down",         lua_engine_is_action_down         },
    { "is_action_pressed",      lua_engine_is_action_pressed      },
    { "is_action_released",     lua_engine_is_action_released     },
    { "get_action_axis",        lua_engine_get_action_axis        },
    { "get_action_axis_2d",     lua_engine_get_action_axis_2d     },
    { nullptr, nullptr }
};

void ScriptSystem::RegisterEngineAPI(lua_State* L) {
    luaL_newlib(L, s_engineLib);
    lua_setglobal(L, "engine");
}

// ------------------------------------------------------------------
// Initialize / Shutdown
// ------------------------------------------------------------------
bool ScriptSystem::Initialize() {
    if (m_initialized) return true;
    m_initialized = true;
    m_consoleLog.push_back("[ScriptSystem] Initialized (Lua 5.4.7 embedded)");
    return true;
}

void ScriptSystem::Shutdown() {
    for (auto& kv : m_scripts) {
        if (kv.second && kv.second->L) {
            lua_close(kv.second->L);
            kv.second->L = nullptr;
        }
    }
    m_scripts.clear();
    m_initialized = false;
}

// ------------------------------------------------------------------
// Update - hot-reload check
// ------------------------------------------------------------------
void ScriptSystem::Update(float dt) {
    if (!m_hotReloadEnabled) return;
    m_hotReloadTimer += dt;
    if (m_hotReloadTimer < m_hotReloadInterval) return;
    m_hotReloadTimer = 0.0f;

    for (auto& kv : m_scripts) {
        auto& ctx = *kv.second;
        if (!ctx.loaded || ctx.filePath.empty()) continue;
        std::error_code ec;
        auto newTime = std::filesystem::last_write_time(ctx.filePath, ec);
        if (!ec && newTime != ctx.lastWriteTime) {
            m_consoleLog.push_back("[HotReload] " + ctx.name + " changed, reloading...");
            ReloadScript(kv.first);
        }
    }
}

// ------------------------------------------------------------------
// File loading
// ------------------------------------------------------------------
bool ScriptSystem::LoadFile(ScriptContext& ctx) {
    if (ctx.L) {
        lua_close(ctx.L);
        ctx.L = nullptr;
    }
    ctx.loaded   = false;
    ctx.hasError = false;
    ctx.lastError.clear();

    ctx.L = luaL_newstate();
    if (!ctx.L) {
        ctx.hasError = true;
        ctx.lastError = "Failed to create Lua state";
        return false;
    }

    luaL_openlibs(ctx.L);
    RegisterEngineAPI(ctx.L);

    // Set script name as global
    lua_pushstring(ctx.L, ctx.name.c_str());
    lua_setglobal(ctx.L, "SCRIPT_NAME");

    std::vector<uint8_t> source;
    int status = LUA_ERRFILE;
    if (EngineStorage::ReadContentBytes(ctx.filePath, source)) {
        status = luaL_loadbuffer(ctx.L, source.empty() ? "" : reinterpret_cast<const char*>(source.data()),
                                 source.size(), ctx.filePath.c_str());
        if (status == LUA_OK) status = lua_pcall(ctx.L, 0, LUA_MULTRET, 0);
    } else lua_pushliteral(ctx.L, "Script file not found on disk or in content archives");
    if (status != LUA_OK) {
        ctx.hasError = true;
        ctx.lastError = lua_tostring(ctx.L, -1);
        lua_pop(ctx.L, 1);
        m_consoleLog.push_back("[ERROR] " + ctx.name + ": " + ctx.lastError);
        return false;
    }

    std::error_code ec;
    ctx.lastWriteTime = std::filesystem::last_write_time(ctx.filePath, ec);
    ctx.loaded = true;
    m_consoleLog.push_back("[ScriptSystem] Loaded: " + ctx.name);
    return true;
}

bool ScriptSystem::SafeCall(lua_State* L, int nargs, int nresults, std::string& outError) {
    int status = lua_pcall(L, nargs, nresults, 0);
    if (status != LUA_OK) {
        outError = lua_tostring(L, -1);
        lua_pop(L, 1);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------
// Script Management
// ------------------------------------------------------------------
uint32_t ScriptSystem::LoadScript(const std::string& filePath) {
    uint32_t id = m_nextId++;
    auto ctx = std::make_unique<ScriptContext>();
    ctx->filePath = filePath;
    ctx->name     = std::filesystem::path(filePath).stem().string();
    LoadFile(*ctx);
    m_scripts[id] = std::move(ctx);
    return id;
}

void ScriptSystem::UnloadScript(uint32_t id) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end()) return;
    if (it->second && it->second->L) {
        lua_close(it->second->L);
        it->second->L = nullptr;
    }
    m_scripts.erase(it);
}

void ScriptSystem::ReloadScript(uint32_t id) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end()) return;
    LoadFile(*it->second);
    CallOnStart(id);
}

void ScriptSystem::ReloadAll() {
    for (auto& kv : m_scripts) ReloadScript(kv.first);
}

// ------------------------------------------------------------------
// Lua Callbacks
// ------------------------------------------------------------------
void ScriptSystem::CallOnStart(uint32_t id) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->loaded) return;
    lua_State* L = it->second->L;
    lua_getglobal(L, "OnStart");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    std::string err;
    if (!SafeCall(L, 0, 0, err))
        m_consoleLog.push_back("[ERROR] OnStart: " + err);
}

void ScriptSystem::CallOnUpdate(uint32_t id, float dt) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->loaded) return;
    lua_State* L = it->second->L;
    lua_getglobal(L, "OnUpdate");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushnumber(L, (lua_Number)dt);
    std::string err;
    if (!SafeCall(L, 1, 0, err))
        m_consoleLog.push_back("[ERROR] OnUpdate: " + err);
}

void ScriptSystem::CallOnEvent(uint32_t id, const std::string& eventName, const std::string& eventData) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->loaded) return;
    lua_State* L = it->second->L;
    lua_getglobal(L, "OnEvent");
    if (!lua_isfunction(L, -1)) { lua_pop(L, 1); return; }
    lua_pushstring(L, eventName.c_str());
    lua_pushstring(L, eventData.c_str());
    std::string err;
    if (!SafeCall(L, 2, 0, err))
        m_consoleLog.push_back("[ERROR] OnEvent: " + err);
}

// ------------------------------------------------------------------
// ExecString - editor console
// ------------------------------------------------------------------
bool ScriptSystem::ExecString(uint32_t id, const std::string& code, std::string& outError) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->L) {
        outError = "Script not loaded";
        return false;
    }
    int status = luaL_dostring(it->second->L, code.c_str());
    if (status != LUA_OK) {
        outError = lua_tostring(it->second->L, -1);
        lua_pop(it->second->L, 1);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------
// Global variable access
// ------------------------------------------------------------------
void ScriptSystem::SetGlobalNumber(uint32_t id, const std::string& name, double value) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->L) return;
    lua_pushnumber(it->second->L, (lua_Number)value);
    lua_setglobal(it->second->L, name.c_str());
}

void ScriptSystem::SetGlobalString(uint32_t id, const std::string& name, const std::string& value) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->L) return;
    lua_pushstring(it->second->L, value.c_str());
    lua_setglobal(it->second->L, name.c_str());
}

double ScriptSystem::GetGlobalNumber(uint32_t id, const std::string& name, double defaultVal) {
    auto it = m_scripts.find(id);
    if (it == m_scripts.end() || !it->second->L) return defaultVal;
    lua_getglobal(it->second->L, name.c_str());
    double val = lua_isnumber(it->second->L, -1) ? (double)lua_tonumber(it->second->L, -1) : defaultVal;
    lua_pop(it->second->L, 1);
    return val;
}

const ScriptContext* ScriptSystem::GetContext(uint32_t id) const {
    auto it = m_scripts.find(id);
    return (it != m_scripts.end()) ? it->second.get() : nullptr;
}

void ScriptSystem::RegisterCallback(const std::string& name, std::function<std::string(const std::string&)> cb) {
    m_callbacks[name] = std::move(cb);
}

// ------------------------------------------------------------------
// ImGui Editor UI
// ------------------------------------------------------------------
void ScriptSystem::DrawUI() {
#if !defined(ENGINE_RELEASE) && !defined(ENGINE_SERVER)
    if (!m_uiOpen) return;

    ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(" Script System", &m_uiOpen)) {
        ImGui::End();
        return;
    }

    // -- Left panel: Script list --
    ImGui::BeginChild("ScriptList", ImVec2(220, 0), true);
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "Loaded Scripts (%d)", (int)m_scripts.size());
    ImGui::Separator();

    for (auto& kv : m_scripts) {
        auto& ctx = *kv.second;
        ImVec4 col = ctx.hasError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) :
                     ctx.loaded   ? ImVec4(0.4f, 1.0f, 0.6f, 1.0f) :
                                    ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        ImGui::PushID((int)kv.first);
        bool selected = (m_selectedScriptId == kv.first);
        ImGui::TextColored(col, "%s %s", ctx.hasError ? "!" : (ctx.loaded ? "+" : "?"), ctx.name.c_str());
        if (ImGui::IsItemClicked()) m_selectedScriptId = kv.first;
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button(" Reload All")) ReloadAll();
    ImGui::EndChild();

    ImGui::SameLine();

    // -- Right panel: Script inspector + console --
    ImGui::BeginChild("ScriptInspector", ImVec2(0, 0), false);

    if (m_selectedScriptId != 0) {
        auto it = m_scripts.find(m_selectedScriptId);
        if (it != m_scripts.end()) {
            auto& ctx = *it->second;
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.4f, 1.0f), "Script: %s", ctx.name.c_str());
            ImGui::TextUnformatted(ctx.filePath.c_str());
            ImGui::Separator();

            if (ctx.hasError) {
                ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error:");
                ImGui::TextWrapped("%s", ctx.lastError.c_str());
                ImGui::Separator();
            }

            if (ImGui::Button(" OnStart"))         CallOnStart(m_selectedScriptId);
            ImGui::SameLine();
            if (ImGui::Button(" Reload Script"))   ReloadScript(m_selectedScriptId);
            ImGui::SameLine();
            bool hr = m_hotReloadEnabled;
            if (ImGui::Checkbox("Hot-Reload", &hr)) EnableHotReload(hr);
        }
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.5f, 0.9f, 1.0f, 1.0f), "Lua Console");

    // Console log
    ImGui::BeginChild("ConsoleLog", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4), true);
    for (const auto& line : m_consoleLog) {
        ImVec4 col = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        if (line.find("[ERROR]") != std::string::npos) col = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
        else if (line.find("[LUA]")  != std::string::npos) col = ImVec4(0.7f, 1.0f, 0.7f, 1.0f);
        else if (line.find("[HotReload]") != std::string::npos) col = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
        ImGui::TextColored(col, "%s", line.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Input
    ImGui::SetNextItemWidth(-80.0f);
    bool exec = ImGui::InputText("##console", m_consoleInput, sizeof(m_consoleInput),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    exec = ImGui::Button("Exec") || exec;
    if (exec && m_consoleInput[0] != '\0') {
        std::string code = m_consoleInput;
        m_consoleLog.push_back("> " + code);
        std::string err;
        uint32_t targetId = m_selectedScriptId;
        if (targetId == 0 && !m_scripts.empty())
            targetId = m_scripts.begin()->first;

        if (targetId != 0) {
            if (!ExecString(targetId, code, err))
                m_consoleLog.push_back("[ERROR] " + err);
        } else {
            m_consoleLog.push_back("[ERROR] No script selected for console");
        }
        m_consoleInput[0] = '\0';
    }

    ImGui::EndChild();
    ImGui::End();
#endif
}

} // namespace EngineScript
