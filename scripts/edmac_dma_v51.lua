-- EDMAC DMA Test v5.1 for Canon M50 (DIGIC 8)
-- ★★★ GEOMETRY OFFSET FIX ★★★
--
-- ROOT CAUSE: All v4.2-v5.0 wrote xfer size to struct offset +0x1C = "xs"
-- (DIGIC 8 super-block dimension), but actual transfer width "xb" is at +0x24.
-- This meant xb=0 → hardware transfers 0 bytes.
--
-- Fix: Set xb at +0x24, yb at +0x30 (0 = count-1 = 1 line).
--
-- DIGIC 8 edmac_info layout (from edmac.h CONFIG_DIGIC_8X):
--   +0x00: off1s   +0x04: off1a   +0x08: off1b
--   +0x0C: off2s   +0x10: off2a   +0x14: off2b
--   +0x18: off3
--   +0x1C: xs      +0x20: xa      +0x24: xb  ★ TRANSFER WIDTH
--   +0x28: ys      +0x2C: ya      +0x30: yb  ★ LINE COUNT - 1
--   +0x34: xn      +0x38: yn
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA51.LOG

local LOG_FILE = "ML/LOGS/DMA51.LOG"
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

local function peek(addr)  return dryos.peek(addr) end
local function poke(addr, val) dryos.poke(addr, val) end
local function fw_call(addr, ...) return dryos.fw_call(addr, ...) end
local function mmio_read(addr) return dryos.mmio_read(addr) end

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
    set_enable      = 0xE054A4F2,
    irq_enable      = 0xE054A386,
    set_addr        = 0xE054A9AE,
    set_geom_a      = 0xE054A9BA,
    set_mode        = 0xE054AEB0,
    start_arm       = 0xE054A7CC,
    connect         = 0xE0549E52,
    set_irq_handler = 0xE0549F76,
    abort_cleanup   = 0xE0549E8C,
    hw_reset        = 0xE054A8C8,
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
local NOP_CBR  = 0xE084DF49

local WR_MMIO = 0xD0487600
local RD_MMIO = 0xD0487100

-----------------------------------------------------------------------
-- Memory layout (uncacheable alias — proven equivalent to physical in v5.0)
-----------------------------------------------------------------------
local SRC_ADDR = 0x4F000000
local DST_ADDR = 0x4F001000

local GEOM_STRUCT = 0x4F002000  -- 15 fields × 4 bytes = 60 bytes
local ADDR_PAIR   = 0x4F003010
local GEOM_DESC   = 0x4F003020

local XFER_SIZE   = 256  -- bytes

-----------------------------------------------------------------------
local function dump_regs(label)
    logf("  %s:", label)
    logf("    Wr: CMD=%08X EN1=%08X ADDR=%08X CONN=%08X STAT=%08X CBR=%08X YB_XB=%08X",
        mmio_read(WR_MMIO+0x08), mmio_read(WR_MMIO+0x20),
        mmio_read(WR_MMIO+0xA0), mmio_read(WR_MMIO+0xB4),
        mmio_read(WR_MMIO+0xC4), mmio_read(WR_MMIO+0x3C),
        mmio_read(WR_MMIO+0x50))
    logf("    Rd: CMD=%08X EN1=%08X ADDR=%08X CONN=%08X STAT=%08X CBR=%08X YB_XB=%08X",
        mmio_read(RD_MMIO+0x08), mmio_read(RD_MMIO+0x20),
        mmio_read(RD_MMIO+0xA0), mmio_read(RD_MMIO+0xB4),
        mmio_read(RD_MMIO+0xC4), mmio_read(RD_MMIO+0x3C),
        mmio_read(RD_MMIO+0x50))
end

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v5.1 ===")
log("★ GEOMETRY FIX: xb at +0x24 (was +0x1C=xs) ★")
logf("SRC=0x%08X DST=0x%08X SIZE=%d bytes (%d words)",
    SRC_ADDR, DST_ADDR, XFER_SIZE, XFER_SIZE/4)
log("")

-- Step 1: Fill source, clear destination
log("--- 1: Fill source / clear dest ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(SRC_ADDR + i*4, 0xDEAD0000 + i)
    poke(DST_ADDR + i*4, 0)
end
logf("  SRC[0]=%08X SRC[63]=%08X DST[0]=%08X",
    peek(SRC_ADDR), peek(SRC_ADDR + 252), peek(DST_ADDR))
log("")

-- Step 2: Build geometry with CORRECT offsets
log("--- 2: Geometry (FIXED offsets) ---")
for i = 0, 14 do poke(GEOM_STRUCT + i*4, 0) end
-- ★ THE FIX: xb at struct offset +0x24 (field index 9), NOT +0x1C (xs)
poke(GEOM_STRUCT + 0x24, XFER_SIZE)  -- xb = 256 bytes per line
-- yb at +0x30 = 0 → count-1 semantics → 1 line (already 0 from memset)

-- Build addr pair
poke(ADDR_PAIR + 0, SRC_ADDR)  -- write_ch source
poke(ADDR_PAIR + 4, DST_ADDR)  -- read_ch destination

-- Build geometry descriptor
poke(GEOM_DESC + 0, GEOM_STRUCT)  -- write_geom
poke(GEOM_DESC + 4, GEOM_STRUCT)  -- read_geom
poke(GEOM_DESC + 8, 1)            -- mode = 1

-- Verify struct contents
logf("  geom struct +0x1C(xs)=%08X +0x24(xb)=%08X +0x30(yb)=%08X",
    peek(GEOM_STRUCT + 0x1C), peek(GEOM_STRUCT + 0x24), peek(GEOM_STRUCT + 0x30))
logf("  addr[0]=%08X(src) addr[1]=%08X(dst) mode=1", SRC_ADDR, DST_ADDR)
log("")

-- Step 3: Power up
log("--- 3: Power ---")
local lock_handle = fw_call(PWR.resource_lock, DARKCURCOR.res_entry, 2)
fw_call(PWR.pwr_wake)
logf("  lock=%08X", lock_handle or 0)
log("")

-- Step 4: Init + config via Layer 2
log("--- 4: Init + config ---")
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
fw_call(L2.store_struct, NOP_CBR, 0)
fw_call(L2.set_addrs, ADDR_PAIR)
fw_call(L2.set_geom_mode, GEOM_DESC)

-- Read back MMIO geometry registers to confirm
logf("  Wr MMIO+0x50 (yb_xb) = %08X (expect 0x00000100)",
    mmio_read(WR_MMIO + 0x50))
logf("  Rd MMIO+0x50 (yb_xb) = %08X (expect 0x00000100)",
    mmio_read(RD_MMIO + 0x50))
logf("  Wr MMIO+0x48 (ys_xs) = %08X (expect 0x00000000)",
    mmio_read(WR_MMIO + 0x48))
logf("  Wr ADDR=%08X Rd ADDR=%08X",
    mmio_read(WR_MMIO + 0xA0), mmio_read(RD_MMIO + 0xA0))
log("")

-- Step 5: Enable
log("--- 5: Enable ---")
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
fw_call(ELD.irq_enable, READ_CH)
log("  Done.")
log("")

-- Step 6: Pre-trigger state
log("--- 6: Pre-trigger ---")
dump_regs("Pre-trigger")
log("")

-- Step 7: Trigger
log("--- 7: TRIGGER ---")
fw_call(L2.start_connect)
msleep(200)
log("  Done (200ms).")
log("")

-- Step 8: Post-trigger
log("--- 8: Post-trigger ---")
dump_regs("Post-trigger")
log("")

-- Step 9: Verify
log("--- 9: Verify ---")
local match = 0
local first_fail_idx = -1
local first_fail_got = 0
for i = 0, XFER_SIZE/4 - 1 do
    local expected = 0xDEAD0000 + i
    local got = peek(DST_ADDR + i*4)
    if got == expected then
        match = match + 1
    elseif first_fail_idx < 0 then
        first_fail_idx = i
        first_fail_got = got
    end
end

logf("  *** RESULT: %d/%d words match ***", match, XFER_SIZE/4)
if match == XFER_SIZE/4 then
    log("  ★★★ DMA TRANSFER SUCCESSFUL! ★★★")
else
    if first_fail_idx >= 0 then
        logf("  First fail [%d]: exp=%08X got=%08X",
            first_fail_idx, 0xDEAD0000 + first_fail_idx, first_fail_got)
    end
end
-- Show first 8 words
for i = 0, 7 do
    logf("  DST[%d]=%08X SRC[%d]=%08X %s",
        i, peek(DST_ADDR + i*4), i, peek(SRC_ADDR + i*4),
        peek(DST_ADDR + i*4) == peek(SRC_ADDR + i*4) and "OK" or "FAIL")
end
log("")

-- Step 10: Cleanup
log("--- 10: Cleanup ---")
pcall(function() fw_call(L2.reinit_struct) end)
pcall(function() fw_call(L2.cleanup_channels) end)
fw_call(PWR.pwr_sleep)
if lock_handle and lock_handle ~= 0 then
    fw_call(PWR.resource_unlock, lock_handle)
end
log("  Done.")
log("")

logf("=== Test v5.1 complete: %d/%d ===", match, XFER_SIZE/4)
flush_log()
log("Log written to " .. LOG_FILE)
