-- EDMAC DMA Test v4.3 for Canon M50 (DIGIC 8)
-- Uses Canon's Layer 2 MemoryToMemoryEsub5 wrappers
-- Based on DarkCurCor CopyOB reverse-engineered flow
--
-- FIX from v4.2: v4.2 froze because it read MMIO from 0xD0487xxx
-- block before powering it up. This block is NOT powered during idle.
-- DarkCurCor calls efm_resource_lock + efm_pwr_wake FIRST.
--
-- Complete sequence (matching DarkCurCor exactly):
--   0. efm_resource_lock(res_entry, 2)  ← POWER GATE UNLOCK
--   1. efm_pwr_wake()                   ← CLOCK ENABLE
--   2. InitMem2MemModule(ch_pair)
--   3. mem2mem_store_struct(callback, ctx)
--   4. mem2mem_set_addrs(addr_pair)
--   5. mem2mem_set_geom_mode(geom_desc)
--   6. mem2mem_start_connect()          ← TRIGGERS DMA
--   7. msleep(200)                      ← wait for completion
--   8. mem2mem_reinit_struct()
--   9. mem2mem_cleanup_channels()
--  10. efm_pwr_sleep()
--  11. efm_resource_unlock(lock_handle)
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA43.LOG

local LOG_FILE = "ML/LOGS/DMA43.LOG"
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
-- Layer 2 MemoryToMemoryEsub5 API
local L2 = {
    InitMem2MemModule  = 0xE084DE9C, -- (channel_pair_desc_ptr)
    store_struct       = 0xE084DF40, -- (callback, ctx)
    reinit_struct      = 0xE084DF4A, -- ()
    cleanup_channels   = 0xE084DF26, -- ()
    set_addrs          = 0xE084DF70, -- (addr_pair_ptr)
    set_geom_mode      = 0xE084DF8A, -- (geom_mode_desc_ptr)
    start_connect      = 0xE084DF56, -- ()
}

-- Resource/power management (Canon wrappers with asserts)
local PWR = {
    resource_lock   = 0xE084B380, -- (resource_entry, priority) → lock_handle
    resource_unlock = 0xE084B3AC, -- (lock_handle)
    pwr_wake        = 0xE084B446, -- ()
    pwr_sleep       = 0xE084B44E, -- ()
}

-- NOP callback (bx lr) - safe completion callback
local NOP_CBR = 0xE084DF49

-- DarkCurCor ROM config addresses (read-only ROM data)
-- config_base = 0xE0F7267C
local DARKCURCOR = {
    res_entry  = 0xE0F72638, -- config_base - 0x44: resource lock entry
    ch_pair    = 0xE0F72640, -- config_base - 0x3C: {write_ch=0x3D, read_ch=0x18}
    geom_desc  = 0xE0F72650, -- config_base - 0x2C: {wr_geom, rd_geom, mode}
}

-- Mem2Mem global struct
local M2M_STRUCT = 0x00016F38

-- Channel MMIO bases (for diagnostic reads AFTER power-up)
local WR_MMIO = 0xD0487600  -- ch 0x3D
local RD_MMIO = 0xD0487100  -- ch 0x18

-----------------------------------------------------------------------
-- Memory layout (all in uncacheable RAM: 0x40-0x5F range)
-----------------------------------------------------------------------
local SRC_ADDR    = 0x4F000000  -- source data
local DST_ADDR    = 0x4F001000  -- destination
local GEOM_STRUCT = 0x4F002000  -- 60-byte edmac_geom struct
local ADDR_PAIR   = 0x4F003010  -- 8-byte {dst, src}
local GEOM_DESC   = 0x4F003020  -- 12-byte {write_geom, read_geom, mode}

local XFER_SIZE   = 256  -- bytes to transfer (must be even)

-----------------------------------------------------------------------
-- edmac_geom struct layout (from eld_edmac_set_geom disasm):
--   +0x00: off2a    +0x04: off3a    +0x08: off4a
--   +0x0C: off2b    +0x10: off3b    +0x14: off4b
--   +0x18: off5
--   +0x1C: xb (bytes per line, MUST be even)
--   +0x20: xa (stride)   +0x24: xn
--   +0x28: ya (additional lines, 0=1 line)
--   +0x2C: yb   +0x30: yn
--   +0x34: off1a   +0x38: off1b
-- Total: 0x3C = 60 bytes, 15 uint32_t
-----------------------------------------------------------------------

local function dump_mmio_safe(label, base)
    -- Only call AFTER hardware is powered!
    local ok, err = pcall(function()
        logf("  %s MMIO=0x%08X:", label, base)
        logf("    CTRL=0x%08X  ARM=0x%08X  CMD=0x%08X",
            mmio_read(base + 0x00), mmio_read(base + 0x04), mmio_read(base + 0x08))
        logf("    EN1=0x%08X   CBR=0x%08X",
            mmio_read(base + 0x20), mmio_read(base + 0x3C))
        logf("    G0=0x%08X  G1=0x%08X  G2=0x%08X",
            mmio_read(base + 0x48), mmio_read(base + 0x4C), mmio_read(base + 0x50))
        logf("    ADDR=0x%08X  STAT=0x%08X  MODE=0x%08X",
            mmio_read(base + 0xA0), mmio_read(base + 0xB4), mmio_read(base + 0xC0))
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

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v4.3 ===")
log("Layer 2 API + Resource Lock + Power Wake")
logf("SRC=0x%08X  DST=0x%08X  SIZE=%d", SRC_ADDR, DST_ADDR, XFER_SIZE)
log("")

-- Track lock handle for cleanup
local lock_handle = nil
local hardware_powered = false

-----------------------------------------------------------------------
-- Step 1: Fill source, clear destination (safe: uncacheable RAM)
-----------------------------------------------------------------------
log("--- Step 1: Fill source, clear dest ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(SRC_ADDR + i*4, 0xDEAD0000 + i)
end
for i = 0, XFER_SIZE/4 - 1 do
    poke(DST_ADDR + i*4, 0)
end
logf("  SRC[0]=0x%08X SRC[1]=0x%08X", peek(SRC_ADDR), peek(SRC_ADDR + 4))
logf("  DST[0]=0x%08X DST[1]=0x%08X", peek(DST_ADDR), peek(DST_ADDR + 4))
log("")

-----------------------------------------------------------------------
-- Step 2: Build descriptors in uncacheable RAM
-----------------------------------------------------------------------
log("--- Step 2: Build descriptors ---")

-- Address pair: {dst_addr, src_addr}  (dst → write_ch, src → read_ch)
poke(ADDR_PAIR + 0, DST_ADDR)
poke(ADDR_PAIR + 4, SRC_ADDR)
logf("  Addr pair: dst=0x%08X src=0x%08X", DST_ADDR, SRC_ADDR)

-- Geometry struct (60 bytes = 15 words): clear all, then set xb
for i = 0, 14 do
    poke(GEOM_STRUCT + i*4, 0)
end
-- xb = bytes per line (single-line transfer: all data in one line)
poke(GEOM_STRUCT + 0x1C, XFER_SIZE)
-- ya = 0 means 1 line (ya = additional lines beyond first)
logf("  Geom: xb=%d ya=0 (1 line of %d bytes)", XFER_SIZE, XFER_SIZE)

-- Geometry/mode descriptor: {write_geom_ptr, read_geom_ptr, mode}
poke(GEOM_DESC + 0, GEOM_STRUCT)  -- write ch geometry
poke(GEOM_DESC + 4, GEOM_STRUCT)  -- read ch geometry (same struct)
poke(GEOM_DESC + 8, 1)            -- mode = 1 (DarkCurCor uses mode 1)
logf("  Geom desc: ptr=0x%08X mode=1", GEOM_STRUCT)
log("")

-----------------------------------------------------------------------
-- Step 3: Pre-check Mem2Mem struct (safe: normal RAM peek)
-----------------------------------------------------------------------
log("--- Step 3: Pre-check M2M struct ---")
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 4: Resource lock (MUST be before any MMIO access!)
-- DarkCurCor: efm_resource_lock(config_base-0x44, 2)
-----------------------------------------------------------------------
log("--- Step 4: Resource lock ---")
logf("  Calling efm_resource_lock(0x%08X, 2)...", DARKCURCOR.res_entry)
lock_handle = fw_call(PWR.resource_lock, DARKCURCOR.res_entry, 2)
logf("  Lock handle = 0x%08X", lock_handle or 0)
log("")

-----------------------------------------------------------------------
-- Step 5: Power wake (enables clocks to EDMAC hardware block)
-----------------------------------------------------------------------
log("--- Step 5: Power wake ---")
log("  Calling efm_pwr_wake()...")
fw_call(PWR.pwr_wake)
hardware_powered = true
log("  Hardware block should now be powered.")
log("")

-----------------------------------------------------------------------
-- Step 6: MMIO pre-check (NOW safe - hardware is powered)
-----------------------------------------------------------------------
log("--- Step 6: Post-power MMIO check ---")
dump_mmio_safe("Write ch 0x3D", WR_MMIO)
dump_mmio_safe("Read  ch 0x18", RD_MMIO)
log("")

-----------------------------------------------------------------------
-- Step 7: Initialize EDMAC channel pair
-- Uses DarkCurCor's ROM config: write_ch=0x3D, read_ch=0x18
-----------------------------------------------------------------------
log("--- Step 7: InitMem2MemModule ---")
logf("  Using DarkCurCor channel pair at ROM 0x%08X", DARKCURCOR.ch_pair)
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
log("  Done.")
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 8: Set completion callback (NOP = bx lr)
-----------------------------------------------------------------------
log("--- Step 8: Store callback ---")
fw_call(L2.store_struct, NOP_CBR, 0)
logf("  Callback=0x%08X ctx=0", NOP_CBR)
log("")

-----------------------------------------------------------------------
-- Step 9: Set addresses
-----------------------------------------------------------------------
log("--- Step 9: Set addresses ---")
fw_call(L2.set_addrs, ADDR_PAIR)
log("  Done.")
log("")

-----------------------------------------------------------------------
-- Step 10: Set geometry and mode
-----------------------------------------------------------------------
log("--- Step 10: Set geometry/mode ---")
fw_call(L2.set_geom_mode, GEOM_DESC)
log("  Done.")
log("")

-----------------------------------------------------------------------
-- Step 11: MMIO state before trigger
-----------------------------------------------------------------------
log("--- Step 11: Pre-trigger MMIO ---")
dump_mmio_safe("Write ch 0x3D", WR_MMIO)
dump_mmio_safe("Read  ch 0x18", RD_MMIO)
log("")

-----------------------------------------------------------------------
-- Step 12: TRIGGER DMA!
-- start_connect: arm(read) → arm(write) → connect(write)
-----------------------------------------------------------------------
log("--- Step 12: START DMA! ---")
log("  Calling mem2mem_start_connect()...")
fw_call(L2.start_connect)
log("  Returned from start_connect.")

-- Wait for hardware to complete
msleep(200)
log("  Waited 200ms for completion.")
log("")

-----------------------------------------------------------------------
-- Step 13: Post-trigger state
-----------------------------------------------------------------------
log("--- Step 13: Post-trigger ---")
dump_mmio_safe("Write ch 0x3D", WR_MMIO)
dump_mmio_safe("Read  ch 0x18", RD_MMIO)
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 14: Verify data
-----------------------------------------------------------------------
log("--- Step 14: Verification ---")
local match = 0
local first_fail = -1
for i = 0, XFER_SIZE/4 - 1 do
    local expected = 0xDEAD0000 + i
    local got = peek(DST_ADDR + i*4)
    if got == expected then
        match = match + 1
    elseif first_fail < 0 then
        first_fail = i
        logf("  FIRST MISMATCH word %d: expect=0x%08X got=0x%08X", i, expected, got)
    end
end
logf("")
logf("  *** RESULT: %d/%d words match ***", match, XFER_SIZE/4)
log("")

-- Show first 8 destination words
log("  DST sample:")
for i = 0, 7 do
    local v = peek(DST_ADDR + i*4)
    local e = 0xDEAD0000 + i
    local m = (v == e) and "OK" or "FAIL"
    logf("    [%d] 0x%08X (exp 0x%08X) %s", i, v, e, m)
end
log("")

-----------------------------------------------------------------------
-- Step 15: Cleanup
-----------------------------------------------------------------------
log("--- Step 15: Cleanup ---")

-- Reinit struct (reset callback to NOP)
fw_call(L2.reinit_struct)
log("  reinit_struct done")

-- Cleanup channels (de-init + disconnect)
fw_call(L2.cleanup_channels)
log("  cleanup_channels done")

-- Power sleep
fw_call(PWR.pwr_sleep)
hardware_powered = false
log("  pwr_sleep done")

-- Resource unlock
if lock_handle and lock_handle ~= 0 then
    fw_call(PWR.resource_unlock, lock_handle)
    logf("  resource_unlock(0x%08X) done", lock_handle)
else
    log("  skipping resource_unlock (no handle)")
end

log("")
log("=== Test v4.3 complete ===")
flush_log()
log("Log written to " .. LOG_FILE)
