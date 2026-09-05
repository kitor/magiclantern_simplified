-- EDMAC DMA Copy Test v3 for Canon M50 (DIGIC 8)
-- Single-channel mem-to-mem DMA via ch 0x29
--
-- KEY FIX from v2: Canon's code calls 0xE054AF96 before set_addr2.
-- This function sets bit 6 (0x40) in MODE register (MMIO+0xC0),
-- which ENABLES the addr2 destination path. Without it, addr2 is ignored.
--
-- Canon's actual mem2mem sequence (from 0xE083DFBC):
--   1. eld_edmac_config(ch, callback, mode)    -- register ISR, CBR=1
--   2. eld_edmac_set_addr(ch, src_addr)         -- MMIO+0xA0
--   3. eld_edmac_enable_addr2(ch)               -- bit 6 in MMIO+0xC0
--   4. eld_edmac_set_addr2(ch, dst_addr)        -- MMIO+0xAC
--   5. eld_edmac_set_geom(ch, geom_struct)      -- MMIO+0x48..+0x70
--   6. eld_edmac_set_enable(ch)                 -- EN1=EN2=1
--   7. eld_edmac_trigger(ch)                    -- MMIO+0xDC=1
--
-- Usage: Run from ML Scripts. Results logged to ML/LOGS/dma3.log
-- WARNING: Experimental - may crash camera!

local LOG = "ML/LOGS/dma3.log"

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

-- ELD function addresses (even addresses - fw_call handles Thumb)
local ELD = {
    init          = 0xE054A30E,  -- clears CBR, deregisters ISR
    reset         = 0xE0549E8C,  -- hardware reset
    stop          = 0xE0549E52,  -- stop channel
    clear_enable  = 0xE054A4B2,  -- clear EN1/EN2
    set_enable    = 0xE054A4F2,  -- set EN1=EN2=1
    set_addr      = 0xE054A9AE,  -- write src addr to MMIO+0xA0
    set_addr2     = 0xE054AFA8,  -- write dst addr to MMIO+0xAC
    enable_addr2  = 0xE054AF96,  -- set bit 6 (0x40) in MODE register
    set_geom      = 0xE054A9BA,  -- write geometry to MMIO+0x48..+0x70
    set_mode      = 0xE054AEB0,  -- write mode bits[1:0] to MMIO+0xC0
    config        = 0xE0549F76,  -- register ISR, set CBR=1
    trigger       = 0xE054BF18,  -- write 1 to MMIO+0xDC
}

-- Default dummy callback (bx lr) from Canon's init code
local DEFAULT_CALLBACK = 0xE0549FA0
local CONFIG_MODE = 0x20

local CH = 0x29
local MMIO_BASE = 0xD0421100

local SRC_ADDR = 0x4F000000
local DST_ADDR = 0x4F001000
local TEST_SIZE = 256  -- bytes

-----------------------------------------------------------------------
-- Dump MMIO registers (non-zero only)
-----------------------------------------------------------------------
local function dump_regs(label)
    log(string.format("  [%s] ch 0x%02X:", label, CH))
    local offsets = {
        {0x00, "CTRL"},  {0x04, "?04"},   {0x08, "CMD"},
        {0x0C, "?0C"},   {0x18, "?18"},   {0x20, "EN1"},
        {0x24, "EN2"},   {0x3C, "CBR"},   {0x48, "GEO1"},
        {0x4C, "GEO2"},  {0x50, "GEO3"},  {0x54, "GEO4"},
        {0x58, "GEO5"},  {0x5C, "GEO6"},  {0x60, "GEO7"},
        {0x64, "GEO8"},  {0x68, "GEO9"},  {0x6C, "GEOA"},
        {0x70, "GEOB"},  {0xA0, "ADDR"},  {0xAC, "ADR2"},
        {0xB4, "STAT"},  {0xC0, "MODE"},  {0xC4, "?C4"},
        {0xD8, "?D8"},   {0xDC, "TRIG"},
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
-- Prepare memory
-----------------------------------------------------------------------
local function prepare_memory(pattern_base)
    for i = 0, TEST_SIZE - 4, 4 do
        poke(SRC_ADDR + i, pattern_base + i)
        poke(DST_ADDR + i, 0)
    end
end

-----------------------------------------------------------------------
-- Check DMA result
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
                log(string.format("    MISMATCH +0x%02X: got 0x%08X expect 0x%08X",
                    i, actual, expected))
            end
        end
    end
    local total = match + mismatch
    log(string.format("  Result: %d/%d words match", match, total))
    return match == total
end

-----------------------------------------------------------------------
-- TEST A: Memory access verification
-----------------------------------------------------------------------
local function test_a()
    log("=== TEST A: Verify memory access ===")
    prepare_memory(0xCAFE0000)
    local src_ok = true
    local dst_ok = true
    for i = 0, TEST_SIZE - 4, 4 do
        if peek(SRC_ADDR + i) ~= 0xCAFE0000 + i then src_ok = false end
        if peek(DST_ADDR + i) ~= 0 then dst_ok = false end
    end
    log(string.format("  SRC: %s  DST: %s",
        src_ok and "OK" or "FAIL", dst_ok and "OK" or "FAIL"))
    if not src_ok or not dst_ok then return false end
    log("  TEST A PASSED")
    return true
end

-----------------------------------------------------------------------
-- TEST B: Canon-style DMA with enable_addr2 (THE KEY FIX)
-- Follows Canon's exact sequence from 0xE083DFBC
-----------------------------------------------------------------------
local function test_b_canon_style()
    log("=== TEST B: Canon-style DMA (enable_addr2 bit 6) ===")
    prepare_memory(0xDEAD0000)

    -- Step 1: Reset + Init + Clear
    log("  Step 1: reset + init + clear_enable")
    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)

    -- Step 2: Config (ISR + CBR=1)
    log("  Step 2: eld_edmac_config")
    fw_call(ELD.config, CH, DEFAULT_CALLBACK, CONFIG_MODE)

    -- Step 3: Set source address
    log(string.format("  Step 3: set_addr(src=0x%08X)", SRC_ADDR))
    fw_call(ELD.set_addr, CH, SRC_ADDR)

    -- Step 4: Enable addr2 mode (SET BIT 6 IN MODE REGISTER)
    log("  Step 4: enable_addr2 (set MODE bit 6)")
    fw_call(ELD.enable_addr2, CH)

    -- Verify MODE register now has bit 6
    local mode_val = mmio_read(MMIO_BASE + 0xC0)
    log(string.format("  MODE after enable_addr2: 0x%X (expect bit 6 = 0x40)", mode_val))

    -- Step 5: Set destination address
    log(string.format("  Step 5: set_addr2(dst=0x%08X)", DST_ADDR))
    fw_call(ELD.set_addr2, CH, DST_ADDR)

    -- Step 6: Geometry (256 bytes as single line)
    log("  Step 6: geometry (xb=256, single line)")
    mmio_write(MMIO_BASE + 0x48, TEST_SIZE)  -- xb | (ya << 16)
    mmio_write(MMIO_BASE + 0x4C, 0)          -- xa | (yb << 16)
    for off = 0x50, 0x70, 4 do
        mmio_write(MMIO_BASE + off, 0)
    end

    dump_regs("after-setup")

    -- Step 7: Enable
    log("  Step 7: set_enable")
    fw_call(ELD.set_enable, CH)

    -- Step 8: Trigger
    log("  Step 8: TRIGGER!")
    fw_call(ELD.trigger, CH)

    -- Step 9: Poll
    local status_imm = mmio_read(MMIO_BASE + 0xB4)
    local cmd_imm = mmio_read(MMIO_BASE + 0x08)
    log(string.format("  Immediate: status=0x%X cmd=0x%X", status_imm, cmd_imm))

    for i = 1, 50 do
        msleep(10)
        local st = mmio_read(MMIO_BASE + 0xB4)
        local cmd = mmio_read(MMIO_BASE + 0x08)
        if i <= 5 or i % 10 == 0 then
            log(string.format("  Poll %dms: status=0x%X cmd=0x%X", i * 10, st, cmd))
        end
        if st == 0x2 and cmd == 0x11 then
            log(string.format("  Complete at %dms", i * 10))
            break
        end
    end

    dump_regs("post-trigger")

    -- Check
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xDEAD0000)
end

-----------------------------------------------------------------------
-- TEST C: Try with set_mode(1) BEFORE enable_addr2
-- (mode 1 sets bits[1:0], enable_addr2 sets bit 6 - they're independent)
-----------------------------------------------------------------------
local function test_c_mode1_plus_addr2()
    log("=== TEST C: set_mode(1) + enable_addr2 (MODE=0x41) ===")
    prepare_memory(0xBEEF0000)

    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)
    fw_call(ELD.config, CH, DEFAULT_CALLBACK, CONFIG_MODE)

    -- Set hardware mode 1 first
    fw_call(ELD.set_mode, CH, 1)
    local mode_before = mmio_read(MMIO_BASE + 0xC0)
    log(string.format("  MODE after set_mode(1): 0x%X", mode_before))

    -- Then enable addr2 (bit 6)
    fw_call(ELD.enable_addr2, CH)
    local mode_after = mmio_read(MMIO_BASE + 0xC0)
    log(string.format("  MODE after enable_addr2: 0x%X (expect 0x41)", mode_after))

    -- Source and destination
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.set_addr2, CH, DST_ADDR)

    -- Geometry
    mmio_write(MMIO_BASE + 0x48, TEST_SIZE)
    mmio_write(MMIO_BASE + 0x4C, 0)
    for off = 0x50, 0x70, 4 do
        mmio_write(MMIO_BASE + off, 0)
    end

    -- Enable + Trigger
    fw_call(ELD.set_enable, CH)
    fw_call(ELD.trigger, CH)

    msleep(200)
    dump_regs("post-c")

    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xBEEF0000)
end

-----------------------------------------------------------------------
-- TEST D: Geometry variants with addr2 enabled
-----------------------------------------------------------------------
local function test_d_geom_variants()
    log("=== TEST D: Geometry variants (with addr2 enabled) ===")

    local variants = {
        {name="xb=256",        xb=256, xa=0, yb=0, ya=0},
        {name="xb=255(n-1)",   xb=255, xa=0, yb=0, ya=0},
        {name="xb=64,yb=3",    xb=64,  xa=0, yb=3, ya=0},
        {name="xb=128,yb=1",   xb=128, xa=0, yb=1, ya=0},
        {name="xb=254(n-2)",   xb=254, xa=0, yb=0, ya=0},
        {name="xb=256,xa=256", xb=256, xa=256, yb=0, ya=0},
    }

    for _, v in ipairs(variants) do
        log(string.format("  --- %s ---", v.name))
        prepare_memory(0xFACE0000)

        fw_call(ELD.reset, CH)
        fw_call(ELD.init, CH)
        fw_call(ELD.clear_enable, CH)
        fw_call(ELD.config, CH, DEFAULT_CALLBACK, CONFIG_MODE)

        -- Addresses with addr2 enable
        fw_call(ELD.set_addr, CH, SRC_ADDR)
        fw_call(ELD.enable_addr2, CH)
        fw_call(ELD.set_addr2, CH, DST_ADDR)

        -- Geometry
        mmio_write(MMIO_BASE + 0x48, v.xb + v.ya * 0x10000)
        mmio_write(MMIO_BASE + 0x4C, v.xa + v.yb * 0x10000)
        for off = 0x50, 0x70, 4 do
            mmio_write(MMIO_BASE + off, 0)
        end

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
        local mode = mmio_read(MMIO_BASE + 0xC0)
        log(string.format("    %d/%d match, stat=0x%X cmd=0x%X mode=0x%X",
            match, TEST_SIZE / 4, st, cmd, mode))

        if match == TEST_SIZE / 4 then
            log(string.format("  *** GEOMETRY '%s' WORKS! ***", v.name))
            dump_regs("working")
            return true, v.name
        elseif match > 0 then
            log(string.format("    Partial: %d words", match))
            for i = 0, math.min(TEST_SIZE - 4, 31), 4 do
                local actual = peek(DST_ADDR + i)
                if actual ~= 0 then
                    log(string.format("      DST+0x%02X = 0x%08X", i, actual))
                end
            end
        else
            log(string.format("    DST: 0x%08X 0x%08X",
                peek(DST_ADDR), peek(DST_ADDR+4)))
        end

        fw_call(ELD.stop, CH)
        fw_call(ELD.reset, CH)
    end

    log("  TEST D: No geometry variant succeeded")
    return false
end

-----------------------------------------------------------------------
-- TEST E: Also try mode=1 + addr2 enable with geometry variants
-----------------------------------------------------------------------
local function test_e_mode1_geom()
    log("=== TEST E: mode(1) + addr2 enable + geometry variants ===")

    local variants = {
        {name="m1+xb=256",      xb=256, yb=0},
        {name="m1+xb=255",      xb=255, yb=0},
        {name="m1+xb=64,yb=3",  xb=64,  yb=3},
    }

    for _, v in ipairs(variants) do
        log(string.format("  --- %s ---", v.name))
        prepare_memory(0xABCD0000)

        fw_call(ELD.reset, CH)
        fw_call(ELD.init, CH)
        fw_call(ELD.clear_enable, CH)
        fw_call(ELD.config, CH, DEFAULT_CALLBACK, CONFIG_MODE)

        fw_call(ELD.set_mode, CH, 1)   -- hardware mode 1
        fw_call(ELD.enable_addr2, CH)  -- bit 6
        fw_call(ELD.set_addr, CH, SRC_ADDR)
        fw_call(ELD.set_addr2, CH, DST_ADDR)

        mmio_write(MMIO_BASE + 0x48, v.xb)
        mmio_write(MMIO_BASE + 0x4C, v.yb * 0x10000)
        for off = 0x50, 0x70, 4 do
            mmio_write(MMIO_BASE + off, 0)
        end

        fw_call(ELD.set_enable, CH)
        fw_call(ELD.trigger, CH)
        msleep(200)

        local match = 0
        for i = 0, TEST_SIZE - 4, 4 do
            if peek(DST_ADDR + i) == 0xABCD0000 + i then
                match = match + 1
            end
        end

        local st = mmio_read(MMIO_BASE + 0xB4)
        local mode = mmio_read(MMIO_BASE + 0xC0)
        log(string.format("    %d/%d match, stat=0x%X mode=0x%X",
            match, TEST_SIZE / 4, st, mode))

        if match == TEST_SIZE / 4 then
            log(string.format("  *** '%s' WORKS! ***", v.name))
            dump_regs("working-e")
            return true, v.name
        elseif match > 0 then
            log(string.format("    Partial: %d words", match))
        end

        fw_call(ELD.stop, CH)
        fw_call(ELD.reset, CH)
    end

    log("  TEST E: No variant succeeded")
    return false
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
    log("EDMAC DMA Copy Test v3")
    log("Channel: 0x29 (Mem2MemPath)")
    log(string.format("SRC: 0x%08X, DST: 0x%08X, SIZE: %d", SRC_ADDR, DST_ADDR, TEST_SIZE))
    log("KEY FIX: enable_addr2 sets bit 6 in MMIO+0xC0")
    log("==========================================")

    -- Test A: Memory
    if not test_a() then
        log("ABORT: Memory access failed")
        cleanup()
        return
    end
    log("")

    -- Test B: Canon-style (addr2 enable, no mode 1)
    local b_ok = test_b_canon_style()
    cleanup()
    if b_ok then
        log("")
        log("*** DMA WORKS with Canon-style addr2 enable! ***")
        return
    end

    -- Test C: mode(1) + addr2 enable
    log("")
    local c_ok = test_c_mode1_plus_addr2()
    cleanup()
    if c_ok then
        log("")
        log("*** DMA WORKS with mode(1) + addr2! ***")
        return
    end

    -- Test D: Geometry variants with addr2
    log("")
    local d_ok, d_name = test_d_geom_variants()
    cleanup()
    if d_ok then
        log("")
        log(string.format("*** DMA WORKS with geometry '%s'! ***", d_name))
        return
    end

    -- Test E: mode(1) + addr2 + geometry variants
    log("")
    local e_ok, e_name = test_e_mode1_geom()
    cleanup()
    if e_ok then
        log("")
        log(string.format("*** DMA WORKS with '%s'! ***", e_name))
        return
    end

    log("")
    log("=== DMA copy still not working ===")
    log("Addr2 enable (bit 6) was set but data not transferred.")
    log("Remaining possibilities:")
    log("  1. Need EfmErscLockResources before using channel")
    log("  2. 0x4F000000 not DMA-accessible")
    log("  3. Geometry struct via set_geom required (not raw MMIO)")
    log("  4. Need specific xa (bytes-per-line stride)")
    log("=== End of test v3 ===")
end

local ok, err = pcall(main)
if not ok then
    log("SCRIPT ERROR: " .. tostring(err))
    pcall(cleanup)
end
