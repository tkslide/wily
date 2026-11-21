#!/usr/bin/env lua

local wily = require("wily")
local Mtype = wily.Mtype

local client <close> = wily.connect()

print("Existing windows:")
for _, win in ipairs(client:list_windows()) do
    print(string.format("  [%d] %s", win.id, win.name))
end

local wid = client:new("+LuaConsole", false)
client:settools(wid, " Hello Quit ")
client:replace(wid, 0, 0, "Middle-click (B2) 'Hello' or 'Quit' in the tag.\n")
client:attach(wid, Mtype.WEexec | Mtype.WEgoto | Mtype.WEdestroy)

while true do
    local ev = client:next_event()
    if ev.t == Mtype.WEdestroy then
        print("Window closed, exiting.")
        break
    elseif ev.t == Mtype.WEexec then
        local cmd = ev.s:match("^%s*(.-)%s*$")
        if cmd == "Hello" then
            client:replace(wid, 0, 0, "Clicked Hello!\n")
        elseif cmd == "Quit" then
            client:exec_cmd(wid, "Del")
            break
        else
            client:bounce(ev)
        end
    elseif ev.t == Mtype.WEgoto then
        client:bounce(ev)
    end
end
