-- demo_game.lua
-- Example game script for MyEngine
-- Callbacks: OnStart(), OnUpdate(dt), OnEvent(name, data)

local timer = 0.0
local count = 0

function OnStart()
    engine.log("Demo script started!")
    engine.log("Engine time: " .. tostring(engine.get_time()))
end

function OnUpdate(dt)
    timer = timer + dt
    if timer >= 2.0 then
        timer = 0.0
        count = count + 1
        engine.log("Tick #" .. count .. " (every 2 seconds)")
    end
end

function OnEvent(name, data)
    engine.log("Event received: " .. name .. " | data: " .. data)
    if name == "player_shoot" then
        engine.log("Pew! Player shot something!")
    end
end
