-- EDMAC DMA Test v4.5 for Canon M50 (DIGIC 8)
-- Uses Canon's Layer 2 MemoryToMemoryEsub5 wrappers
--
-- FIXES from v4.4 (still 0/64):
--   1. ADDRESS ORDER SWAPPED! addr_struct[0] goes to write_ch (SOURCE),
--      addr_struct[1] goes to read_ch (SINK). We had DST/SRC, need SRC/DST.
--   2. Comprehensive crossbar diagnostics: read BoomerSelector ports 0x66/0xF2
--   3. Check MMIO+0x10, +0x14, +0x2C, global 0xC1100730
--   4. Post-transfer: check BOTH SRC and DST buffers
--   5. Try BoomerSelector_write for write channel port 0xF2 (optional FIX #3)
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA45.LOG

local LOG_FILE = "ML/LOGS/DMA45.LOG"
local log_lines = {}

local function log(msg)
    if msg == nil then msg = "(nil)" end
    log_lines[#log_lines + 1] = msg
    pcall(print, msg)
end

local function logf(fmt, ...)
    log(string.format(fmt, ...))
end

local function flush_log()
    pcall(function()
        local f = io.open(LOG_FILE, "w")
        if f then
            for _, line in ipairs(log_lines) do
                f:write(line .. "\n")
            end
            f:close()
        end
    end)
end

local function peek(addr)
    return dryos.peek(addr)
end

local function poke(addr, val)
    dryos.poke(addr, val)
end

local function fw_call(addr, ...)
    return dryos.fw_call(addr, ...)
end

local function mmio_read(addr)
    return dryos.mmio_read(addr)
end

-----------------------------------------------------------------------
-- Canon ROM function addresses
-----------------------------------------------------------------------
local L2 = {
    InitMem2MemModule  = 0xE084DE9C,
    store_struct       = 0xE084DF40,
    reinit_struct      = 0xE084DF4A,
    cleanup_channels   = 0xE084DF26,
    set_addrs          = 0xE084DF70,
    set_geom_mode      = 0xE084DF8A,
    start_connect      = 0xE084DF56,
}

local ELD = {
    set_enable  = 0xE054A4F2,
    irq_enable  = 0xE054A386,
}

local PWR = {
    resource_lock   = 0xE084B380,
    resource_unlock = 0xE084B3AC,
    pwr_wake        = 0xE084B446,
    pwr_sleep       = 0xE084B44E,
}

-- BoomerSelector_write(packed) at 0xE053AC34
-- packed = (port_index << 16) | connection_value
local BOOMER_WRITE = 0xE053AC34

-- BoomerSelector crossbar table in RAM (array of MMIO reg pointers)
local BOOMER_TABLE = 0xE0FC40E0

-- NOP callback (bx lr)
local NOP_CBR = 0xE084DF49

local DARKCURCOR = {
    res_entry  = 0xE0F72638,
    ch_pair    = 0xE0F72640,
}

local WRITE_CH = 0x3D  -- source, reads from memory, direct ARM
local READ_CH  = 0x18  -- sink, writes to memory, crossbar-routed

local M2M_STRUCT = 0x00016F38

local WR_MMIO     = 0xD0487600
local RD_MMIO     = 0xD0487100
local WR_SUB_MMIO = 0xD0487D00
local RD_SUB_MMIO = 0xD0487900

-- BoomerSelector ports for our channels
local RD_PORT = 0x66   -- read_ch 0x18, crossbar port
local WR_PORT = 0xF2   -- write_ch 0x3D, crossbar port
local CONN_ID = 0x0C   -- connection ID used by crossbar_config(0x18, 0x00C2060C)

-- Global DMA register
local GLOBAL_DMA_REG = 0xC1100730

-----------------------------------------------------------------------
-- Memory layout (all in uncacheable RAM: 0x40-0x5F range)
-----------------------------------------------------------------------
local SRC_ADDR    = 0x4F000000
local DST_ADDR    = 0x4F001000
local GEOM_STRUCT = 0x4F002000
local ADDR_PAIR   = 0x4F003010
local GEOM_DESC   = 0x4F003020

local XFER_SIZE   = 256

-----------------------------------------------------------------------
-- Diagnostic functions
-----------------------------------------------------------------------
local function read_boomer_port(port)
    -- BoomerSelector crossbar table: array of MMIO reg pointers
    -- Each port has a 4-byte pointer at BOOMER_TABLE + port*4
    local ok, val = pcall(function()
        local reg_ptr = peek(BOOMER_TABLE + port * 4)
        if reg_ptr == 0 or reg_ptr == 0xFFFFFFFF then
            return string.format("port_ptr=0x%08X (invalid)", reg_ptr)
        end
        local reg_val = mmio_read(reg_ptr)
        return string.format("port_ptr=0x%08X → val=0x%08X", reg_ptr, reg_val)
    end)
    if ok then return val else return "FAILED: " .. tostring(val) end
end

local function dump_mmio_full(label, base, sub_base)
    local ok, err = pcall(function()
        logf("  %s MMIO=0x%08X:", label, base)
        logf("    CTRL=0x%08X  ARM=0x%08X  CMD=0x%08X",
            mmio_read(base + 0x00), mmio_read(base + 0x04), mmio_read(base + 0x08))
        logf("    XBAR_CONN=0x%08X  XBAR_PORT=0x%08X  XFER_CTL=0x%08X",
            mmio_read(base + 0x10), mmio_read(base + 0x14), mmio_read(base + 0x18))
        logf("    EN1=0x%08X  EN2=0x%08X  STATUS=0x%08X  CBR=0x%08X",
            mmio_read(base + 0x20), mmio_read(base + 0x24),
            mmio_read(base + 0x2C), mmio_read(base + 0x3C))
        logf("    G0=0x%08X  G1=0x%08X  G2=0x%08X  G3=0x%08X  G4=0x%08X",
            mmio_read(base + 0x48), mmio_read(base + 0x4C), mmio_read(base + 0x50),
            mmio_read(base + 0x54), mmio_read(base + 0x58))
        logf("    ADDR=0x%08X  CONN_ST=0x%08X  MODE=0x%08X  STAT_EXT=0x%08X",
            mmio_read(base + 0xA0), mmio_read(base + 0xB4), mmio_read(base + 0xC0),
            mmio_read(base + 0xC4))
        if sub_base then
            logf("    SUB=0x%08X: [00]=0x%08X [10]=0x%08X [18]=0x%08X [1C]=0x%08X",
                sub_base,
                mmio_read(sub_base + 0x00), mmio_read(sub_base + 0x10),
                mmio_read(sub_base + 0x18), mmio_read(sub_base + 0x1C))
        end
    end)
    if not ok then
        logf("  %s MMIO FAILED: %s", label, tostring(err))
    end
end

local function dump_m2m_struct()
    logf("  M2M struct: cb=0x%08X ctx=0x%08X wr=0x%08X rd=0x%08X fl=0x%08X",
        peek(M2M_STRUCT + 0), peek(M2M_STRUCT + 4),
        peek(M2M_STRUCT + 8), peek(M2M_STRUCT + 12), peek(M2M_STRUCT + 16))
end

local function dump_boomer()
    logf("  BoomerSelector port 0x%02X (read_ch): %s", RD_PORT, read_boomer_port(RD_PORT))
    logf("  BoomerSelector port 0x%02X (write_ch): %s", WR_PORT, read_boomer_port(WR_PORT))
end

local function dump_global()
    local ok, val = pcall(function() return mmio_read(GLOBAL_DMA_REG) end)
    if ok then
        logf("  Global DMA reg (0x%08X) = 0x%08X", GLOBAL_DMA_REG, val)
    else
        logf("  Global DMA reg read FAILED")
    end
end

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v4.5 ===")
log("v4.4 + FIXED address order + crossbar diagnostics")
logf("SRC=0x%08X  DST=0x%08X  SIZE=%d", SRC_ADDR, DST_ADDR, XFER_SIZE)
logf("WRITE_CH=0x%02X (source, reads mem)  READ_CH=0x%02X (sink, writes mem)",
    WRITE_CH, READ_CH)
log("")

local lock_handle = nil

-----------------------------------------------------------------------
-- Step 1: Fill source, clear destination
-----------------------------------------------------------------------
log("--- Step 1: Fill source, clear dest ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(SRC_ADDR + i*4, 0xDEAD0000 + i)
end
for i = 0, XFER_SIZE/4 - 1 do
    poke(DST_ADDR + i*4, 0)
end
logf("  SRC[0]=0x%08X SRC[1]=0x%08X SRC[63]=0x%08X",
    peek(SRC_ADDR), peek(SRC_ADDR + 4), peek(SRC_ADDR + 252))
logf("  DST[0]=0x%08X DST[1]=0x%08X", peek(DST_ADDR), peek(DST_ADDR + 4))
log("")

-----------------------------------------------------------------------
-- Step 2: Build descriptors — FIXED ADDRESS ORDER!
-- addr_struct[0] → write_ch (0x3D) = SOURCE → SRC_ADDR
-- addr_struct[1] → read_ch  (0x18) = SINK   → DST_ADDR
-----------------------------------------------------------------------
log("--- Step 2: Build descriptors ---")

-- ★ FIXED: addr[0]=SRC (for source ch), addr[1]=DST (for sink ch)
poke(ADDR_PAIR + 0, SRC_ADDR)  -- write_ch source address
poke(ADDR_PAIR + 4, DST_ADDR)  -- read_ch destination address
logf("  Addr pair: [0]=0x%08X→write_ch(src) [1]=0x%08X→read_ch(dst)",
    SRC_ADDR, DST_ADDR)

-- Geometry: 256 bytes, single line
for i = 0, 14 do
    poke(GEOM_STRUCT + i*4, 0)
end
poke(GEOM_STRUCT + 0x1C, XFER_SIZE)
logf("  Geom: xb=%d ya=0 (1 line of %d bytes)", XFER_SIZE, XFER_SIZE)

-- Geom descriptor: {write_geom_ptr, read_geom_ptr, mode}
poke(GEOM_DESC + 0, GEOM_STRUCT)
poke(GEOM_DESC + 4, GEOM_STRUCT)
poke(GEOM_DESC + 8, 1)
logf("  Geom desc: geom_ptr=0x%08X mode=1", GEOM_STRUCT)
log("")

-----------------------------------------------------------------------
-- Step 3: Resource lock + power
-----------------------------------------------------------------------
log("--- Step 3: Resource lock + power ---")
lock_handle = fw_call(PWR.resource_lock, DARKCURCOR.res_entry, 2)
logf("  Lock handle = 0x%08X", lock_handle or 0)
fw_call(PWR.pwr_wake)
log("  Powered.")
log("")

-----------------------------------------------------------------------
-- Step 4: Pre-init state (raw hardware)
-----------------------------------------------------------------------
log("--- Step 4: Pre-init MMIO + crossbar ---")
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
dump_boomer()
dump_global()
log("")

-----------------------------------------------------------------------
-- Step 5: InitMem2MemModule
-----------------------------------------------------------------------
log("--- Step 5: InitMem2MemModule ---")
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
log("  Done.")
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 6: Post-init crossbar state (important!)
-----------------------------------------------------------------------
log("--- Step 6: Post-init crossbar + MMIO ---")
dump_boomer()
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
dump_global()
log("")

-----------------------------------------------------------------------
-- Step 7: Store callback, set addresses, set geometry
-----------------------------------------------------------------------
log("--- Step 7: Config (callback + addrs + geom) ---")
fw_call(L2.store_struct, NOP_CBR, 0)
fw_call(L2.set_addrs, ADDR_PAIR)
fw_call(L2.set_geom_mode, GEOM_DESC)
log("  callback + addrs + geom set.")

-- Verify addresses in MMIO
logf("  Wr ch ADDR(+0xA0)=0x%08X (expect SRC=0x%08X)",
    mmio_read(WR_MMIO + 0xA0), SRC_ADDR)
logf("  Rd ch ADDR(+0xA0)=0x%08X (expect DST=0x%08X)",
    mmio_read(RD_MMIO + 0xA0), DST_ADDR)
log("")

-----------------------------------------------------------------------
-- Step 8: FIX #1 — set_enable on both channels
-----------------------------------------------------------------------
log("--- Step 8: set_enable (both) ---")
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
logf("  Wr EN1=0x%08X EN2=0x%08X subEN1=0x%08X subEN2=0x%08X",
    mmio_read(WR_MMIO + 0x20), mmio_read(WR_MMIO + 0x24),
    mmio_read(WR_SUB_MMIO + 0x18), mmio_read(WR_SUB_MMIO + 0x1C))
logf("  Rd EN1=0x%08X EN2=0x%08X subEN1=0x%08X subEN2=0x%08X",
    mmio_read(RD_MMIO + 0x20), mmio_read(RD_MMIO + 0x24),
    mmio_read(RD_SUB_MMIO + 0x18), mmio_read(RD_SUB_MMIO + 0x1C))
log("")

-----------------------------------------------------------------------
-- Step 9: FIX #2 — irq_enable on read channel
-----------------------------------------------------------------------
log("--- Step 9: irq_enable (read ch) ---")
fw_call(ELD.irq_enable, READ_CH)
log("  Done.")
log("")

-----------------------------------------------------------------------
-- Step 10: Pre-trigger full state
-----------------------------------------------------------------------
log("--- Step 10: Pre-trigger full state ---")
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
dump_boomer()
dump_global()
log("")

-----------------------------------------------------------------------
-- Step 11: TRIGGER DMA!
-----------------------------------------------------------------------
log("--- Step 11: START DMA! ---")
log("  Calling mem2mem_start_connect()...")
fw_call(L2.start_connect)
log("  Returned from start_connect.")
msleep(200)
log("  Waited 200ms.")
log("")

-----------------------------------------------------------------------
-- Step 12: Post-trigger state
-----------------------------------------------------------------------
log("--- Step 12: Post-trigger state ---")
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
dump_boomer()
dump_global()
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 13: Verify BOTH buffers
-----------------------------------------------------------------------
log("--- Step 13: Verification ---")

-- Check DST buffer (should have DEAD pattern if DMA worked correctly)
local dst_match = 0
local dst_first_fail = -1
for i = 0, XFER_SIZE/4 - 1 do
    local expected = 0xDEAD0000 + i
    local got = peek(DST_ADDR + i*4)
    if got == expected then
        dst_match = dst_match + 1
    elseif dst_first_fail < 0 then
        dst_first_fail = i
        logf("  DST first mismatch [%d]: exp=0x%08X got=0x%08X", i, expected, got)
    end
end

-- Check SRC buffer (should still have DEAD pattern; if DMA ran with OLD swapped
-- addresses, this might be zeroed)
local src_changed = 0
for i = 0, XFER_SIZE/4 - 1 do
    local expected = 0xDEAD0000 + i
    local got = peek(SRC_ADDR + i*4)
    if got ~= expected then
        src_changed = src_changed + 1
        if src_changed == 1 then
            logf("  SRC first change [%d]: exp=0x%08X got=0x%08X", i, expected, got)
        end
    end
end

logf("")
logf("  *** DST RESULT: %d/%d words match ***", dst_match, XFER_SIZE/4)
logf("  *** SRC CHANGED: %d/%d words differ ***", src_changed, XFER_SIZE/4)
log("")

-- Sample both buffers
log("  DST sample:")
for i = 0, 7 do
    local v = peek(DST_ADDR + i*4)
    local e = 0xDEAD0000 + i
    local m = (v == e) and "OK" or "FAIL"
    logf("    [%d] 0x%08X (exp 0x%08X) %s", i, v, e, m)
end
log("  SRC sample:")
for i = 0, 3 do
    logf("    [%d] 0x%08X (exp 0x%08X)", i, peek(SRC_ADDR + i*4), 0xDEAD0000 + i)
end
log("")

-----------------------------------------------------------------------
-- Step 14: Cleanup
-----------------------------------------------------------------------
log("--- Step 14: Cleanup ---")
fw_call(L2.reinit_struct)
fw_call(L2.cleanup_channels)
fw_call(PWR.pwr_sleep)
if lock_handle and lock_handle ~= 0 then
    fw_call(PWR.resource_unlock, lock_handle)
    logf("  unlock(0x%08X)", lock_handle)
end
log("  Cleanup done.")
log("")

log("=== Test v4.5 complete ===")
flush_log()
log("Log written to " .. LOG_FILE)
