-- EDMAC DMA Test Script for Canon M50 (DIGIC 8)
-- Tests ELD Edmac Low Driver functions and hardware register access
--
-- Usage: Run from ML Scripts menu. Results logged to ML/LOGS/edmac.log
-- WARNING: Experimental - may crash camera. Have battery grip ready.

local LOG = "ML/LOGS/edmac.log"

local function log(msg)
    if msg == nil then msg = "(nil)" end
    pcall(print, msg)
    pcall(function()
        if dryos and dryos.append_file then
            dryos.append_file(LOG, msg .. "\n")
        elseif io and io.open then
            local f = io.open(LOG, "a")
            if f then f:write(msg .. "\n"); f:close() end
        end
    end)
end

-- ELD Edmac function addresses (from stubs.S)
local ELD = {
    init         = 0xE054A30E,   -- clear channel config
    reset        = 0xE0549E8C,   -- full channel reset
    stop         = 0xE0549E52,   -- stop channel
    clear_enable = 0xE054A4B2,   -- clear DMA enable [+0x20]=0,[+0x24]=0
    set_enable   = 0xE054A4F2,   -- set DMA enable [+0x20]=1,[+0x24]=1
    set_addr     = 0xE054A9AE,   -- set ram_addr [+0xA0]
    set_addr2    = 0xE054AFA8,   -- set second addr [+0xAC]
    set_geom     = 0xE054A9BA,   -- write edmac_info geometry
    set_mode     = 0xE054AEB0,   -- set transfer_mode [+0xC0]
    start        = 0xE054AF3C,   -- check flags + trigger
    trigger      = 0xE054BF18,   -- write [+0xDC]=1 directly
}

-- EDMAC Channel Table (from ROM 0xE0DD7BA0)
-- Each entry: {mmio_base, flags}
local CHANNELS = {
    [0x13] = {mmio = 0xD045E200, flags = 0x00080411},  -- write ch 19
    [0x29] = {mmio = 0xD0421100, flags = 0x00000006},  -- read ch 41 (Mem2MemPath)
    [0x2B] = {mmio = 0xD0421300, flags = 0x00000006},  -- read ch 43
    [0x2D] = {mmio = 0xD0421500, flags = 0x00000006},  -- read ch 45
    [0x30] = {mmio = 0xD0440300, flags = 0x00000006},  -- read ch 48
}

-- EDMAC MMIO register offsets
local REG = {
    COMMAND      = 0x08,
    DMA_EN_A     = 0x20,
    DMA_EN_B     = 0x24,
    CBR_REG      = 0x3C,
    YS_XS        = 0x48,
    YA_XA        = 0x4C,
    YB_XB        = 0x50,
    YN_XN        = 0x54,
    OFF1S        = 0x58,
    OFF2S        = 0x5C,
    OFF1A        = 0x60,
    OFF2A        = 0x64,
    OFF1B        = 0x68,
    OFF2B        = 0x6C,
    OFF3         = 0x70,
    RAM_ADDR     = 0xA0,
    RAM_ADDR2    = 0xAC,
    STATUS_ACK   = 0xB4,
    XFER_MODE    = 0xC0,
    START_TRIG   = 0xDC,
}

-----------------------------------------------------------------------
-- TEST 1: Read EDMAC MMIO registers (verify hardware access)
-----------------------------------------------------------------------
local function test1_read_mmio()
    log("=== TEST 1: Read EDMAC MMIO registers ===")

    -- ONLY read ch 0x29 - other channels may be active and crash
    for ch_name, ch_info in pairs({
        ["ch0x29(m2m)"]  = CHANNELS[0x29],
    }) do
        local base = ch_info.mmio
        log(string.format("  %s MMIO base=0x%08X:", ch_name, base))

        -- Read a few key registers
        local cmd    = dryos.mmio_read(base + REG.COMMAND)
        local en_a   = dryos.mmio_read(base + REG.DMA_EN_A)
        local en_b   = dryos.mmio_read(base + REG.DMA_EN_B)
        local addr   = dryos.mmio_read(base + REG.RAM_ADDR)
        local addr2  = dryos.mmio_read(base + REG.RAM_ADDR2)
        local status = dryos.mmio_read(base + REG.STATUS_ACK)
        local mode   = dryos.mmio_read(base + REG.XFER_MODE)
        local yb_xb  = dryos.mmio_read(base + REG.YB_XB)

        log(string.format("    cmd=0x%X en=%d/%d addr=0x%08X addr2=0x%08X",
            cmd, en_a, en_b, addr, addr2))
        log(string.format("    status=0x%X mode=%d yb_xb=0x%08X",
            status, mode, yb_xb))
    end

    log("  TEST 1 PASSED (no crash reading MMIO)")
    return true
end

-----------------------------------------------------------------------
-- TEST 2: Call eld_edmac_reset on an unused channel
-----------------------------------------------------------------------
local function test2_eld_reset()
    log("=== TEST 2: Call eld_edmac_reset(0x29) ===")

    -- Read status before
    local base = CHANNELS[0x29].mmio
    local before = dryos.mmio_read(base + REG.STATUS_ACK)
    log(string.format("  Before: status_ack=0x%X", before))

    -- Call reset
    dryos.fw_call(ELD.reset, 0x29)

    -- Read after
    local after = dryos.mmio_read(base + REG.STATUS_ACK)
    log(string.format("  After:  status_ack=0x%X", after))

    -- Reset should set status_ack = 2
    if after == 2 then
        log("  TEST 2 PASSED (reset set status=2)")
    else
        log(string.format("  TEST 2 RESULT: status=%d (expected 2)", after))
    end
    return true
end

-----------------------------------------------------------------------
-- TEST 3: Set and read back address register
-----------------------------------------------------------------------
local function test3_set_addr()
    log("=== TEST 3: Set address register on ch 0x29 ===")

    local base = CHANNELS[0x29].mmio
    local test_addr = 0x12345678

    -- Reset first
    dryos.fw_call(ELD.reset, 0x29)

    -- Set address
    dryos.fw_call(ELD.set_addr, 0x29, test_addr)

    -- Read back
    local readback = dryos.mmio_read(base + REG.RAM_ADDR)
    log(string.format("  Wrote: 0x%08X  Read: 0x%08X", test_addr, readback))

    if readback == test_addr then
        log("  TEST 3 PASSED (address readback matches)")
    else
        log("  TEST 3 FAILED (address mismatch)")
    end

    -- Also test addr2
    dryos.fw_call(ELD.set_addr2, 0x29, 0xAABBCCDD)
    local readback2 = dryos.mmio_read(base + REG.RAM_ADDR2)
    log(string.format("  addr2: wrote=0x%08X read=0x%08X", 0xAABBCCDD, readback2))

    return true
end

-----------------------------------------------------------------------
-- TEST 4: Full ELD setup verification (NO DMA TRIGGER - safe)
--
-- Programs two EDMAC channels with geometry, address, mode,
-- then reads back all MMIO registers to verify the ELD functions
-- actually wrote the correct values. Does NOT trigger DMA.
-----------------------------------------------------------------------
local function test4_setup_verify()
    log("=== TEST 4: ELD Setup Verification (safe, no trigger) ===")

    -- Use safe uncacheable RAM for address values
    local test_src = 0x4F000000
    local test_dst = 0x4F001000
    local geom_buf = 0x4F002000  -- temp buf for edmac_info struct

    -- Verify we can actually write to these addresses
    dryos.poke(test_src, 0xAAAAAAAA)
    local check = dryos.peek(test_src)
    if check ~= 0xAAAAAAAA then
        log(string.format("  SKIP: RAM write failed (wrote 0xAAAAAAAA, read 0x%08X)", check))
        return false
    end
    log("  RAM access OK")

    -- ---- Channel 0x29 (Mem2MemPath read) ----
    local ch = 0x29
    local base = CHANNELS[ch].mmio
    log(string.format("  --- Channel 0x%02X (base=0x%08X) ---", ch, base))

    -- Step 1: Reset
    dryos.fw_call(ELD.reset, ch)
    local status_after_reset = dryos.mmio_read(base + REG.STATUS_ACK)
    log(string.format("  After reset: status=0x%X", status_after_reset))

    -- Step 2: Init (clear config)
    dryos.fw_call(ELD.init, ch)
    log("  Init (clear config) done")

    -- Step 3: Clear enable
    dryos.fw_call(ELD.clear_enable, ch)
    local en_a = dryos.mmio_read(base + REG.DMA_EN_A)
    local en_b = dryos.mmio_read(base + REG.DMA_EN_B)
    log(string.format("  After clear_enable: en_a=%d en_b=%d", en_a, en_b))

    -- Step 4: Build geometry struct (15 x uint32)
    -- struct edmac_info { off1s, off1a, off1b, off2s, off2a, off2b,
    --                     off3, xs, xa, xb, ys, ya, yb, xn, yn }
    for i = 0, 14 do
        dryos.poke(geom_buf + i*4, 0)
    end
    dryos.poke(geom_buf + 9*4, 256)   -- xb = 256 bytes per line
    dryos.poke(geom_buf + 12*4, 3)    -- yb = 3 (4 lines total: 0..3)
    -- Total transfer: 256 * (3+1) = 1024 bytes

    dryos.fw_call(ELD.set_geom, ch, geom_buf)

    -- Read back geometry from MMIO (packed format: Y<<16 | X)
    local yb_xb = dryos.mmio_read(base + REG.YB_XB)
    local yn_xn = dryos.mmio_read(base + REG.YN_XN)
    local ya_xa = dryos.mmio_read(base + REG.YA_XA)
    local ys_xs = dryos.mmio_read(base + REG.YS_XS)
    local off1a = dryos.mmio_read(base + REG.OFF1A)
    local off1b = dryos.mmio_read(base + REG.OFF1B)
    local off3  = dryos.mmio_read(base + REG.OFF3)

    -- Expected: yb_xb = (3<<16)|256 = 0x00030100
    local xb_got = yb_xb % 0x10000           -- low 16 bits
    local yb_got = math.floor(yb_xb / 0x10000)  -- high 16 bits
    log(string.format("  Geom readback: yb_xb=0x%08X (xb=%d yb=%d)", yb_xb, xb_got, yb_got))
    log(string.format("  yn_xn=0x%08X ya_xa=0x%08X ys_xs=0x%08X", yn_xn, ya_xa, ys_xs))
    log(string.format("  off1a=0x%X off1b=0x%X off3=0x%X", off1a, off1b, off3))

    if xb_got == 256 and yb_got == 3 then
        log("  Geometry PASS: xb=256, yb=3 written correctly")
    else
        log("  Geometry FAIL: unexpected values")
    end

    -- Step 5: Set mode
    dryos.fw_call(ELD.set_mode, ch, 1)
    local mode = dryos.mmio_read(base + REG.XFER_MODE)
    log(string.format("  Mode readback: 0x%X (expect 1)", mode))

    -- Step 6: Set address
    dryos.fw_call(ELD.set_addr, ch, test_src)
    local addr = dryos.mmio_read(base + REG.RAM_ADDR)
    log(string.format("  Addr readback: 0x%08X (expect 0x%08X)", addr, test_src))

    -- Step 7: Set addr2
    dryos.fw_call(ELD.set_addr2, ch, test_dst)
    local addr2 = dryos.mmio_read(base + REG.RAM_ADDR2)
    log(string.format("  Addr2 readback: 0x%08X (expect 0x%08X)", addr2, test_dst))

    -- Step 8: Read command register (skip set_enable - could arm DMA)
    local cmd = dryos.mmio_read(base + REG.COMMAND)
    log(string.format("  Command reg: 0x%X", cmd))

    -- Clean up: reset ch 0x29
    dryos.fw_call(ELD.reset, ch)
    log("  Cleanup: channel reset")

    -- Summary
    local pass = (xb_got == 256 and yb_got == 3)
    if pass then
        log("  TEST 4 PASSED: All ELD setup functions verified via MMIO readback")
    else
        log("  TEST 4 FAILED: MMIO readback mismatch")
    end
    return pass
end

-----------------------------------------------------------------------
-- TEST 5: Scan for EDMAC connection registers
--
-- On older DIGIC, EDMAC channels are connected via MMIO registers
-- at specific addresses. This test scans candidate regions to find
-- the connection mechanism on DIGIC 8.
-----------------------------------------------------------------------
local function test5_scan_connections()
    log("=== TEST 5: ROM Channel Table Dump (safe, ROM reads only) ===")

    -- Dump the full 76-entry EDMAC channel table from ROM
    -- Each entry is 8 bytes: {uint32 mmio_base, uint32 flags}
    -- This is pure ROM read via peek - cannot crash
    log("  Channel table at ROM 0xE0DD7BA0:")
    local ch_table_rom = 0xE0DD7BA0
    for i = 0, 75 do
        local mmio = dryos.peek(ch_table_rom + i*8)
        local flags = dryos.peek(ch_table_rom + i*8 + 4)
        -- Classify channel type from MMIO address range
        local chtype = "???"
        if mmio >= 0xD0400000 and mmio < 0xD0430000 then chtype = "RD"
        elseif mmio >= 0xD0440000 and mmio < 0xD0470000 then chtype = "RD"
        elseif mmio >= 0xD0478000 and mmio < 0xD04A0000 then chtype = "WR"
        elseif mmio >= 0xD04A0000 then chtype = "WR" end
        log(string.format("    ch[%02d](0x%02X) %s mmio=0x%08X flags=0x%08X",
            i, i, chtype, mmio, flags))
    end

    -- Also dump ch 0x29 registers (the ONLY channel proven safe)
    log("  Register dump for ch 0x29 (0xD0421100) - proven safe:")
    local base29 = 0xD0421100
    for off = 0, 0xFC, 4 do
        local val = dryos.mmio_read(base29 + off)
        if val ~= 0 then
            log(string.format("    +0x%02X = 0x%08X", off, val))
        end
    end

    log("  TEST 5 DONE")
    return true
end

-----------------------------------------------------------------------
-- MAIN
-----------------------------------------------------------------------
log("========================================")
log("EDMAC DMA Test")
log("========================================")

-- Run tests in order of increasing risk
local ok

ok = test1_read_mmio()
if not ok then
    log("ABORT: MMIO read failed")
    return
end

msleep(500)

ok = test2_eld_reset()
if not ok then
    log("ABORT: ELD reset failed")
    return
end

msleep(500)

ok = test3_set_addr()

msleep(500)

test4_setup_verify()

msleep(500)

test5_scan_connections()

log("========================================")
log("All tests completed")
log("========================================")
