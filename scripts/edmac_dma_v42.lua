-- EDMAC DMA Test v4.2 for Canon M50 (DIGIC 8)
-- Uses Canon's Layer 2 MemoryToMemoryEsub5 wrappers
-- Based on DarkCurCor CopyOB reverse-engineered flow
--
-- BREAKTHROUGH: Prior tests (v1-v4.1) used channels 0x28-0x2F which
-- lack flag bit 12 → geometry writes fail, set_geom asserts.
-- Canon's Mem2Mem module uses channels with bit 12 (0x3D write, 0x18 read).
--
-- Layer 2 API sequence (DarkCurCor pattern):
--   1. InitMem2MemModule(channel_pair_desc)
--   2. mem2mem_store_struct(callback, ctx)
--   3. mem2mem_set_addrs(addr_pair)
--   4. mem2mem_set_geom_mode(geom_mode_desc)
--   5. mem2mem_start_connect()  ← TRIGGERS DMA
--   6. Wait for completion
--   7. mem2mem_reinit_struct()
--   8. mem2mem_cleanup_channels()
--
-- IMPORTANT: No os.date(), no os module!
-- Results logged to ML/LOGS/DMA42.LOG

local LOG_FILE = "ML/LOGS/DMA42.LOG"
local log_lines = {}

local function log(fmt, ...)
    local line
    if select('#', ...) > 0 then
        line = string.format(fmt, ...)
    else
        line = fmt
    end
    log_lines[#log_lines + 1] = line
    pcall(print, line)
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
-- Layer 2 API function addresses (even = fw_call handles Thumb)
-----------------------------------------------------------------------
local L2 = {
    InitMem2MemModule  = 0xE084DE9C, -- (channel_pair_desc_ptr)
    store_struct       = 0xE084DF40, -- (callback, ctx)
    reinit_struct      = 0xE084DF4A, -- ()
    cleanup_channels   = 0xE084DF26, -- ()
    set_addrs          = 0xE084DF70, -- (addr_pair_ptr)
    set_geom_mode      = 0xE084DF8A, -- (geom_mode_desc_ptr)
    start_connect      = 0xE084DF56, -- ()
}

-- NOP callback (bx lr at 0xE084DF49) - safe completion callback
local NOP_CBR = 0xE084DF49

-- Mem2Mem global struct address
local M2M_STRUCT = 0x00016F38

-----------------------------------------------------------------------
-- Channel configuration
-- DarkCurCor uses: write_ch=0x3D, read_ch=0x18
-- Both have flag bit 12 (geometry support), MMIO in 0xD0487xxx block
-----------------------------------------------------------------------
local WRITE_CH = 0x3D  -- channel 61, MMIO 0xD0487600, flags 0x01C06
local READ_CH  = 0x18  -- channel 24, MMIO 0xD0487100, flags 0x81C09

local WR_MMIO = 0xD0487600
local RD_MMIO = 0xD0487100

-----------------------------------------------------------------------
-- Memory layout (all in uncacheable RAM: 0x40000000-0x5FFFFFFF)
-----------------------------------------------------------------------
local SRC_ADDR    = 0x4F000000  -- 256 bytes source data
local DST_ADDR    = 0x4F001000  -- 256 bytes destination
local GEOM_STRUCT = 0x4F002000  -- 60-byte edmac_geom struct
local PAIR_DESC   = 0x4F003000  -- 8-byte channel pair descriptor
local ADDR_PAIR   = 0x4F003010  -- 8-byte {dst, src}
local GEOM_DESC   = 0x4F003020  -- 12-byte {write_geom, read_geom, mode}

local XFER_SIZE   = 256  -- bytes to transfer (must be even)

-----------------------------------------------------------------------
-- edmac_geom struct layout (verified from eld_edmac_set_geom disasm):
--   +0x00: off2a    +0x04: off3a    +0x08: off4a
--   +0x0C: off2b    +0x10: off3b    +0x14: off4b
--   +0x18: off5
--   +0x1C: xb (bytes per line, MUST be even)
--   +0x20: xa (stride)   +0x24: xn
--   +0x28: ya (additional lines, 0=1 line)
--   +0x2C: yb   +0x30: yn
--   +0x34: off1a   +0x38: off1b
-- Total: 0x3C = 60 bytes
-----------------------------------------------------------------------

local function dump_mmio(label, base)
    log("  %s MMIO=0x%08X:", label, base)
    log("    +0x00(CTRL)=0x%08X  +0x04(ARM)=0x%08X  +0x08(CMD)=0x%08X",
        mmio_read(base + 0x00), mmio_read(base + 0x04), mmio_read(base + 0x08))
    log("    +0x20(EN1)=0x%08X   +0x3C(CBR)=0x%08X",
        mmio_read(base + 0x20), mmio_read(base + 0x3C))
    log("    +0x48(G0)=0x%08X   +0x4C(G1)=0x%08X   +0x50(G2)=0x%08X",
        mmio_read(base + 0x48), mmio_read(base + 0x4C), mmio_read(base + 0x50))
    log("    +0x54(G3)=0x%08X   +0x58(G4)=0x%08X   +0x5C(G5)=0x%08X",
        mmio_read(base + 0x54), mmio_read(base + 0x58), mmio_read(base + 0x5C))
    log("    +0xA0(ADDR)=0x%08X  +0xB4(STAT)=0x%08X  +0xC0(MODE)=0x%08X",
        mmio_read(base + 0xA0), mmio_read(base + 0xB4), mmio_read(base + 0xC0))
end

local function dump_m2m_struct()
    log("  Mem2Mem struct (0x%08X):", M2M_STRUCT)
    log("    +0x00(callback)=0x%08X  +0x04(ctx)=0x%08X",
        peek(M2M_STRUCT + 0), peek(M2M_STRUCT + 4))
    log("    +0x08(write_ch)=0x%08X  +0x0C(read_ch)=0x%08X  +0x10(flags)=0x%08X",
        peek(M2M_STRUCT + 8), peek(M2M_STRUCT + 12), peek(M2M_STRUCT + 16))
end

-----------------------------------------------------------------------
-- MAIN TEST
-----------------------------------------------------------------------
log("=== M50 EDMAC DMA Test v4.2 ===")
log("Layer 2 MemoryToMemoryEsub5 API")
log("WRITE_CH=0x%02X (MMIO=0x%08X)  READ_CH=0x%02X (MMIO=0x%08X)",
    WRITE_CH, WR_MMIO, READ_CH, RD_MMIO)
log("SRC=0x%08X  DST=0x%08X  SIZE=%d bytes", SRC_ADDR, DST_ADDR, XFER_SIZE)
log("")

-----------------------------------------------------------------------
-- Step 0: Pre-check channel MMIO state
-----------------------------------------------------------------------
log("--- Step 0: Pre-check channel state ---")
dump_mmio("Write ch 0x3D", WR_MMIO)
dump_mmio("Read  ch 0x18", RD_MMIO)
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 1: Fill source pattern, clear destination
-----------------------------------------------------------------------
log("--- Step 1: Fill source, clear dest ---")
for i = 0, XFER_SIZE/4 - 1 do
    poke(SRC_ADDR + i*4, 0xDEAD0000 + i)
end
for i = 0, XFER_SIZE/4 - 1 do
    poke(DST_ADDR + i*4, 0)
end

-- Verify source and dest
local s0 = peek(SRC_ADDR)
local s1 = peek(SRC_ADDR + 4)
local d0 = peek(DST_ADDR)
local d1 = peek(DST_ADDR + 4)
log("  SRC[0]=0x%08X SRC[1]=0x%08X (expect 0xDEAD0000, 0xDEAD0001)", s0, s1)
log("  DST[0]=0x%08X DST[1]=0x%08X (expect 0, 0)", d0, d1)
log("")

-----------------------------------------------------------------------
-- Step 2: Build descriptors in uncacheable RAM
-----------------------------------------------------------------------
log("--- Step 2: Build descriptors ---")

-- Channel pair descriptor: {write_ch, read_ch}
poke(PAIR_DESC + 0, WRITE_CH)
poke(PAIR_DESC + 4, READ_CH)
log("  Channel pair at 0x%08X: write=0x%02X read=0x%02X",
    PAIR_DESC, WRITE_CH, READ_CH)

-- Address pair: {dst_addr, src_addr}
poke(ADDR_PAIR + 0, DST_ADDR)
poke(ADDR_PAIR + 4, SRC_ADDR)
log("  Addr pair at 0x%08X: dst=0x%08X src=0x%08X",
    ADDR_PAIR, DST_ADDR, SRC_ADDR)

-- Geometry struct (60 bytes): clear all, set xb
for i = 0, 14 do  -- 15 words = 60 bytes
    poke(GEOM_STRUCT + i*4, 0)
end
poke(GEOM_STRUCT + 0x1C, XFER_SIZE)  -- xb = bytes per line
log("  Geom struct at 0x%08X: xb=%d at +0x1C, all else 0",
    GEOM_STRUCT, XFER_SIZE)

-- Verify geom struct
log("  Geom verify: +0x1C=0x%08X +0x28=0x%08X +0x20=0x%08X",
    peek(GEOM_STRUCT + 0x1C), peek(GEOM_STRUCT + 0x28), peek(GEOM_STRUCT + 0x20))

-- Geometry/mode descriptor: {write_geom_ptr, read_geom_ptr, mode}
poke(GEOM_DESC + 0, GEOM_STRUCT)  -- write channel geometry
poke(GEOM_DESC + 4, GEOM_STRUCT)  -- read channel geometry (same = symmetric)
poke(GEOM_DESC + 8, 1)            -- mode = 1 (mem2mem transfer mode)
log("  Geom desc at 0x%08X: geom_ptr=0x%08X mode=1", GEOM_DESC, GEOM_STRUCT)
log("")

-----------------------------------------------------------------------
-- Step 3: Call Layer 2 API
-----------------------------------------------------------------------
log("--- Step 3: Layer 2 API calls ---")

-- 3a. InitMem2MemModule(channel_pair_desc)
log("3a. InitMem2MemModule(0x%08X)...", PAIR_DESC)
fw_call(L2.InitMem2MemModule, PAIR_DESC)
log("    done.")
dump_m2m_struct()
log("")

-- 3b. mem2mem_store_struct(NOP_callback, 0)
log("3b. mem2mem_store_struct(NOP=0x%08X, 0)...", NOP_CBR)
fw_call(L2.store_struct, NOP_CBR, 0)
log("    done. callback=0x%08X ctx=0x%08X",
    peek(M2M_STRUCT), peek(M2M_STRUCT + 4))
log("")

-- 3c. mem2mem_set_addrs(addr_pair)
log("3c. mem2mem_set_addrs(0x%08X)...", ADDR_PAIR)
fw_call(L2.set_addrs, ADDR_PAIR)
log("    done.")
log("")

-- 3d. mem2mem_set_geom_mode(geom_desc)
log("3d. mem2mem_set_geom_mode(0x%08X)...", GEOM_DESC)
fw_call(L2.set_geom_mode, GEOM_DESC)
log("    done.")
log("")

-- Dump MMIO state pre-trigger
log("--- Pre-trigger MMIO state ---")
dump_mmio("Write ch 0x3D", WR_MMIO)
dump_mmio("Read  ch 0x18", RD_MMIO)
log("")

-- 3e. mem2mem_start_connect() — TRIGGERS DMA!
log("3e. mem2mem_start_connect() — TRIGGERING DMA!")
fw_call(L2.start_connect)
log("    returned from start_connect")

-- Wait for completion
msleep(200)
log("    waited 200ms for completion")
log("")

-- Dump MMIO state post-trigger
log("--- Post-trigger MMIO state ---")
dump_mmio("Write ch 0x3D", WR_MMIO)
dump_mmio("Read  ch 0x18", RD_MMIO)
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 4: Verify data transfer
-----------------------------------------------------------------------
log("--- Step 4: Verification ---")
local match = 0
local first_fail = -1
for i = 0, XFER_SIZE/4 - 1 do
    local expected = 0xDEAD0000 + i
    local got = peek(DST_ADDR + i*4)
    if got == expected then
        match = match + 1
    elseif first_fail < 0 then
        first_fail = i
        log("  First mismatch at word %d: expected=0x%08X got=0x%08X",
            i, expected, got)
    end
end

log("")
log("  *** RESULT: %d/%d words match ***", match, XFER_SIZE/4)
log("")

-- Show first 16 destination words
log("  First 16 DST words:")
for i = 0, 15 do
    local val = peek(DST_ADDR + i*4)
    local exp = 0xDEAD0000 + i
    local mark = (val == exp) and "OK" or "FAIL"
    log("    [%02d] 0x%08X (expect 0x%08X) %s", i, val, exp, mark)
end
log("")

-----------------------------------------------------------------------
-- Step 5: Cleanup
-----------------------------------------------------------------------
log("--- Step 5: Cleanup ---")
fw_call(L2.reinit_struct)
log("  reinit_struct done")
fw_call(L2.cleanup_channels)
log("  cleanup_channels done")

-- Final MMIO state
log("")
log("--- Post-cleanup MMIO state ---")
dump_mmio("Write ch 0x3D", WR_MMIO)
dump_mmio("Read  ch 0x18", RD_MMIO)
dump_m2m_struct()
log("")

-----------------------------------------------------------------------
-- Step 6: Additional diagnostics
-----------------------------------------------------------------------
log("--- Step 6: Diagnostics ---")

-- Read Canon's geometry struct at 0x00011BCC (used by DarkCurCor)
log("  Canon geom struct at 0x00011BCC (DarkCurCor runtime):")
for i = 0, 14 do
    log("    +0x%02X: 0x%08X", i*4, peek(0x00011BCC + i*4))
end
log("")

-- EDMAC table entries for our channels
-- Table at 0xE0DD7BA0, each entry = {mmio_base(4), flags(4)}
-- These are in ROM so we can't peek them without shamem_read
-- But we can verify by reading known MMIO bases
log("  Channel 0x3D: expected MMIO=0xD0487600")
log("  Channel 0x18: expected MMIO=0xD0487100")
log("")

log("=== Test v4.2 complete ===")
flush_log()
log("Log saved to " .. LOG_FILE)
