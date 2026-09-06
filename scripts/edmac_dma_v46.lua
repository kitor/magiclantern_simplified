-- EDMAC DMA Test v4.6 for Canon M50 (DIGIC 8)
-- FULL MMIO register hex dump for root cause analysis
--
-- v4.5 findings:
--   - Addresses, enables, crossbar all look correct
--   - STAT_EXT=0x340 (armed but never busy)
--   - BoomerSelector port 0x66=0x0D (read ch connected)
--   - BoomerSelector port 0xF2=0x00 (write ch NOT connected)
--   - 0/64 words transferred
--
-- v4.6 strategy:
--   - Full hex dump of ALL MMIO regs (0x00-0xFF) per channel
--   - Pre-init, post-init, post-config, post-enable, post-trigger
--   - Also try: call set_enable + irq_enable AFTER start_arm
--     (in case start_arm's sub-reset undoes enables)
--   - Also try: skip Layer 2 start_connect, manually write MMIO
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA46.LOG

local LOG_FILE = "ML/LOGS/DMA46.LOG"
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

local function mmio_write(addr, val)
    -- Use shamem_read for writing: we need dryos.poke for MMIO too
    -- Actually poke() should work for MMIO on DIGIC 8
    dryos.poke(addr, val)
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
    reset       = 0xE0549E8C,   -- eld_edmac_abort_cleanup (was: reset 0xE054A8C8)
    set_addr    = 0xE054A9AE,
    set_geom_a  = 0xE054A9BA,
    set_mode    = 0xE054AEB0,   -- set_geom_b / set_mode
    set_enable  = 0xE054A4F2,
    clear_enable = 0xE054A4B2,
    irq_enable  = 0xE054A386,
    irq_disable = 0xE054A35C,
    start_arm   = 0xE054A7CC,
    connect     = 0xE0549E52,
    set_irq_handler = 0xE0549F76,
    crossbar_config = 0xE054A3DA,
    disconnect  = 0xE054A46E,
    hw_reset    = 0xE054A8C8,   -- simple MMIO+0x00 pulse
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

local WR_MMIO     = 0xD0487600
local RD_MMIO     = 0xD0487100
local WR_SUB_MMIO = 0xD0487D00
local RD_SUB_MMIO = 0xD0487900

local BOOMER_TABLE = 0xE0FC40E0
local GLOBAL_DMA   = 0xC1100730

-----------------------------------------------------------------------
-- Memory layout
-----------------------------------------------------------------------
local SRC_ADDR    = 0x4F000000
local DST_ADDR    = 0x4F001000
local GEOM_STRUCT = 0x4F002000
local ADDR_PAIR   = 0x4F003010
local GEOM_DESC   = 0x4F003020

local XFER_SIZE   = 256

-----------------------------------------------------------------------
-- Full MMIO hex dump: reads 0x00 to max_off in 16-byte lines
-----------------------------------------------------------------------
local function hex_dump_mmio(label, base, max_off)
    logf("  %s full dump (0x%08X):", label, base)
    for off = 0, max_off, 16 do
        local v0 = mmio_read(base + off)
        local v1 = mmio_read(base + off + 4)
        local v2 = mmio_read(base + off + 8)
        local v3 = mmio_read(base + off + 12)
        logf("    +%03X: %08X %08X %08X %08X", off, v0, v1, v2, v3)
    end
end

local function dump_sub(label, sub_base)
    logf("  %s SUB dump (0x%08X):", label, sub_base)
    for off = 0, 0x30, 16 do
        local v0 = mmio_read(sub_base + off)
        local v1 = mmio_read(sub_base + off + 4)
        local v2 = mmio_read(sub_base + off + 8)
        local v3 = mmio_read(sub_base + off + 12)
        logf("    +%03X: %08X %08X %08X %08X", off, v0, v1, v2, v3)
    end
end

local function dump_boomer()
    local function read_port(port)
        local ok, val = pcall(function()
            local reg_ptr = peek(BOOMER_TABLE + port * 4)
            local reg_val = mmio_read(reg_ptr)
            return string.format("ptr=0x%08X val=0x%08X", reg_ptr, reg_val)
        end)
        return ok and val or "FAIL"
    end
    logf("  Boomer 0x66(rd)=%s  0xF2(wr)=%s", read_port(0x66), read_port(0xF2))
end

local function dump_m2m()
    logf("  M2M: cb=%08X ctx=%08X wr=%08X rd=%08X fl=%08X",
        peek(M2M_STRUCT), peek(M2M_STRUCT+4), peek(M2M_STRUCT+8),
        peek(M2M_STRUCT+12), peek(M2M_STRUCT+16))
end

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v4.6 ===")
log("Full MMIO register hex dump for root cause analysis")
logf("SRC=0x%08X DST=0x%08X SIZE=%d", SRC_ADDR, DST_ADDR, XFER_SIZE)
log("")

local lock_handle = nil

-- Step 1: Fill source, clear dest
log("--- 1: Fill/clear ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(SRC_ADDR + i*4, 0xDEAD0000 + i)
    poke(DST_ADDR + i*4, 0)
end
log("  Done.")

-- Step 2: Build descriptors (FIXED address order)
log("--- 2: Descriptors ---")
poke(ADDR_PAIR + 0, SRC_ADDR)   -- write_ch = SOURCE
poke(ADDR_PAIR + 4, DST_ADDR)   -- read_ch = SINK
for i = 0, 14 do poke(GEOM_STRUCT + i*4, 0) end
poke(GEOM_STRUCT + 0x1C, XFER_SIZE)
poke(GEOM_DESC + 0, GEOM_STRUCT)
poke(GEOM_DESC + 4, GEOM_STRUCT)
poke(GEOM_DESC + 8, 1)
logf("  addr[0]=SRC=%08X addr[1]=DST=%08X xb=%d mode=1",
    SRC_ADDR, DST_ADDR, XFER_SIZE)
log("")

-- Step 3: Power up
log("--- 3: Power ---")
lock_handle = fw_call(PWR.resource_lock, DARKCURCOR.res_entry, 2)
fw_call(PWR.pwr_wake)
logf("  lock=%08X powered.", lock_handle or 0)
log("")

-- Step 4: Pre-init FULL hex dump
log("--- 4: Pre-init FULL dump ---")
hex_dump_mmio("Write 0x3D", WR_MMIO, 0xF0)
dump_sub("Write 0x3D", WR_SUB_MMIO)
hex_dump_mmio("Read 0x18", RD_MMIO, 0xF0)
dump_sub("Read 0x18", RD_SUB_MMIO)
dump_boomer()
log("")

-- Step 5: InitMem2MemModule
log("--- 5: InitMem2MemModule ---")
fw_call(L2.InitMem2MemModule, DARKCURCOR.ch_pair)
log("  Done.")
dump_m2m()
log("")

-- Step 6: Post-init hex dump
log("--- 6: Post-init FULL dump ---")
hex_dump_mmio("Write 0x3D", WR_MMIO, 0xF0)
dump_sub("Write 0x3D", WR_SUB_MMIO)
hex_dump_mmio("Read 0x18", RD_MMIO, 0xF0)
dump_sub("Read 0x18", RD_SUB_MMIO)
dump_boomer()
log("")

-- Step 7: Config (callback + addrs + geom)
log("--- 7: Config ---")
fw_call(L2.store_struct, NOP_CBR, 0)
fw_call(L2.set_addrs, ADDR_PAIR)
fw_call(L2.set_geom_mode, GEOM_DESC)
logf("  Wr ADDR=%08X Rd ADDR=%08X",
    mmio_read(WR_MMIO + 0xA0), mmio_read(RD_MMIO + 0xA0))
log("")

-- Step 8: Post-config hex dump
log("--- 8: Post-config FULL dump ---")
hex_dump_mmio("Write 0x3D", WR_MMIO, 0xF0)
hex_dump_mmio("Read 0x18", RD_MMIO, 0xF0)
log("")

-- Step 9: set_enable (both) + irq_enable (read)
log("--- 9: Enable ---")
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
fw_call(ELD.irq_enable, READ_CH)
logf("  Wr EN1=%08X EN2=%08X Rd EN1=%08X EN2=%08X",
    mmio_read(WR_MMIO+0x20), mmio_read(WR_MMIO+0x24),
    mmio_read(RD_MMIO+0x20), mmio_read(RD_MMIO+0x24))
log("")

-- Step 10: Pre-trigger hex dump
log("--- 10: Pre-trigger FULL dump ---")
hex_dump_mmio("Write 0x3D", WR_MMIO, 0xF0)
dump_sub("Write 0x3D", WR_SUB_MMIO)
hex_dump_mmio("Read 0x18", RD_MMIO, 0xF0)
dump_sub("Read 0x18", RD_SUB_MMIO)
dump_boomer()
log("")

-- Step 11: TRIGGER (using Layer 2 start_connect)
log("--- 11: TRIGGER ---")
fw_call(L2.start_connect)
log("  start_connect returned.")
msleep(200)
log("  200ms wait done.")
log("")

-- Step 12: Post-trigger hex dump
log("--- 12: Post-trigger FULL dump ---")
hex_dump_mmio("Write 0x3D", WR_MMIO, 0xF0)
dump_sub("Write 0x3D", WR_SUB_MMIO)
hex_dump_mmio("Read 0x18", RD_MMIO, 0xF0)
dump_sub("Read 0x18", RD_SUB_MMIO)
dump_boomer()
dump_m2m()
log("")

-- Step 13: Verify
log("--- 13: Verify ---")
local match = 0
for i = 0, XFER_SIZE/4 - 1 do
    if peek(DST_ADDR + i*4) == (0xDEAD0000 + i) then match = match + 1 end
end
logf("  *** RESULT: %d/%d words ***", match, XFER_SIZE/4)
if match == 0 then
    logf("  DST[0]=%08X DST[1]=%08X", peek(DST_ADDR), peek(DST_ADDR+4))
end
log("")

-- Step 14: Also try manual MMIO trigger (bypass Layer 2)
-- Reset and re-do everything manually
log("--- 14: MANUAL MMIO attempt ---")
log("  Re-filling buffers...")
for i = 0, XFER_SIZE/4 - 1 do
    poke(SRC_ADDR + i*4, 0xBEEF0000 + i)
    poke(DST_ADDR + i*4, 0)
end

-- Hardware reset both channels
log("  HW reset...")
fw_call(ELD.hw_reset, READ_CH)
fw_call(ELD.hw_reset, WRITE_CH)

-- Set addresses directly via ELD
log("  Set addrs...")
fw_call(ELD.set_addr, WRITE_CH, SRC_ADDR)  -- write_ch=source
fw_call(ELD.set_addr, READ_CH, DST_ADDR)   -- read_ch=sink

-- Set geometry
log("  Set geom...")
fw_call(ELD.set_geom_a, WRITE_CH, GEOM_STRUCT)
fw_call(ELD.set_geom_a, READ_CH, GEOM_STRUCT)
fw_call(ELD.set_mode, WRITE_CH, 1)
fw_call(ELD.set_mode, READ_CH, 1)

-- Set crossbar config for read channel
-- InitMem2MemModule used config that yielded port 0x66=0x0D
-- The packed_config value: we need to figure this out
-- From ch_pair at 0xE0F72640, the config is embedded
-- Let's skip this and use the existing crossbar state (still 0x0D from init)
log("  Crossbar: using existing port 0x66=0x0D from InitMem2MemModule")

-- Set IRQ handlers (NOP)
log("  Set IRQ handlers...")
fw_call(ELD.set_irq_handler, WRITE_CH, NOP_CBR, 0)
fw_call(ELD.set_irq_handler, READ_CH, NOP_CBR, 0)

-- Enable
log("  Enable...")
fw_call(ELD.set_enable, READ_CH)
fw_call(ELD.set_enable, WRITE_CH)
fw_call(ELD.irq_enable, READ_CH)

-- Dump pre-manual-trigger
log("  Pre-manual-trigger key regs:")
logf("    Wr: EN1=%08X EN2=%08X ADDR=%08X CONN_ST=%08X STAT_EXT=%08X",
    mmio_read(WR_MMIO+0x20), mmio_read(WR_MMIO+0x24),
    mmio_read(WR_MMIO+0xA0), mmio_read(WR_MMIO+0xB4), mmio_read(WR_MMIO+0xC4))
logf("    Rd: EN1=%08X EN2=%08X ADDR=%08X CONN_ST=%08X STAT_EXT=%08X",
    mmio_read(RD_MMIO+0x20), mmio_read(RD_MMIO+0x24),
    mmio_read(RD_MMIO+0xA0), mmio_read(RD_MMIO+0xB4), mmio_read(RD_MMIO+0xC4))

-- ARM read channel (crossbar): set CONN_ST=1
log("  ARM read ch (MMIO+0xB4=1)...")
fw_call(ELD.start_arm, READ_CH)

-- Re-enable AFTER start_arm (in case sub-reset cleared internal state)
fw_call(ELD.set_enable, READ_CH)

-- ARM write channel (direct): set ARM=1
log("  ARM write ch...")
fw_call(ELD.start_arm, WRITE_CH)
fw_call(ELD.set_enable, WRITE_CH)

-- Snapshot before connect
logf("    Wr: ARM=%08X CMD=%08X CONN_ST=%08X STAT_EXT=%08X",
    mmio_read(WR_MMIO+0x04), mmio_read(WR_MMIO+0x08),
    mmio_read(WR_MMIO+0xB4), mmio_read(WR_MMIO+0xC4))
logf("    Rd: ARM=%08X CMD=%08X CONN_ST=%08X STAT_EXT=%08X",
    mmio_read(RD_MMIO+0x04), mmio_read(RD_MMIO+0x08),
    mmio_read(RD_MMIO+0xB4), mmio_read(RD_MMIO+0xC4))

-- CONNECT write channel (trigger!)
log("  CONNECT write ch...")
fw_call(ELD.connect, WRITE_CH)

msleep(200)
log("  200ms wait done.")

-- Post-manual-trigger
logf("    Wr: CMD=%08X CONN_ST=%08X STAT_EXT=%08X",
    mmio_read(WR_MMIO+0x08), mmio_read(WR_MMIO+0xB4), mmio_read(WR_MMIO+0xC4))
logf("    Rd: CMD=%08X CONN_ST=%08X STAT_EXT=%08X",
    mmio_read(RD_MMIO+0x08), mmio_read(RD_MMIO+0xB4), mmio_read(RD_MMIO+0xC4))

-- Verify manual attempt
local match2 = 0
for i = 0, XFER_SIZE/4 - 1 do
    if peek(DST_ADDR + i*4) == (0xBEEF0000 + i) then match2 = match2 + 1 end
end
logf("  *** MANUAL RESULT: %d/%d words ***", match2, XFER_SIZE/4)
if match2 == 0 then
    logf("  DST[0]=%08X DST[1]=%08X", peek(DST_ADDR), peek(DST_ADDR+4))
end
log("")

-- Step 15: Cleanup
log("--- 15: Cleanup ---")
pcall(function() fw_call(L2.reinit_struct) end)
pcall(function() fw_call(L2.cleanup_channels) end)
fw_call(PWR.pwr_sleep)
if lock_handle and lock_handle ~= 0 then
    fw_call(PWR.resource_unlock, lock_handle)
end
log("  Done.")
log("")

log("=== Test v4.6 complete ===")
flush_log()
log("Log written to " .. LOG_FILE)
