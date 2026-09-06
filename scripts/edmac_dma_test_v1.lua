-- EDMAC DMA Copy Test for Canon M50 (DIGIC 8)
-- Single-channel mem-to-mem DMA via ch 0x29 (Mem2MemPath)
--
-- Theory: ch 0x29 has dual-address support (flags bit 11) and
-- mode 1 support (flags bit 10), making it a single-channel
-- mem-to-mem DMA engine: addr=src, addr2=dst.
--
-- MMIO register map (from ROM disassembly):
--   +0x00 = CMD/CTRL (write 0x80000000 to reset)
--   +0x08 = CMD status (0x11=idle, 0x03=active)
--   +0x20 = EN1 (set_enable writes 1)
--   +0x24 = EN2 (set_enable writes 1)
--   +0x3C = CBR registered (config writes 1)
--   +0x48 = GEOM: xb | (ya << 16), mask 0x7FFFFFFE
--   +0x4C = GEOM: xa | (yb << 16), mask 0x7FFFFFFE
--   +0x50 = GEOM: off1 pair, mask 0x7FFFFFFE
--   +0x54 = GEOM: off2 pair, mask 0x0FFF1FFF
--   +0x58..+0x70 = GEOM: secondary fields
--   +0xA0 = ADDR  (source address for DMA read)
--   +0xAC = ADDR2 (destination address for DMA write)
--   +0xB4 = STATUS
--   +0xC0 = MODE bits[1:0] (0=normal, 1=mem2mem?)
--   +0xDC = TRIGGER (write 1 to start)
--
-- Usage: Run from ML Scripts. Results logged to ML/LOGS/dma.log
-- WARNING: Experimental - may crash camera!

local LOG = "ML/LOGS/dma.log"

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

-- ELD function addresses (from stubs.S, even addresses - fw_call handles Thumb)
local ELD = {
    init         = 0xE054A30E,
    reset        = 0xE0549E8C,
    stop         = 0xE0549E52,
    clear_enable = 0xE054A4B2,
    set_enable   = 0xE054A4F2,
    set_addr     = 0xE054A9AE,
    set_addr2    = 0xE054AFA8,
    set_geom     = 0xE054A9BA,
    set_mode     = 0xE054AEB0,
    config       = 0xE0549F76,
    start        = 0xE054AF3C,
    trigger      = 0xE054BF18,
}

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
        -- Print in groups of 4
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
-- TEST A: Verify memory access at src/dst addresses
-----------------------------------------------------------------------
local function test_a_memory()
    log("=== TEST A: Verify memory access ===")

    -- Write test patterns
    log(string.format("  Writing test pattern to SRC 0x%08X...", SRC_ADDR))
    for i = 0, TEST_SIZE - 4, 4 do
        poke(SRC_ADDR + i, 0xCAFE0000 + i)
    end

    -- Zero destination
    log(string.format("  Zeroing DST 0x%08X...", DST_ADDR))
    for i = 0, TEST_SIZE - 4, 4 do
        poke(DST_ADDR + i, 0)
    end

    -- Verify writes
    local src_ok = true
    local dst_ok = true
    for i = 0, TEST_SIZE - 4, 4 do
        local sv = peek(SRC_ADDR + i)
        local dv = peek(DST_ADDR + i)
        if sv ~= 0xCAFE0000 + i then src_ok = false end
        if dv ~= 0 then dst_ok = false end
    end

    log(string.format("  SRC readback: %s", src_ok and "OK" or "FAIL"))
    log(string.format("  DST readback: %s", dst_ok and "OK" or "FAIL"))

    if not src_ok or not dst_ok then
        log("  TEST A FAILED - memory access broken")
        return false
    end

    -- Show first few words
    log(string.format("  SRC[0..3]: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(SRC_ADDR), peek(SRC_ADDR+4), peek(SRC_ADDR+8), peek(SRC_ADDR+12)))
    log(string.format("  DST[0..3]: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

    log("  TEST A PASSED")
    return true
end

-----------------------------------------------------------------------
-- TEST B: Setup ch 0x29 for DMA (no trigger yet)
-----------------------------------------------------------------------
local function test_b_setup()
    log("=== TEST B: Setup ch 0x29 for single-channel DMA ===")

    -- Step 1: Reset and init
    log("  Step 1: Reset + Init")
    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)
    dump_regs("after-reset")

    -- Step 2: Set geometry via direct MMIO writes
    -- For 256 bytes: xb=256 (0x100), ya=0, xa=0, yb=0, all offsets=0
    log("  Step 2: Set geometry (256 bytes, 1 line)")
    mmio_write(MMIO_BASE + 0x48, TEST_SIZE)   -- xb | (ya << 16)
    mmio_write(MMIO_BASE + 0x4C, 0)           -- xa | (yb << 16)
    mmio_write(MMIO_BASE + 0x50, 0)           -- off pair 1
    mmio_write(MMIO_BASE + 0x54, 0)           -- off pair 2
    mmio_write(MMIO_BASE + 0x58, 0)           -- flags
    mmio_write(MMIO_BASE + 0x5C, 0)           -- secondary 1
    mmio_write(MMIO_BASE + 0x60, 0)           -- secondary 2
    mmio_write(MMIO_BASE + 0x64, 0)           -- secondary 3
    mmio_write(MMIO_BASE + 0x68, 0)           -- secondary 4
    mmio_write(MMIO_BASE + 0x6C, 0)           -- secondary 5
    mmio_write(MMIO_BASE + 0x70, 0)           -- secondary 6

    -- Step 3: Set mode 1 (mem-to-mem, ch 0x29 supports this)
    log("  Step 3: Set mode 1")
    fw_call(ELD.set_mode, CH, 1)

    -- Step 4: Set addresses
    log(string.format("  Step 4: Set addr=0x%08X, addr2=0x%08X", SRC_ADDR, DST_ADDR))
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.set_addr2, CH, DST_ADDR)

    dump_regs("after-setup")

    -- Verify addr readback
    local addr_rb = mmio_read(MMIO_BASE + 0xA0)
    local addr2_rb = mmio_read(MMIO_BASE + 0xAC)
    log(string.format("  Addr readback:  0x%08X (expect 0x%08X) %s",
        addr_rb, SRC_ADDR, addr_rb == SRC_ADDR and "OK" or "MISMATCH"))
    log(string.format("  Addr2 readback: 0x%08X (expect 0x%08X) %s",
        addr2_rb, DST_ADDR,
        addr2_rb == DST_ADDR and "OK" or "(may be write-only)"))

    log("  TEST B PASSED (setup complete, NOT triggered)")
    return true
end

-----------------------------------------------------------------------
-- TEST C: Enable and Trigger DMA, check result
-----------------------------------------------------------------------
local function test_c_trigger()
    log("=== TEST C: Enable + Trigger DMA copy ===")

    -- Show pre-trigger state
    dump_regs("pre-trigger")
    log(string.format("  DST before: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

    -- Step 1: Enable
    log("  Step 1: set_enable")
    fw_call(ELD.set_enable, CH)

    -- Step 2: Read enable registers to confirm
    local en1 = mmio_read(MMIO_BASE + 0x20)
    local en2 = mmio_read(MMIO_BASE + 0x24)
    log(string.format("  Enable: EN1=%d, EN2=%d", en1, en2))

    -- Step 3: Trigger!
    log("  Step 2: TRIGGERING DMA...")
    fw_call(ELD.trigger, CH)

    -- Step 4: Wait and poll status
    log("  Step 3: Waiting for completion...")
    local status_before = mmio_read(MMIO_BASE + 0xB4)
    log(string.format("  Status immediately after trigger: 0x%X", status_before))

    -- Wait up to 500ms, polling status every 10ms
    local completed = false
    for i = 1, 50 do
        msleep(10)
        local st = mmio_read(MMIO_BASE + 0xB4)
        local cmd = mmio_read(MMIO_BASE + 0x08)
        if i <= 5 or i % 10 == 0 then
            log(string.format("  Poll %d: status=0x%X cmd=0x%X", i * 10, st, cmd))
        end
        -- Status 0x2 = idle/done (from our observations)
        if st == 0x2 and cmd == 0x11 then
            completed = true
            log(string.format("  DMA appears complete at %dms", i * 10))
            break
        end
    end

    -- Step 5: Read post-trigger registers
    dump_regs("post-trigger")

    -- Step 6: Check destination memory
    log("  Step 4: Checking destination memory...")
    log(string.format("  DST after: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))

    local match = 0
    local mismatch = 0
    for i = 0, TEST_SIZE - 4, 4 do
        local expected = 0xCAFE0000 + i
        local actual = peek(DST_ADDR + i)
        if actual == expected then
            match = match + 1
        else
            mismatch = mismatch + 1
            if mismatch <= 4 then
                log(string.format("    MISMATCH at +0x%02X: got 0x%08X, expected 0x%08X",
                    i, actual, expected))
            end
        end
    end

    local total = match + mismatch
    log(string.format("  Result: %d/%d words match (%d mismatches)",
        match, total, mismatch))

    if match == total then
        log("  *** DMA COPY SUCCEEDED! ***")
        return true
    elseif match > 0 then
        log("  PARTIAL COPY - DMA is working but geometry/size wrong")
        return false
    else
        log("  NO DATA COPIED - DMA did not transfer any data")
        return false
    end
end

-----------------------------------------------------------------------
-- TEST D: Alternative approach - try with eld_edmac_start instead of trigger
-----------------------------------------------------------------------
local function test_d_start_api()
    log("=== TEST D: Try eld_edmac_start API ===")

    -- Re-prepare memory
    for i = 0, TEST_SIZE - 4, 4 do
        poke(SRC_ADDR + i, 0xBEEF0000 + i)
        poke(DST_ADDR + i, 0)
    end

    -- Full setup
    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)

    -- Geometry (direct MMIO)
    mmio_write(MMIO_BASE + 0x48, TEST_SIZE)
    mmio_write(MMIO_BASE + 0x4C, 0)
    for off = 0x50, 0x70, 4 do
        mmio_write(MMIO_BASE + off, 0)
    end

    -- Mode, addresses
    fw_call(ELD.set_mode, CH, 1)
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.set_addr2, CH, DST_ADDR)

    -- Enable
    fw_call(ELD.set_enable, CH)

    -- Use start API (validates then triggers)
    log("  Calling eld_edmac_start(0x29)...")
    fw_call(ELD.start, CH)

    msleep(200)

    -- Check result
    local match = 0
    for i = 0, TEST_SIZE - 4, 4 do
        if peek(DST_ADDR + i) == 0xBEEF0000 + i then
            match = match + 1
        end
    end

    log(string.format("  Result: %d/%d words match", match, TEST_SIZE / 4))
    dump_regs("post-start")

    if match == TEST_SIZE / 4 then
        log("  *** DMA via start API SUCCEEDED! ***")
        return true
    else
        log("  DMA via start API did not copy data")

        -- Show DST contents
        log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
            peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
        return false
    end
end

-----------------------------------------------------------------------
-- TEST E: Different geometry - try xb-1 convention
-----------------------------------------------------------------------
local function test_e_geometry_variants()
    log("=== TEST E: Try geometry variants ===")

    local variants = {
        {name="xb=255(n-1)", xb=255, xa=0, yb=0, ya=0},
        {name="xb=256,yb=0", xb=256, xa=0, yb=0, ya=0},
        {name="xb=64,yb=3",  xb=64,  xa=0, yb=3, ya=0},  -- 64*4=256
        {name="xb=128,yb=1", xb=128, xa=0, yb=1, ya=0},   -- 128*2=256
    }

    for _, v in ipairs(variants) do
        log(string.format("  --- Trying: %s ---", v.name))

        -- Prepare memory
        for i = 0, TEST_SIZE - 4, 4 do
            poke(SRC_ADDR + i, 0xFACE0000 + i)
            poke(DST_ADDR + i, 0)
        end

        -- Setup
        fw_call(ELD.reset, CH)
        fw_call(ELD.init, CH)
        fw_call(ELD.clear_enable, CH)

        -- Geometry packing: MMIO+0x48 = xb | (ya << 16)
        --                   MMIO+0x4C = xa | (yb << 16)
        mmio_write(MMIO_BASE + 0x48, v.xb + v.ya * 0x10000)
        mmio_write(MMIO_BASE + 0x4C, v.xa + v.yb * 0x10000)
        for off = 0x50, 0x70, 4 do
            mmio_write(MMIO_BASE + off, 0)
        end

        fw_call(ELD.set_mode, CH, 1)
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
        log(string.format("    %d/%d match, status=0x%X cmd=0x%X",
            match, TEST_SIZE / 4, st, cmd))

        if match == TEST_SIZE / 4 then
            log(string.format("  *** GEOMETRY '%s' WORKS! ***", v.name))
            return true
        elseif match > 0 then
            log(string.format("    Partial: %d words copied", match))
            -- Show what was copied
            for i = 0, math.min(TEST_SIZE - 4, 63), 4 do
                local actual = peek(DST_ADDR + i)
                if actual ~= 0 then
                    log(string.format("      DST+0x%02X = 0x%08X", i, actual))
                end
            end
        end

        -- Cleanup
        fw_call(ELD.stop, CH)
        fw_call(ELD.reset, CH)
    end

    log("  TEST E: No geometry variant succeeded")
    return false
end

-----------------------------------------------------------------------
-- Cleanup: always reset channel on exit
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
    log("======================================")
    log("EDMAC DMA Copy Test - ")
    log("Channel: 0x29 (Mem2MemPath)")
    log(string.format("SRC: 0x%08X, DST: 0x%08X, SIZE: %d",
        SRC_ADDR, DST_ADDR, TEST_SIZE))
    log("======================================")

    -- Test A: Memory access
    if not test_a_memory() then
        log("ABORT: Memory access failed")
        cleanup()
        return
    end

    -- Test B: Setup (no trigger)
    if not test_b_setup() then
        log("ABORT: Setup failed")
        cleanup()
        return
    end

    -- Test C: Trigger and check
    local c_ok = test_c_trigger()
    cleanup()

    if c_ok then
        log("\n*** SINGLE-CHANNEL DMA WORKS! ***")
        log("Channel 0x29 confirmed as mem-to-mem DMA engine")
        return
    end

    -- Test D: Try start API instead of raw trigger
    log("")
    local d_ok = test_d_start_api()
    cleanup()

    if d_ok then
        log("\n*** DMA via start API WORKS! ***")
        return
    end

    -- Test E: Different geometry variants
    log("")
    local e_ok = test_e_geometry_variants()
    cleanup()

    if e_ok then
        log("\n*** DMA WORKS with different geometry! ***")
        return
    end

    log("\n=== DMA copy did not succeed ===")
    log("Possible issues:")
    log("  1. addr2 not functional (register may need config enablement)")
    log("  2. Need three-channel setup (src+dst+router) instead of single channel")
    log("  3. Geometry format is wrong")
    log("  4. Mode 1 doesn't mean mem-to-mem")
    log("  5. 0x4F000000 may not be DMA-accessible")
    log("=== End of test ===")
end

-- Run with error handling
local ok, err = pcall(main)
if not ok then
    log("SCRIPT ERROR: " .. tostring(err))
    pcall(cleanup)
end
