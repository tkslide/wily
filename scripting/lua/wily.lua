-- wily.lua — Lua 5.3+ client library for Wily's libmsg / RPC protocol

local unix = require("wily_unix")

local wily = {}

wily.Mtype = {
    -- Events (also bitmasks)
    WEexec      = 1,
    WEgoto      = 2,
    WEdestroy   = 4,
    WEreplace   = 8,
    WEfencepost = 9,

    -- Requests and Responses
    WRerror     = 10,
    WMlist      = 11,
    WRlist      = 12,
    WMnew       = 13,
    WRnew       = 14,
    WMattach    = 15,
    WRattach    = 16,
    WMsetname   = 17,
    WRsetname   = 18,
    WMgetname   = 19,
    WRgetname   = 20,
    WMsettools  = 21,
    WRsettools  = 22,
    WMgettools  = 23,
    WRgettools  = 24,
    WMread      = 25,
    WRread      = 26,
    WMreplace   = 27,
    WRreplace   = 28,
    WMexec      = 29,
    WRexec      = 30,
    WMgoto      = 31,
    WRgoto      = 32,
    WMfencepost = 33,
}

local Mtype = wily.Mtype
local COOKIE = 0xFEED
local HEADER_FMT = ">I2I2I4I2I2I4I4I2" -- cookie(2), t(2), len(4), m(2), w(2), p0(4), p1(4), flag(2)
local HEADERSIZE = string.packsize(HEADER_FMT) -- 22 bytes

function wily.get_fifo()
    local env_fifo = os.getenv("WILYFIFO")
    if env_fifo and env_fifo ~= "" then
        return env_fifo
    end
    local user = os.getenv("USER")
    if not user or user == "" then
        local p = assert(io.popen("id -un"))
        user = p:read("*l")
        p:close()
    end
    local disp = os.getenv("DISPLAY")
    if not disp or disp == "" then
        error("$DISPLAY not set and $WILYFIFO not set")
    end
    local tmpdir = os.getenv("TMPDIR")
    if not tmpdir or tmpdir == "" then
        tmpdir = "/tmp"
    end
    return string.format("%s/wily%s%s", tmpdir, user, disp)
end

local Client = {}
Client.__index = Client

function wily.connect(fifo_path)
    fifo_path = fifo_path or wily.get_fifo()
    local fd, err = unix.dial(fifo_path)
    if not fd then
        error("Failed to connect to Wily FIFO (" .. fifo_path .. "): " .. tostring(err))
    end
    return setmetatable({
        fifo_path = fifo_path,
        fd        = fd,
        next_id   = 1,
        events    = {},
        ev_head   = 1,
        ev_tail   = 0,
    }, Client)
end

function Client:close()
    if self.fd and self.fd >= 0 then
        unix.close(self.fd)
        self.fd = -1
    end
end

-- Lua 5.4 `<close>` support (e.g., `local c <close> = wily.connect()`)
Client.__close = Client.close

function Client:_alloc_id()
    local mid = self.next_id
    self.next_id = (self.next_id % 0xFFFF) + 1
    return mid
end

function Client:_pack_msg(msg)
    local s = msg.s or ""
    local payload = s .. "\0"
    local total_len = HEADERSIZE + #payload
    return string.pack(
        HEADER_FMT,
        COOKIE,
        msg.t,
        total_len,
        msg.m or 0,
        msg.w or 0,
        msg.p0 or 0,
        msg.p1 or 0,
        msg.flag or 0
    ) .. payload
end

function Client:_recv_raw_msg()
    if not self.fd or self.fd < 0 then
        error("Not connected to Wily")
    end
    local hdr, err = unix.read_exact(self.fd, HEADERSIZE)
    if not hdr then
        self:close()
        error(err)
    end
    local cookie, mtype, total_len, msg_id, win_id, p0, p1, flag =
        string.unpack(HEADER_FMT, hdr)

    if cookie ~= COOKIE or total_len < HEADERSIZE then
        error(string.format("Corrupt Wily packet: cookie=0x%x, len=%d", cookie, total_len))
    end

    local raw_text, berr = unix.read_exact(self.fd, total_len - HEADERSIZE)
    if not raw_text then
        self:close()
        error(berr)
    end
    local text = (#raw_text > 1) and raw_text:sub(1, -2) or ""
    return {
        t    = mtype,
        m    = msg_id,
        w    = win_id,
        p0   = p0,
        p1   = p1,
        flag = flag,
        s    = text,
    }
end

function Client:_rpc(req_type, expected_reply, opts)
    if not self.fd or self.fd < 0 then
        error("Not connected to Wily")
    end
    opts = opts or {}
    local mid = self:_alloc_id()
    local pkt = self:_pack_msg({
        t    = req_type,
        m    = mid,
        w    = opts.w or 0,
        p0   = opts.p0 or 0,
        p1   = opts.p1 or 0,
        flag = opts.flag or 0,
        s    = opts.s or "",
    })
    assert(unix.write_all(self.fd, pkt))

    while true do
        local msg = self:_recv_raw_msg()
        if msg.t < Mtype.WEfencepost then
            self.ev_tail = self.ev_tail + 1
            self.events[self.ev_tail] = msg
        else
            if msg.m ~= mid then
                error(string.format("Unexpected message id %d (expected %d)", msg.m, mid))
            end
            if msg.t == Mtype.WRerror then
                error("WilyError: " .. msg.s)
            end
            if msg.t ~= expected_reply then
                error(string.format("Unexpected reply type %d (expected %d)", msg.t, expected_reply))
            end
            return msg
        end
    end
end

-- --- High-Level RPC Methods ---

function Client:list_windows()
    local reply = self:_rpc(Mtype.WMlist, Mtype.WRlist)
    local windows = {}
    for line in reply.s:gmatch("[^\r\n]+") do
        local name, wid = line:match("^(.-)%s+(%d+)%s*$")
        if wid then
            windows[#windows + 1] = { id = tonumber(wid), name = name }
        end
    end
    return windows
end

function Client:new(name, backup)
    local reply = self:_rpc(Mtype.WMnew, Mtype.WRnew, {
        flag = backup and 1 or 0,
        s    = name,
    })
    return reply.w
end

function Client:attach(win_id, mask)
    self:_rpc(Mtype.WMattach, Mtype.WRattach, { w = win_id, flag = mask })
end

function Client:setname(win_id, name)
    self:_rpc(Mtype.WMsetname, Mtype.WRsetname, { w = win_id, s = name })
end

function Client:getname(win_id)
    return self:_rpc(Mtype.WMgetname, Mtype.WRgetname, { w = win_id }).s
end

function Client:settools(win_id, tools)
    self:_rpc(Mtype.WMsettools, Mtype.WRsettools, { w = win_id, s = tools })
end

function Client:gettools(win_id)
    return self:_rpc(Mtype.WMgettools, Mtype.WRgettools, { w = win_id }).s
end

function Client:read(win_id, p0, p1)
    return self:_rpc(Mtype.WMread, Mtype.WRread, { w = win_id, p0 = p0, p1 = p1 }).s
end

function Client:replace(win_id, p0, p1, text)
    self:_rpc(Mtype.WMreplace, Mtype.WRreplace, { w = win_id, p0 = p0, p1 = p1, s = text })
end

function Client:exec_cmd(win_id, cmd)
    self:_rpc(Mtype.WMexec, Mtype.WRexec, { w = win_id, s = cmd })
end

function Client:goto_addr(win_id, target, set_dot)
    if set_dot == nil then set_dot = true end
    local reply = self:_rpc(Mtype.WMgoto, Mtype.WRgoto, {
        w    = win_id,
        flag = set_dot and 1 or 0,
        s    = target,
    })
    return reply.w, reply.p0, reply.p1
end
Client["goto"] = Client.goto_addr

-- --- Event Queue & Bouncing ---

function Client:would_block()
    if self.ev_head <= self.ev_tail then
        return false
    end
    if not self.fd or self.fd < 0 then
        return true
    end
    return not unix.poll(self.fd, 0)
end

function Client:next_event()
    if self.ev_head <= self.ev_tail then
        local ev = self.events[self.ev_head]
        self.events[self.ev_head] = nil
        self.ev_head = self.ev_head + 1
        return ev
    end
    while true do
        local msg = self:_recv_raw_msg()
        if msg.t < Mtype.WEfencepost then
            return msg
        end
    end
end

function Client:bounce(ev)
    if ev.t == Mtype.WEexec then
        self:_rpc(Mtype.WMexec, Mtype.WRexec, {
            w = ev.w, p0 = ev.p0, p1 = ev.p1, flag = ev.flag, s = ev.s
        })
    elseif ev.t == Mtype.WEgoto then
        self:_rpc(Mtype.WMgoto, Mtype.WRgoto, {
            w = ev.w, p0 = ev.p0, p1 = ev.p1, flag = ev.flag, s = ev.s
        })
    end
end

return wily
