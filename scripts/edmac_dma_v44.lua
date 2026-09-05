-- EDMAC DMA Test v4.4 for Canon M50 (DIGIC 8)
-- Uses Canon's Layer 2 MemoryToMemoryEsub5 wrappers
-- Based on DarkCurCor CopyOB reverse-engineered flow
--
-- FIX from v4.3 (0/64 words, STAT stuck at 0x01):
--   ROOT CAUSE: InitMem2MemModule calls clear_enable on both channels
--   (EN1=EN2=0) and irq_disable on read channel. Nothing re-enables
--   them before the transfer. Channels arm (STAT=0x01) but DMA gates
--   are closed so data never flows.
--
-- THREE FIXES in v4.4:
--   1. Call eld_edmac_set_enable(0x18) and (0x3D) after config, before trigger
--      → sets EN1=1, EN2=1, sub+0x18=1, sub+0x1C=1 on both channels
--   2. Call eld_edmac_irq_enable(0x18) after init, before trigger
--      → enables BoomerSelector IRQ for crossbar port 0x66
--   3. Extended MMIO diagnostics: EN2, sub-controller, STAT_EXT
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA44.LOG

local LOG_FILE = "ML/LOGS/DMA44.LOG"
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

-- Layer 1 ELD primitives (direct calls)
local ELD = {
    set_enable  = 0xE054A4F2, -- (ch) → writes EN1=1,EN2=1,subEN1=1,subEN2=1
    irq_enable  = 0xE054A386, -- (ch) → enables BoomerSelector IRQ for channel
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
local DARKCURCOR = {
    res_entry  = 0xE0F72638, -- resource lock entry
    ch_pair    = 0xE0F72640, -- {write_ch=0x3D, read_ch=0x18}
}

-- Channel numbers (from DarkCurCor config)
local WRITE_CH = 0x3D  -- 61
local READ_CH  = 0x18  -- 24

-- Mem2Mem global struct
local M2M_STRUCT = 0x00016F38

-- Channel MMIO bases (for diagnostic reads AFTER power-up)
local WR_MMIO     = 0xD0487600  -- ch 0x3D
local RD_MMIO     = 0xD0487100  -- ch 0x18
local WR_SUB_MMIO = 0xD0487D00  -- ch 0x3D sub-controller
local RD_SUB_MMIO = 0xD0487900  -- ch 0x18 sub-controller

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
-- Extended MMIO dump with EN2, sub-controller, STAT_EXT
-----------------------------------------------------------------------
local function dump_mmio_full(label, base, sub_base)
    local ok, err = pcall(function()
        logf("  %s MMIO=0x%08X:", label, base)
        logf("    CTRL=0x%08X  ARM=0x%08X  CMD=0x%08X",
            mmio_read(base + 0x00), mmio_read(base + 0x04), mmio_read(base + 0x08))
        logf("    EN1=0x%08X   EN2=0x%08X  CBR=0x%08X",
            mmio_read(base + 0x20), mmio_read(base + 0x24), mmio_read(base + 0x3C))
        logf("    G0=0x%08X  G1=0x%08X  G2=0x%08X",
            mmio_read(base + 0x48), mmio_read(base + 0x4C), mmio_read(base + 0x50))
        logf("    ADDR=0x%08X  STAT=0x%08X  MODE=0x%08X  STAT_EXT=0x%08X",
            mmio_read(base + 0xA0), mmio_read(base + 0xB4), mmio_read(base + 0xC0),
            mmio_read(base + 0xC4))
        if sub_base then
            logf("    SUB=0x%08X: EN1=0x%08X EN2=0x%08X RST=0x%08X IRQ=0x%08X",
                sub_base,
                mmio_read(sub_base + 0x18), mmio_read(sub_base + 0x1C),
                mmio_read(sub_base + 0x10), mmio_read(sub_base + 0x00))
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

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v4.4 ===")
log("v4.3 + set_enable + irq_enable fixes")
logf("SRC=0x%08X  DST=0x%08X  SIZE=%d", SRC_ADDR, DST_ADDR, XFER_SIZE)
log("")

local lock_handle = nil
local hardware_powered = false

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
logf("  SRC[0]=0x%08X SRC[1]=0x%08X", peek(SRC_ADDR), peek(SRC_ADDR + 4))
logf("  DST[0]=0x%08X DST[1]=0x%08X", peek(DST_ADDR), peek(DST_ADDR + 4))
log("")

-----------------------------------------------------------------------
-- Step 2: Build descriptors in uncacheable RAM
-----------------------------------------------------------------------
log("--- Step 2: Build descriptors ---")

-- Address pair: {dst_addr, src_addr}
poke(ADDR_PAIR + 0, DST_ADDR)
poke(ADDR_PAIR + 4, SRC_ADDR)
logf("  Addr pair: dst=0x%08X src=0x%08X", DST_ADDR, SRC_ADDR)

-- Geometry struct (60 bytes = 15 words): clear all, then set xb
for i = 0, 14 do
    poke(GEOM_STRUCT + i*4, 0)
end
poke(GEOM_STRUCT + 0x1C, XFER_SIZE)
logf("  Geom: xb=%d ya=0 (1 line of %d bytes)", XFER_SIZE, XFER_SIZE)

-- Geometry/mode descriptor: {write_geom_ptr, read_geom_ptr, mode}
poke(GEOM_DESC + 0, GEOM_STRUCT)
poke(GEOM_DESC + 4, GEOM_STRUCT)
poke(GEOM_DESC + 8, 1)
logf("  Geom desc: ptr=0x%08X mode=1", GEOM_STRUCT)
log("")

-----------------------------------------------------------------------
-- Step 3: Pre-check Mem2Mem struct
-----------------------------------------------------------------------
log("--- Step 3: Pre-check M2M struct ---")
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 4: Resource lock
-----------------------------------------------------------------------
log("--- Step 4: Resource lock ---")
logf("  Calling efm_resource_lock(0x%08X, 2)...", DARKCURCOR.res_entry)
lock_handle = fw_call(PWR.resource_lock, DARKCURCOR.res_entry, 2)
logf("  Lock handle = 0x%08X", lock_handle or 0)
log("")

-----------------------------------------------------------------------
-- Step 5: Power wake
-----------------------------------------------------------------------
log("--- Step 5: Power wake ---")
fw_call(PWR.pwr_wake)
hardware_powered = true
log("  Hardware powered.")
log("")

-----------------------------------------------------------------------
-- Step 6: Post-power MMIO check
-----------------------------------------------------------------------
log("--- Step 6: Post-power MMIO ---")
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
log("")

-----------------------------------------------------------------------
-- Step 7: InitMem2MemModule
-- This resets channels, sets crossbar routing, registers ISRs,
-- then calls clear_enable on both (EN1=EN2=0, sub_EN=0)
-- and irq_disable on read_ch (disables BoomerSelector port 0x66)
-----------------------------------------------------------------------
log("--- Step 7: InitMem2MemModule ---")
logf("  ch_pair ROM ptr = 0x%08X", DARKCURCOR.ch_pair)
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
log("  Done.")
dump_m2m_struct()

-- Show EN state after init (should all be 0)
logf("  Post-init EN state:")
logf("    Wr EN1=0x%08X EN2=0x%08X  Sub_EN1=0x%08X Sub_EN2=0x%08X",
    mmio_read(WR_MMIO + 0x20), mmio_read(WR_MMIO + 0x24),
    mmio_read(WR_SUB_MMIO + 0x18), mmio_read(WR_SUB_MMIO + 0x1C))
logf("    Rd EN1=0x%08X EN2=0x%08X  Sub_EN1=0x%08X Sub_EN2=0x%08X",
    mmio_read(RD_MMIO + 0x20), mmio_read(RD_MMIO + 0x24),
    mmio_read(RD_SUB_MMIO + 0x18), mmio_read(RD_SUB_MMIO + 0x1C))
log("")

-----------------------------------------------------------------------
-- Step 8: Set callback
-----------------------------------------------------------------------
log("--- Step 8: Store callback ---")
fw_call(L2.store_struct, NOP_CBR, 0)
logf("  Callback=0x%08X (NOP bx lr) ctx=0", NOP_CBR)
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
-- Step 11: ★★★ FIX #1: Enable DMA gates on both channels ★★★
-- InitMem2MemModule disabled EN1/EN2 + sub enables.
-- set_enable writes 1 to: MMIO+0x20, +0x24, sub+0x18, sub+0x1C
-----------------------------------------------------------------------
log("--- Step 11: FIX #1 — set_enable on both channels ---")
logf("  Calling eld_edmac_set_enable(%d) [read ch]...", READ_CH)
fw_call(ELD.set_enable, READ_CH)
logf("  Calling eld_edmac_set_enable(%d) [write ch]...", WRITE_CH)
fw_call(ELD.set_enable, WRITE_CH)

-- Verify EN is now 1
logf("  Post-enable state:")
logf("    Wr EN1=0x%08X EN2=0x%08X  Sub_EN1=0x%08X Sub_EN2=0x%08X",
    mmio_read(WR_MMIO + 0x20), mmio_read(WR_MMIO + 0x24),
    mmio_read(WR_SUB_MMIO + 0x18), mmio_read(WR_SUB_MMIO + 0x1C))
logf("    Rd EN1=0x%08X EN2=0x%08X  Sub_EN1=0x%08X Sub_EN2=0x%08X",
    mmio_read(RD_MMIO + 0x20), mmio_read(RD_MMIO + 0x24),
    mmio_read(RD_SUB_MMIO + 0x18), mmio_read(RD_SUB_MMIO + 0x1C))
log("")

-----------------------------------------------------------------------
-- Step 12: ★★★ FIX #2: Enable IRQ for read channel ★★★
-- InitMem2MemModule disabled BoomerSelector IRQ for port 0x66.
-- irq_enable re-enables it so the ISR can fire on DMA completion.
-----------------------------------------------------------------------
log("--- Step 12: FIX #2 — irq_enable on read channel ---")
logf("  Calling eld_edmac_irq_enable(%d) [read ch, port 0x66]...", READ_CH)
fw_call(ELD.irq_enable, READ_CH)
log("  Done.")
log("")

-----------------------------------------------------------------------
-- Step 13: Pre-trigger full MMIO dump
-----------------------------------------------------------------------
log("--- Step 13: Pre-trigger MMIO ---")
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
log("")

-----------------------------------------------------------------------
-- Step 14: TRIGGER DMA!
-- start_connect: arm(read) → arm(write) → connect(write)
-----------------------------------------------------------------------
log("--- Step 14: START DMA! ---")
log("  Calling mem2mem_start_connect()...")
fw_call(L2.start_connect)
log("  Returned from start_connect.")

-- Wait for hardware
msleep(200)
log("  Waited 200ms for completion.")
log("")

-----------------------------------------------------------------------
-- Step 15: Post-trigger state
-----------------------------------------------------------------------
log("--- Step 15: Post-trigger ---")
dump_mmio_full("Write ch 0x3D", WR_MMIO, WR_SUB_MMIO)
dump_mmio_full("Read  ch 0x18", RD_MMIO, RD_SUB_MMIO)
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 16: Verify data
-----------------------------------------------------------------------
log("--- Step 16: Verification ---")
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
-- Step 17: Cleanup
-----------------------------------------------------------------------
log("--- Step 17: Cleanup ---")

fw_call(L2.reinit_struct)
log("  reinit_struct done")

fw_call(L2.cleanup_channels)
log("  cleanup_channels done")

fw_call(PWR.pwr_sleep)
hardware_powered = false
log("  pwr_sleep done")

if lock_handle and lock_handle ~= 0 then
    fw_call(PWR.resource_unlock, lock_handle)
    logf("  resource_unlock(0x%08X) done", lock_handle)
else
    log("  skipping resource_unlock (no handle)")
end

log("")
log("=== Test v4.4 complete ===")
flush_log()
log("Log written to " .. LOG_FILE)
