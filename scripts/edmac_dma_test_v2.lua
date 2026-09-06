-- EDMAC DMA Copy Test v2 for Canon M50 (DIGIC 8)
-- Single-channel mem-to-mem DMA via ch 0x29 (Mem2MemPath)
--
-- KEY FIX from v1: We now call eld_edmac_config() to set CBR=1 at MMIO+0x3C.
-- Without CBR=1, the hardware ignores trigger writes.
--
-- eld_edmac_init() CLEARS CBR to 0 and deregisters the ISR.
-- eld_edmac_config() SETS CBR to 1 and registers the ISR.
-- eld_edmac_start() checks ROM flags bit 16 - ch 0x29 LACKS this bit,
--   so eld_edmac_start ALWAYS asserts on ch 0x29. Use raw trigger instead.
--
-- Proper sequence: reset -> init -> config -> geometry -> mode -> addr -> enable -> trigger
--
-- MMIO register map (from ROM disassembly):
--   +0x00 = CMD/CTRL (write 0x80000000 to reset)
--   +0x08 = CMD status (0x11=idle, 0x03=active)
--   +0x20 = EN1 (set_enable writes 1)
--   +0x24 = EN2 (set_enable writes 1)
--   +0x3C = CBR registered flag (config writes 1, init clears to 0)
--   +0x48 = GEOM: xb | (ya << 16), mask 0x7FFFFFFE
--   +0x4C = GEOM: xa | (yb << 16), mask 0x7FFFFFFE
--   +0x50 = GEOM: off1 pair
--   +0x54 = GEOM: off2 pair
--   +0x58..+0x70 = GEOM: secondary fields
--   +0xA0 = ADDR  (source address for DMA read)
--   +0xAC = ADDR2 (destination address for DMA write)
--   +0xB4 = STATUS
--   +0xC0 = MODE bits[1:0] (0=normal, 1=mem2mem?)
--   +0xDC = TRIGGER (write 1 to start)
--
-- Usage: Run from ML Scripts. Results logged to ML/LOGS/dma2.log
-- WARNING: Experimental - may crash camera!

local LOG = "ML/LOGS/dma2.log"

local function log(msg)
    if msg == nil then msg = "(nil)" end
    pcall(print, msg)
    pcall(function()
        if io and io.open then
            local f = io.open(LOG, "a")
            if f then f:write(msg .. "\n"); f:close() end
        end
    end)
end

local function mmio_read(addr)
    return dryos.mmio_read(addr)
end

local function mmio_write(addr, val)
    dryos.mmio_write(addr, val)
end

local function fw_call(addr, ...)
    return dryos.fw_call(addr, ...)
end

local function peek(addr)
    return dryos.peek(addr)
end

local function poke(addr, val)
    dryos.poke(addr, val)
end

-- ELD function addresses (from stubs.S, even addresses - fw_call adds Thumb bit)
local ELD = {
    init         = 0xE054A30E,  -- clears CBR, deregisters ISR
    reset        = 0xE0549E8C,  -- hardware reset
    stop         = 0xE0549E52,  -- stop channel
    clear_enable = 0xE054A4B2,  -- clear EN1/EN2
    set_enable   = 0xE054A4F2,  -- set EN1=EN2=1
    set_addr     = 0xE054A9AE,  -- write source addr to MMIO+0xA0
    set_addr2    = 0xE054AFA8,  -- write dest addr to MMIO+0xAC
    set_geom     = 0xE054A9BA,  -- write geometry struct to MMIO+0x48..+0x70
    set_mode     = 0xE054AEB0,  -- write mode bits[1:0] to MMIO+0xC0
    config       = 0xE0549F76,  -- set CBR=1, register ISR: config(ch, callback, mode)
    start        = 0xE054AF3C,  -- NEVER use on ch 0x29! asserts on missing bit 16
    trigger      = 0xE054BF18,  -- write 1 to MMIO+0xDC (raw trigger)
}

-- Default callback: bx lr (return immediately) at 0xE0549FA0
-- This is what eld_edmac_init sets as the default. Safe to use.
local DEFAULT_CALLBACK = 0xE0549FA0

-- Config mode values seen in Canon code: 0x08, 0x10, 0x20
-- These determine ISR callback dispatch type. 0x20 = primary/complete.
local CONFIG_MODE = 0x20

local CH = 0x29
local MMIO_BASE = 0xD0421100

-- Uncached DRAM addresses for DMA test
local SRC_ADDR = 0x4F000000
local DST_ADDR = 0x4F001000
local TEST_SIZE = 256  -- bytes

-----------------------------------------------------------------------
-- Dump all relevant MMIO registers for ch 0x29
-----------------------------------------------------------------------
local function dump_regs(label)
    log(string.format("  [%s] ch 0x%02X registers:", label, CH))
    local offsets = {
        {0x00, "CTRL"},   {0x04, "?04"},    {0x08, "CMD"},
        {0x0C, "?0C"},    {0x18, "?18"},    {0x20, "EN1"},
        {0x24, "EN2"},    {0x3C, "CBR"},    {0x48, "GEO1"},
        {0x4C, "GEO2"},   {0x50, "GEO3"},   {0x54, "GEO4"},
        {0x58, "GEO5"},   {0x5C, "GEO6"},   {0x60, "GEO7"},
        {0x64, "GEO8"},   {0x68, "GEO9"},   {0x6C, "GEOA"},
        {0x70, "GEOB"},   {0xA0, "ADDR"},   {0xAC, "ADR2"},
        {0xB4, "STAT"},   {0xC0, "MODE"},   {0xC4, "?C4"},
        {0xD8, "?D8"},    {0xDC, "TRIG"},
    }
    local parts = {}
    for _, reg in ipairs(offsets) do
        local val = mmio_read(MMIO_BASE + reg[1])
        if val ~= 0 then
            table.insert(parts, string.format("+%02X(%s)=0x%X", reg[1], reg[2], val))
        end
    end
    if #parts == 0 then
        log("    (all zero)")
    else
        for i = 1, #parts, 4 do
            local line = "    "
            for j = i, math.min(i+3, #parts) do
                line = line .. parts[j] .. "  "
            end
            log(line)
        end
    end
end

-----------------------------------------------------------------------
-- Helper: prepare src/dst memory
-----------------------------------------------------------------------
local function prepare_memory(pattern_base)
    for i = 0, TEST_SIZE - 4, 4 do
        poke(SRC_ADDR + i, pattern_base + i)
        poke(DST_ADDR + i, 0)
    end
end

-----------------------------------------------------------------------
-- Helper: check if destination matches source pattern
-----------------------------------------------------------------------
local function check_result(pattern_base)
    local match = 0
    local mismatch = 0
    for i = 0, TEST_SIZE - 4, 4 do
        local expected = pattern_base + i
        local actual = peek(DST_ADDR + i)
        if actual == expected then
            match = match + 1
        else
            mismatch = mismatch + 1
            if mismatch <= 4 then
                log(string.format("    MISMATCH +0x%02X: got 0x%08X, expect 0x%08X",
                    i, actual, expected))
            end
        end
    end
    local total = match + mismatch
    log(string.format("  Result: %d/%d words match (%d mismatches)",
        match, total, mismatch))
    return match == total
end

-----------------------------------------------------------------------
-- TEST A: Verify memory access at src/dst addresses
-----------------------------------------------------------------------
local function test_a_memory()
    log("=== TEST A: Verify memory access ===")
    prepare_memory(0xCAFE0000)

    local src_ok = true
    local dst_ok = true
    for i = 0, TEST_SIZE - 4, 4 do
        if peek(SRC_ADDR + i) ~= 0xCAFE0000 + i then src_ok = false end
        if peek(DST_ADDR + i) ~= 0 then dst_ok = false end
    end
    log(string.format("  SRC readback: %s", src_ok and "OK" or "FAIL"))
    log(string.format("  DST readback: %s", dst_ok and "OK" or "FAIL"))
    if not src_ok or not dst_ok then
        log("  TEST A FAILED")
        return false
    end
    log(string.format("  SRC[0..3]: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(SRC_ADDR), peek(SRC_ADDR+4), peek(SRC_ADDR+8), peek(SRC_ADDR+12)))
    log("  TEST A PASSED")
    return true
end

-----------------------------------------------------------------------
-- TEST B: Setup ch 0x29 with proper eld_edmac_config (CBR=1)
-- This is the key fix: config() sets MMIO+0x3C = 1
-----------------------------------------------------------------------
local function test_b_setup_with_config()
    log("=== TEST B: Setup ch 0x29 WITH eld_edmac_config ===")
    prepare_memory(0xDEAD0000)

    -- Step 1: Reset + Init (clears CBR)
    log("  Step 1: reset + init")
    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)

    -- Verify CBR is 0 after init
    local cbr_after_init = mmio_read(MMIO_BASE + 0x3C)
    log(string.format("  CBR after init: %d (expect 0)", cbr_after_init))

    -- Step 2: Call eld_edmac_config to set CBR=1 and register ISR
    -- config(channel, callback_fn, config_mode)
    log(string.format("  Step 2: eld_edmac_config(0x%02X, 0x%08X, 0x%02X)",
        CH, DEFAULT_CALLBACK, CONFIG_MODE))
    fw_call(ELD.config, CH, DEFAULT_CALLBACK, CONFIG_MODE)

    -- Verify CBR is now 1
    local cbr_after_config = mmio_read(MMIO_BASE + 0x3C)
    log(string.format("  CBR after config: %d (expect 1)", cbr_after_config))

    -- Step 3: Set geometry (256 bytes, 1 line)
    log("  Step 3: geometry (xb=256)")
    mmio_write(MMIO_BASE + 0x48, TEST_SIZE)
    mmio_write(MMIO_BASE + 0x4C, 0)
    for off = 0x50, 0x70, 4 do
        mmio_write(MMIO_BASE + off, 0)
    end

    -- Step 4: Mode 1 (mem-to-mem)
    log("  Step 4: set_mode(1)")
    fw_call(ELD.set_mode, CH, 1)

    -- Step 5: Set addresses
    log(string.format("  Step 5: addr=0x%08X, addr2=0x%08X", SRC_ADDR, DST_ADDR))
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.set_addr2, CH, DST_ADDR)

    dump_regs("after-setup")
    log("  TEST B PASSED (setup complete)")
    return true
end

-----------------------------------------------------------------------
-- TEST C: Enable + Trigger with CBR=1
-----------------------------------------------------------------------
local function test_c_trigger_with_config()
    log("=== TEST C: Enable + Trigger (CBR=1) ===")

    log(string.format("  DST before: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

    -- Step 1: Enable
    log("  Step 1: set_enable")
    fw_call(ELD.set_enable, CH)
    local en1 = mmio_read(MMIO_BASE + 0x20)
    local en2 = mmio_read(MMIO_BASE + 0x24)
    log(string.format("  EN1=%d EN2=%d CBR=%d", en1, en2, mmio_read(MMIO_BASE + 0x3C)))

    -- Step 2: Trigger
    log("  Step 2: TRIGGER!")
    fw_call(ELD.trigger, CH)

    -- Step 3: Poll status
    log("  Step 3: polling...")
    local status_imm = mmio_read(MMIO_BASE + 0xB4)
    local cmd_imm = mmio_read(MMIO_BASE + 0x08)
    log(string.format("  Immediate: status=0x%X cmd=0x%X", status_imm, cmd_imm))

    local completed = false
    for i = 1, 50 do
        msleep(10)
        local st = mmio_read(MMIO_BASE + 0xB4)
        local cmd = mmio_read(MMIO_BASE + 0x08)
        if i <= 5 or i % 10 == 0 then
            log(string.format("  Poll %dms: status=0x%X cmd=0x%X", i * 10, st, cmd))
        end
        if st == 0x2 and cmd == 0x11 then
            completed = true
            log(string.format("  DMA maybe complete at %dms", i * 10))
            break
        end
    end

    dump_regs("post-trigger")

    -- Step 4: Check result
    log("  Step 4: checking destination...")
    log(string.format("  DST after: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

    local ok = check_result(0xDEAD0000)
    if ok then
        log("  *** TEST C: DMA COPY SUCCEEDED! ***")
    else
        log("  TEST C: DMA did not copy data")
    end
    return ok
end

-----------------------------------------------------------------------
-- TEST D: Try just manual CBR=1 without full config call
-----------------------------------------------------------------------
local function test_d_manual_cbr()
    log("=== TEST D: Manual CBR=1 (no eld_edmac_config) ===")
    prepare_memory(0xBEEF0000)

    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)

    -- Manually set CBR=1 via MMIO
    log("  Writing MMIO+0x3C = 1 (manual CBR)")
    mmio_write(MMIO_BASE + 0x3C, 1)
    local cbr = mmio_read(MMIO_BASE + 0x3C)
    log(string.format("  CBR readback: %d", cbr))

    mmio_write(MMIO_BASE + 0x48, TEST_SIZE)
    mmio_write(MMIO_BASE + 0x4C, 0)
    for off = 0x50, 0x70, 4 do
        mmio_write(MMIO_BASE + off, 0)
    end

    fw_call(ELD.set_mode, CH, 1)
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.set_addr2, CH, DST_ADDR)
    fw_call(ELD.set_enable, CH)
    log("  TRIGGER!")
    fw_call(ELD.trigger, CH)

    msleep(200)

    dump_regs("post-manual-cbr")
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

    local ok = check_result(0xBEEF0000)
    if ok then
        log("  *** TEST D: DMA with manual CBR SUCCEEDED! ***")
    else
        log("  TEST D: Manual CBR did not help")
    end
    return ok
end

-----------------------------------------------------------------------
-- TEST E: Try different config modes and geometry
-----------------------------------------------------------------------
local function test_e_variants()
    log("=== TEST E: Config mode and geometry variants ===")

    local variants = {
        {name="cm=0x08", config_mode=0x08, xb=TEST_SIZE, yb=0, hw_mode=1},
        {name="cm=0x10", config_mode=0x10, xb=TEST_SIZE, yb=0, hw_mode=1},
        {name="xb=255(n-1)", config_mode=0x20, xb=TEST_SIZE-1, yb=0, hw_mode=1},
        {name="64x4lines", config_mode=0x20, xb=64, yb=3, hw_mode=1},
        {name="hw_mode=0", config_mode=0x20, xb=TEST_SIZE, yb=0, hw_mode=0},
    }

    for _, v in ipairs(variants) do
        log(string.format("  --- Variant: %s ---", v.name))
        prepare_memory(0xFACE0000)

        fw_call(ELD.reset, CH)
        fw_call(ELD.init, CH)
        fw_call(ELD.clear_enable, CH)
        fw_call(ELD.config, CH, DEFAULT_CALLBACK, v.config_mode)

        mmio_write(MMIO_BASE + 0x48, v.xb)
        mmio_write(MMIO_BASE + 0x4C, v.yb * 0x10000)
        for off = 0x50, 0x70, 4 do
            mmio_write(MMIO_BASE + off, 0)
        end

        fw_call(ELD.set_mode, CH, v.hw_mode)
        fw_call(ELD.set_addr, CH, SRC_ADDR)
        fw_call(ELD.set_addr2, CH, DST_ADDR)
        fw_call(ELD.set_enable, CH)
        fw_call(ELD.trigger, CH)

        msleep(200)

        local match = 0
        for i = 0, TEST_SIZE - 4, 4 do
            if peek(DST_ADDR + i) == 0xFACE0000 + i then
                match = match + 1
            end
        end

        local st = mmio_read(MMIO_BASE + 0xB4)
        local cmd = mmio_read(MMIO_BASE + 0x08)
        local cbr = mmio_read(MMIO_BASE + 0x3C)
        log(string.format("    %d/%d match, stat=0x%X cmd=0x%X cbr=%d",
            match, TEST_SIZE / 4, st, cmd, cbr))

        if match == TEST_SIZE / 4 then
            log(string.format("  *** VARIANT '%s' WORKS! ***", v.name))
            dump_regs("working-variant")
            return true
        elseif match > 0 then
            log(string.format("    Partial: %d words", match))
        end

        log(string.format("    DST: 0x%08X 0x%08X 0x%08X 0x%08X",
            peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

        fw_call(ELD.stop, CH)
        fw_call(ELD.reset, CH)
    end

    log("  TEST E: No variant succeeded")
    return false
end

-----------------------------------------------------------------------
-- TEST F: Read config_table to see what Canon has for ch 0x29
-----------------------------------------------------------------------
local function test_f_config_table()
    log("=== TEST F: Read runtime config/state tables ===")

    local config_base = 0x000982F8
    for _, c in ipairs({0x13, 0x28, 0x29, 0x2A, 0x30, 0x34, 0x35}) do
        local off = config_base + c * 8
        local cb = peek(off)
        local m = peek(off + 4)
        log(string.format("  ch 0x%02X: cb=0x%08X mode=0x%08X", c, cb, m))
    end

    log("  TEST F DONE (informational)")
    return true
end

-----------------------------------------------------------------------
-- Cleanup
-----------------------------------------------------------------------
local function cleanup()
    log("  Cleanup: resetting ch 0x29")
    pcall(function() fw_call(ELD.stop, CH) end)
    pcall(function() fw_call(ELD.reset, CH) end)
    pcall(function() fw_call(ELD.clear_enable, CH) end)
end

-----------------------------------------------------------------------
-- Main
-----------------------------------------------------------------------
local function main()
    log("==========================================")
    log("EDMAC DMA Copy Test v2")
    log("Channel: 0x29 (Mem2MemPath)")
    log(string.format("SRC: 0x%08X, DST: 0x%08X, SIZE: %d",
        SRC_ADDR, DST_ADDR, TEST_SIZE))
    log("KEY FIX: Now calling eld_edmac_config (CBR=1)")
    log("==========================================")

    -- Test F first: read current config state
    test_f_config_table()
    log("")

    -- Test A: Memory access
    if not test_a_memory() then
        log("ABORT: Memory access failed")
        cleanup()
        return
    end
    log("")

    -- Test B: Setup with config (CBR=1)
    if not test_b_setup_with_config() then
        log("ABORT: Setup failed")
        cleanup()
        return
    end

    -- Test C: Trigger with CBR=1
    local c_ok = test_c_trigger_with_config()
    cleanup()

    if c_ok then
        log("")
        log("*** SINGLE-CHANNEL DMA WORKS WITH CONFIG! ***")
        log("Sequence: reset -> init -> config -> geom -> mode -> addr -> enable -> trigger")
        return
    end

    -- Test D: Manual CBR=1 without config call
    log("")
    local d_ok = test_d_manual_cbr()
    cleanup()

    if d_ok then
        log("")
        log("*** DMA works with manual CBR=1! ***")
        return
    end

    -- Test E: Different modes and geometries
    log("")
    local e_ok = test_e_variants()
    cleanup()

    if e_ok then
        log("")
        log("*** DMA WORKS with different variant! ***")
        return
    end

    -- Read config table after tests
    log("")
    test_f_config_table()

    log("")
    log("=== DMA copy did not succeed ===")
    log("Possible remaining issues:")
    log("  1. Mode 1 doesn't mean mem-to-mem for ch 0x29")
    log("  2. Need three-channel setup (read+write+router)")
    log("  3. 0x4F000000 not DMA-accessible (try shoot_malloc)")
    log("  4. Geometry xb encoding is wrong")
    log("  5. Need EfmErscLockResources for channel")
    log("=== End of test v2 ===")
end

-- Run with error handling
local ok, err = pcall(main)
if not ok then
    log("SCRIPT ERROR: " .. tostring(err))
    pcall(cleanup)
end
