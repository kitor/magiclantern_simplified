-- EDMAC DMA Test v5.0 for Canon M50 (DIGIC 8)
-- ★★★ PHYSICAL ADDRESS FIX ★★★
--
-- ROOT CAUSE HYPOTHESIS: EDMAC uses physical bus addresses.
-- CPU uncacheable alias 0x4F000000 = physical 0x0F000000.
-- Previous tests passed 0x4F... to EDMAC → bus error → DMA stalls.
--
-- Strategy:
--   - Write test data via CPU uncacheable alias (0x4F...)
--   - Pass PHYSICAL addresses (0x0F...) to EDMAC
--   - Read back via CPU uncacheable alias (0x4F...)
--
-- Also: properly reset channels in manual test (abort_cleanup not hw_reset)
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA50.LOG

local LOG_FILE = "ML/LOGS/DMA50.LOG"
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
    set_enable   = 0xE054A4F2,
    irq_enable   = 0xE054A386,
    set_addr     = 0xE054A9AE,
    set_geom_a   = 0xE054A9BA,
    set_mode     = 0xE054AEB0,
    start_arm    = 0xE054A7CC,
    connect      = 0xE0549E52,
    set_irq_handler = 0xE0549F76,
    abort_cleanup = 0xE0549E8C,
    hw_reset     = 0xE054A8C8,
}

local PWR = {
    resource_lock   = 0xE084B380,
    resource_unlock = 0xE084B3AC,
    pwr_wake        = 0xE084B446,
    pwr_sleep       = 0xE084B44E,
}

local DARKCURCOR = {
    res_entry  = 0xE0F72638,
    ch_pair    = 0xE0F72640,
}

local WRITE_CH = 0x3D
local READ_CH  = 0x18
local M2M_STRUCT = 0x00016F38
local NOP_CBR    = 0xE084DF49

local WR_MMIO = 0xD0487600
local RD_MMIO = 0xD0487100

-----------------------------------------------------------------------
-- Memory layout — PHYSICAL vs CPU alias
-- CPU uncacheable: 0x4F... (write/read test data via CPU)
-- Physical/DMA:    0x0F... (pass to EDMAC hardware)
-----------------------------------------------------------------------
local CPU_SRC  = 0x4F000000  -- CPU uncacheable alias for writing test data
local CPU_DST  = 0x4F001000  -- CPU uncacheable alias for reading results
local DMA_SRC  = 0x0F000000  -- Physical bus address for EDMAC source
local DMA_DST  = 0x0F001000  -- Physical bus address for EDMAC destination

-- Descriptors still in uncacheable RAM (CPU writes them, EDMAC shouldn't read them
-- since Layer 2 copies values to MMIO registers directly)
local GEOM_STRUCT = 0x4F002000
local ADDR_PAIR   = 0x4F003010
local GEOM_DESC   = 0x4F003020

local XFER_SIZE   = 256

-----------------------------------------------------------------------
local function dump_key_regs(label, wr_base, rd_base)
    logf("  %s:", label)
    logf("    Wr: CMD=%08X EN1=%08X ADDR=%08X CONN_ST=%08X STAT_EXT=%08X CBR=%08X",
        mmio_read(wr_base+0x08), mmio_read(wr_base+0x20),
        mmio_read(wr_base+0xA0), mmio_read(wr_base+0xB4),
        mmio_read(wr_base+0xC4), mmio_read(wr_base+0x3C))
    logf("    Rd: CMD=%08X EN1=%08X ADDR=%08X CONN_ST=%08X STAT_EXT=%08X CBR=%08X",
        mmio_read(rd_base+0x08), mmio_read(rd_base+0x20),
        mmio_read(rd_base+0xA0), mmio_read(rd_base+0xB4),
        mmio_read(rd_base+0xC4), mmio_read(rd_base+0x3C))
end

local function verify(cpu_dst, pattern_base, count)
    local match = 0
    local first_fail_idx = -1
    local first_fail_got = 0
    for i = 0, count - 1 do
        local expected = pattern_base + i
        local got = peek(cpu_dst + i*4)
        if got == expected then
            match = match + 1
        elseif first_fail_idx < 0 then
            first_fail_idx = i
            first_fail_got = got
        end
    end
    return match, first_fail_idx, first_fail_got
end

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v5.0 ===")
log("★ PHYSICAL ADDRESS FIX: EDMAC gets 0x0F..., CPU uses 0x4F... ★")
logf("CPU: SRC=0x%08X DST=0x%08X", CPU_SRC, CPU_DST)
logf("DMA: SRC=0x%08X DST=0x%08X", DMA_SRC, DMA_DST)
logf("SIZE=%d bytes (%d words)", XFER_SIZE, XFER_SIZE/4)
log("")

local lock_handle = nil

-----------------------------------------------------------------------
-- TEST A: Physical addresses with Layer 2 API
-----------------------------------------------------------------------
log("==============================")
log("TEST A: Layer 2 + PHYSICAL addrs")
log("==============================")
log("")

-- A1: Fill source, clear dest (via CPU uncacheable alias)
log("--- A1: Fill/clear ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(CPU_SRC + i*4, 0xDEAD0000 + i)
    poke(CPU_DST + i*4, 0)
end
logf("  SRC[0]=%08X SRC[63]=%08X DST[0]=%08X",
    peek(CPU_SRC), peek(CPU_SRC + 252), peek(CPU_DST))
log("")

-- A2: Build descriptors with PHYSICAL addresses
log("--- A2: Descriptors (PHYSICAL addrs) ---")
poke(ADDR_PAIR + 0, DMA_SRC)   -- write_ch = source → physical SRC
poke(ADDR_PAIR + 4, DMA_DST)   -- read_ch = sink → physical DST
for i = 0, 14 do poke(GEOM_STRUCT + i*4, 0) end
poke(GEOM_STRUCT + 0x1C, XFER_SIZE)
poke(GEOM_DESC + 0, GEOM_STRUCT)
poke(GEOM_DESC + 4, GEOM_STRUCT)
poke(GEOM_DESC + 8, 1)
logf("  addr[0]=%08X(src) addr[1]=%08X(dst) xb=%d mode=1",
    DMA_SRC, DMA_DST, XFER_SIZE)
log("")

-- A3: Power up
log("--- A3: Power ---")
lock_handle = fw_call(PWR.resource_lock, DARKCURCOR.res_entry, 2)
fw_call(PWR.pwr_wake)
logf("  lock=%08X", lock_handle or 0)
log("")

-- A4: Init + config
log("--- A4: Init + config ---")
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
fw_call(L2.store_struct, NOP_CBR, 0)
fw_call(L2.set_addrs, ADDR_PAIR)
fw_call(L2.set_geom_mode, GEOM_DESC)
logf("  Wr ADDR=%08X (expect %08X)", mmio_read(WR_MMIO + 0xA0), DMA_SRC)
logf("  Rd ADDR=%08X (expect %08X)", mmio_read(RD_MMIO + 0xA0), DMA_DST)
log("")

-- A5: Enable + IRQ
log("--- A5: Enable ---")
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
fw_call(ELD.irq_enable, READ_CH)
log("  EN + IRQ set.")
log("")

-- A6: Pre-trigger state
log("--- A6: Pre-trigger ---")
dump_key_regs("Pre-trigger", WR_MMIO, RD_MMIO)
log("")

-- A7: Trigger
log("--- A7: TRIGGER ---")
fw_call(L2.start_connect)
msleep(200)
log("  Done (200ms).")
log("")

-- A8: Post-trigger
log("--- A8: Post-trigger ---")
dump_key_regs("Post-trigger", WR_MMIO, RD_MMIO)
log("")

-- A9: Verify
log("--- A9: Verify ---")
local m, fi, fg = verify(CPU_DST, 0xDEAD0000, XFER_SIZE/4)
logf("  *** TEST A RESULT: %d/%d words match ***", m, XFER_SIZE/4)
if m < XFER_SIZE/4 and fi >= 0 then
    logf("  First fail [%d]: exp=%08X got=%08X", fi, 0xDEAD0000+fi, fg)
end
for i = 0, 3 do
    logf("  DST[%d]=%08X SRC[%d]=%08X", i, peek(CPU_DST+i*4), i, peek(CPU_SRC+i*4))
end
log("")

-- A10: Cleanup
log("--- A10: Cleanup ---")
fw_call(L2.reinit_struct)
fw_call(L2.cleanup_channels)
log("  Layer 2 cleanup done.")
log("")

-----------------------------------------------------------------------
-- TEST B: Physical addresses with DIRECT MMIO writes (bypass ROM)
-- Use abort_cleanup to properly reset from Test A's stuck state
-----------------------------------------------------------------------
log("==============================")
log("TEST B: Direct MMIO + PHYSICAL addrs")
log("==============================")
log("")

-- B1: Fill buffers with different pattern
log("--- B1: Fill/clear ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(CPU_SRC + i*4, 0xBEEF0000 + i)
    poke(CPU_DST + i*4, 0)
end
log("  Pattern: 0xBEEF...")
log("")

-- B2: Properly reset both channels (abort_cleanup, not hw_reset)
log("--- B2: Abort cleanup (proper reset) ---")
fw_call(ELD.abort_cleanup, READ_CH)
fw_call(ELD.abort_cleanup, WRITE_CH)
dump_key_regs("After abort", WR_MMIO, RD_MMIO)
log("")

-- B3: HW reset for clean slate
log("--- B3: HW reset ---")
fw_call(ELD.hw_reset, READ_CH)
fw_call(ELD.hw_reset, WRITE_CH)
dump_key_regs("After hw_reset", WR_MMIO, RD_MMIO)
log("")

-- B4: Configure via ELD primitives (PHYSICAL addresses)
log("--- B4: ELD config ---")
fw_call(ELD.set_addr, WRITE_CH, DMA_SRC)
fw_call(ELD.set_addr, READ_CH, DMA_DST)
fw_call(ELD.set_geom_a, WRITE_CH, GEOM_STRUCT)
fw_call(ELD.set_geom_a, READ_CH, GEOM_STRUCT)
fw_call(ELD.set_mode, WRITE_CH, 1)
fw_call(ELD.set_mode, READ_CH, 1)
fw_call(ELD.set_irq_handler, WRITE_CH, NOP_CBR, 0)
fw_call(ELD.set_irq_handler, READ_CH, NOP_CBR, 0)
logf("  Wr ADDR=%08X Rd ADDR=%08X", mmio_read(WR_MMIO+0xA0), mmio_read(RD_MMIO+0xA0))
log("")

-- B5: Enable + IRQ
log("--- B5: Enable ---")
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
fw_call(ELD.irq_enable, READ_CH)
log("  Done.")
log("")

-- B6: Pre-trigger
log("--- B6: Pre-trigger ---")
dump_key_regs("Pre-trigger", WR_MMIO, RD_MMIO)
log("")

-- B7: ARM + connect (following Canon's start_connect sequence)
log("--- B7: ARM + Connect ---")
fw_call(ELD.start_arm, READ_CH)   -- crossbar: MMIO+0xB4=1
fw_call(ELD.start_arm, WRITE_CH)  -- direct: MMIO+0x04=1
logf("  After ARM: Wr STAT_EXT=%08X Rd STAT_EXT=%08X",
    mmio_read(WR_MMIO+0xC4), mmio_read(RD_MMIO+0xC4))

fw_call(ELD.connect, WRITE_CH)    -- trigger: CMD=0x12, MMIO+0xB4=1
msleep(200)
log("  Connect done (200ms).")
log("")

-- B8: Post-trigger
log("--- B8: Post-trigger ---")
dump_key_regs("Post-trigger", WR_MMIO, RD_MMIO)
log("")

-- B9: Verify
log("--- B9: Verify ---")
local m2, fi2, fg2 = verify(CPU_DST, 0xBEEF0000, XFER_SIZE/4)
logf("  *** TEST B RESULT: %d/%d words match ***", m2, XFER_SIZE/4)
if m2 < XFER_SIZE/4 and fi2 >= 0 then
    logf("  First fail [%d]: exp=%08X got=%08X", fi2, 0xBEEF0000+fi2, fg2)
end
for i = 0, 3 do
    logf("  DST[%d]=%08X", i, peek(CPU_DST+i*4))
end
log("")

-----------------------------------------------------------------------
-- TEST C: Same as A but with UNCACHEABLE addresses (original v4.x style)
-- Control test to confirm physical vs uncacheable hypothesis
-----------------------------------------------------------------------
log("==============================")
log("TEST C: Layer 2 + UNCACHEABLE addrs (control)")
log("==============================")
log("")

-- C1: Reset
fw_call(ELD.abort_cleanup, READ_CH)
fw_call(ELD.abort_cleanup, WRITE_CH)

-- C2: Fill
for i = 0, XFER_SIZE/4 - 1 do
    poke(CPU_SRC + i*4, 0xCAFE0000 + i)
    poke(CPU_DST + i*4, 0)
end

-- C3: Descriptors with UNCACHEABLE addresses (like v4.x)
poke(ADDR_PAIR + 0, CPU_SRC)   -- 0x4F000000 (uncacheable)
poke(ADDR_PAIR + 4, CPU_DST)   -- 0x4F001000 (uncacheable)
logf("  Using UNCACHEABLE addrs: src=%08X dst=%08X", CPU_SRC, CPU_DST)

-- C4: Re-init + config  
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
fw_call(L2.store_struct, NOP_CBR, 0)
fw_call(L2.set_addrs, ADDR_PAIR)
fw_call(L2.set_geom_mode, GEOM_DESC)
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
fw_call(ELD.irq_enable, READ_CH)

-- C5: Trigger
fw_call(L2.start_connect)
msleep(200)

-- C6: Verify
local m3, fi3, fg3 = verify(CPU_DST, 0xCAFE0000, XFER_SIZE/4)
logf("  *** TEST C RESULT (uncacheable): %d/%d words match ***", m3, XFER_SIZE/4)
if m3 < XFER_SIZE/4 and fi3 >= 0 then
    logf("  First fail [%d]: exp=%08X got=%08X", fi3, 0xCAFE0000+fi3, fg3)
end
log("")

-----------------------------------------------------------------------
-- Cleanup
-----------------------------------------------------------------------
log("--- Final cleanup ---")
pcall(function() fw_call(L2.reinit_struct) end)
pcall(function() fw_call(L2.cleanup_channels) end)
fw_call(PWR.pwr_sleep)
if lock_handle and lock_handle ~= 0 then
    fw_call(PWR.resource_unlock, lock_handle)
end
log("  Done.")
log("")

log("==============================")
logf("SUMMARY: A(phys+L2)=%d/64  B(phys+direct)=%d/64  C(uncache+L2)=%d/64",
    m, m2, m3)
log("==============================")
log("")
log("=== Test v5.0 complete ===")
flush_log()
log("Log written to " .. LOG_FILE)
