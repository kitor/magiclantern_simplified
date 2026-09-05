-- EDMAC DMA Copy Test v4.1 for Canon M50 (DIGIC 8)
--
-- v4 crashed: eld_edmac_set_geom asserts on ch 0x28/0x29 (flag bit 12 = 0)
-- v4 also showed: ch 0x28 is IN USE by Canon, Mem2Mem module NOT initialized
--
-- v4.1 fixes:
--   - Raw MMIO writes for geometry (no set_geom API)
--   - Avoid ch 0x28 (active Canon use)
--   - Skip Mem2Mem module test (not initialized)
--   - Test connect (+0x08=0x12) vs trigger (+0xDC=1)
--   - Test two-channel with ch 0x29 (read) + 0x2A (write)
--
-- Log: ML/LOGS/dma4.log

local LOG = "ML/LOGS/dma4.log"

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

local function mmio_read(addr) return dryos.mmio_read(addr) end
local function mmio_write(addr, val) dryos.mmio_write(addr, val) end
local function fw_call(addr, ...) return dryos.fw_call(addr, ...) end
local function peek(addr) return dryos.peek(addr) end
local function poke(addr, val) dryos.poke(addr, val) end

local ELD = {
    init          = 0xE054A30E,
    reset         = 0xE0549E8C,
    connect       = 0xE0549E52,  -- writes 0x12 to +0x08 (starts DMA)
    clear_enable  = 0xE054A4B2,
    set_enable    = 0xE054A4F2,
    set_addr      = 0xE054A9AE,
    set_addr2     = 0xE054AFA8,
    enable_addr2  = 0xE054AF96,
    set_mode      = 0xE054AEB0,
    config        = 0xE0549F76,
    trigger       = 0xE054BF18,  -- writes 1 to +0xDC
    start_arm     = 0xE054A7CC,  -- writes 1 to +0x04
}

local DEFAULT_CALLBACK = 0xE0549FA0
local EDMAC_TABLE = 0xE0DD7BA0

local SRC_ADDR  = 0x4F000000
local DST_ADDR  = 0x4F001000
local TEST_SIZE = 256

local function get_mmio_base(ch)
    return peek(EDMAC_TABLE + ch * 8)
end

local function dump_ch(label, ch)
    local base = get_mmio_base(ch)
    log(string.format("  [%s] ch 0x%02X (0x%08X):", label, ch, base))
    local offsets = {
        {0x00,"CTRL"},{0x04,"ARM"},{0x08,"CMD"},
        {0x0C,"?0C"},{0x18,"?18"},{0x1C,"?1C"},
        {0x20,"EN1"},{0x24,"EN2"},{0x3C,"CBR"},
        {0x48,"G1"},{0x4C,"G2"},{0x50,"G3"},
        {0x54,"G4"},{0x58,"G5"},{0x5C,"G6"},
        {0x60,"G7"},{0x64,"G8"},{0x68,"G9"},
        {0x6C,"GA"},{0x70,"GB"},{0xA0,"ADDR"},
        {0xAC,"ADR2"},{0xB4,"STAT"},{0xC0,"MODE"},
        {0xC4,"?C4"},{0xD8,"?D8"},{0xDC,"TRIG"},
    }
    local parts = {}
    for _, r in ipairs(offsets) do
        local v = mmio_read(base + r[1])
        if v ~= 0 then
            table.insert(parts, string.format("+%02X(%s)=0x%X", r[1], r[2], v))
        end
    end
    if #parts == 0 then log("    (all zero)")
    else
        for i = 1, #parts, 4 do
            local line = "    "
            for j = i, math.min(i+3, #parts) do line = line .. parts[j] .. "  " end
            log(line)
        end
    end
end

local function prepare_memory(pat)
    for i = 0, TEST_SIZE - 4, 4 do
        poke(SRC_ADDR + i, pat + i)
        poke(DST_ADDR + i, 0)
    end
end

local function check_result(pat)
    local match, mismatch = 0, 0
    for i = 0, TEST_SIZE - 4, 4 do
        local expected = pat + i
        local actual = peek(DST_ADDR + i)
        if actual == expected then match = match + 1
        else
            mismatch = mismatch + 1
            if mismatch <= 4 then
                log(string.format("    MISMATCH +0x%02X: got 0x%08X exp 0x%08X", i, actual, expected))
            end
        end
    end
    log(string.format("  Result: %d/%d words match", match, match + mismatch))
    return mismatch == 0
end

-- Raw MMIO geometry: xb bytes as a single line
local function set_raw_geom(base, xb)
    mmio_write(base + 0x48, xb)  -- xb | (ya << 16)
    mmio_write(base + 0x4C, 0)   -- xa | (yb << 16)
    for off = 0x50, 0x70, 4 do mmio_write(base + off, 0) end
end

local function poll_stat(base, label)
    local si = mmio_read(base + 0xB4)
    log(string.format("  Immediate %s stat=0x%X", label, si))
    for i = 1, 50 do
        msleep(10)
        local st = mmio_read(base + 0xB4)
        if i <= 5 or i % 10 == 0 then
            log(string.format("  Poll %dms: stat=0x%X", i * 10, st))
        end
        if st == 0x2 then
            log(string.format("  Complete at %dms", i * 10))
            return true
        end
    end
    return false
end

-----------------------------------------------------------------------
-- TEST A: System state probe (no DMA, just read registers)
-----------------------------------------------------------------------
local function test_a()
    log("=== TEST A: System state ===")
    prepare_memory(0xCAFE0000)
    local src_ok, dst_ok = true, true
    for i = 0, TEST_SIZE - 4, 4 do
        if peek(SRC_ADDR + i) ~= 0xCAFE0000 + i then src_ok = false end
        if peek(DST_ADDR + i) ~= 0 then dst_ok = false end
    end
    log(string.format("  Memory: SRC=%s DST=%s", src_ok and "OK" or "FAIL", dst_ok and "OK" or "FAIL"))

    -- Show Mem2Mem module state
    local m2m = 0x00016F38
    log(string.format("  M2M: cb=0x%08X wr=0x%02X rd=0x%02X",
        peek(m2m), peek(m2m + 8), peek(m2m + 0x0C)))

    -- Show ch 0x29 and 0x2A state
    dump_ch("ch29", 0x29)
    dump_ch("ch2A", 0x2A)
    log("  TEST A PASSED")
    return true
end

-----------------------------------------------------------------------
-- TEST B: Single-ch 0x29: CONNECT approach (new!)
-- ARM (+0x04=1) then CONNECT (+0x08=0x12) instead of TRIGGER (+0xDC=1)
-----------------------------------------------------------------------
local function test_b()
    log("=== TEST B: ch29 CONNECT + addr2 ===")
    prepare_memory(0xDEAD0000)
    local CH, BASE = 0x29, get_mmio_base(0x29)

    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)
    fw_call(ELD.config, CH, DEFAULT_CALLBACK, 0x20)
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.enable_addr2, CH)
    fw_call(ELD.set_addr2, CH, DST_ADDR)
    set_raw_geom(BASE, TEST_SIZE)

    dump_ch("pre-B", CH)

    fw_call(ELD.start_arm, CH)
    log(string.format("  After ARM: +04=0x%X +B4=0x%X", mmio_read(BASE+4), mmio_read(BASE+0xB4)))
    fw_call(ELD.connect, CH)

    poll_stat(BASE, "B")
    dump_ch("post-B", CH)
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xDEAD0000)
end

-----------------------------------------------------------------------
-- TEST C: Single-ch 0x29: CONNECT + set_enable
-----------------------------------------------------------------------
local function test_c()
    log("=== TEST C: ch29 CONNECT + enable ===")
    prepare_memory(0xBEEF0000)
    local CH, BASE = 0x29, get_mmio_base(0x29)

    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)
    fw_call(ELD.config, CH, DEFAULT_CALLBACK, 0x20)
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.enable_addr2, CH)
    fw_call(ELD.set_addr2, CH, DST_ADDR)
    set_raw_geom(BASE, TEST_SIZE)
    fw_call(ELD.set_enable, CH)

    fw_call(ELD.start_arm, CH)
    fw_call(ELD.connect, CH)
    msleep(200)
    dump_ch("post-C", CH)
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xBEEF0000)
end

-----------------------------------------------------------------------
-- TEST D: Single-ch 0x29: ARM + CONNECT + TRIGGER (all three!)
-----------------------------------------------------------------------
local function test_d()
    log("=== TEST D: ch29 ARM+CONNECT+TRIGGER ===")
    prepare_memory(0xF00D0000)
    local CH, BASE = 0x29, get_mmio_base(0x29)

    fw_call(ELD.reset, CH)
    fw_call(ELD.init, CH)
    fw_call(ELD.clear_enable, CH)
    fw_call(ELD.config, CH, DEFAULT_CALLBACK, 0x20)
    fw_call(ELD.set_addr, CH, SRC_ADDR)
    fw_call(ELD.enable_addr2, CH)
    fw_call(ELD.set_addr2, CH, DST_ADDR)
    set_raw_geom(BASE, TEST_SIZE)
    fw_call(ELD.set_enable, CH)

    fw_call(ELD.start_arm, CH)
    fw_call(ELD.connect, CH)
    fw_call(ELD.trigger, CH)
    msleep(200)
    dump_ch("post-D", CH)
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xF00D0000)
end

-----------------------------------------------------------------------
-- TEST E: Two-channel: rd=0x29, wr=0x2A
-- Canon's pattern: set_addr on both, ARM both, CONNECT write
-----------------------------------------------------------------------
local function test_e()
    log("=== TEST E: Two-ch rd=0x29 wr=0x2A ===")
    local RD, WR = 0x29, 0x2A
    local RD_BASE, WR_BASE = get_mmio_base(RD), get_mmio_base(WR)
    log(string.format("  rd MMIO=0x%08X  wr MMIO=0x%08X", RD_BASE, WR_BASE))

    prepare_memory(0xFACE0000)

    fw_call(ELD.reset, RD)
    fw_call(ELD.reset, WR)
    fw_call(ELD.config, RD, DEFAULT_CALLBACK, 0)
    fw_call(ELD.config, WR, DEFAULT_CALLBACK, 0)
    fw_call(ELD.clear_enable, RD)
    fw_call(ELD.clear_enable, WR)

    fw_call(ELD.set_addr, RD, SRC_ADDR)
    fw_call(ELD.set_addr, WR, DST_ADDR)

    set_raw_geom(RD_BASE, TEST_SIZE)
    set_raw_geom(WR_BASE, TEST_SIZE)

    fw_call(ELD.set_mode, RD, 0)
    fw_call(ELD.set_mode, WR, 0)

    dump_ch("pre-E-rd", RD)
    dump_ch("pre-E-wr", WR)

    log("  ARM both, CONNECT write")
    fw_call(ELD.start_arm, RD)
    fw_call(ELD.start_arm, WR)
    fw_call(ELD.connect, WR)

    local ws = mmio_read(WR_BASE + 0xB4)
    local rs = mmio_read(RD_BASE + 0xB4)
    log(string.format("  Immediate: wr=0x%X rd=0x%X", ws, rs))

    for i = 1, 50 do
        msleep(10)
        ws = mmio_read(WR_BASE + 0xB4)
        rs = mmio_read(RD_BASE + 0xB4)
        if i <= 5 or i % 10 == 0 then
            log(string.format("  Poll %dms: wr=0x%X rd=0x%X", i * 10, ws, rs))
        end
        if ws == 0x2 then break end
    end

    dump_ch("post-E-rd", RD)
    dump_ch("post-E-wr", WR)
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xFACE0000)
end

-----------------------------------------------------------------------
-- TEST F: Two-channel + set_enable on both
-----------------------------------------------------------------------
local function test_f()
    log("=== TEST F: Two-ch + enable ===")
    local RD, WR = 0x29, 0x2A
    local RD_BASE, WR_BASE = get_mmio_base(RD), get_mmio_base(WR)

    prepare_memory(0x12340000)

    fw_call(ELD.reset, RD)
    fw_call(ELD.reset, WR)
    fw_call(ELD.config, RD, DEFAULT_CALLBACK, 0)
    fw_call(ELD.config, WR, DEFAULT_CALLBACK, 0)
    fw_call(ELD.clear_enable, RD)
    fw_call(ELD.clear_enable, WR)

    fw_call(ELD.set_addr, RD, SRC_ADDR)
    fw_call(ELD.set_addr, WR, DST_ADDR)
    set_raw_geom(RD_BASE, TEST_SIZE)
    set_raw_geom(WR_BASE, TEST_SIZE)
    fw_call(ELD.set_mode, RD, 0)
    fw_call(ELD.set_mode, WR, 0)
    fw_call(ELD.set_enable, RD)
    fw_call(ELD.set_enable, WR)

    fw_call(ELD.start_arm, RD)
    fw_call(ELD.start_arm, WR)
    fw_call(ELD.connect, WR)
    msleep(200)

    dump_ch("post-F-wr", WR)
    dump_ch("post-F-rd", RD)
    log(string.format("  DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0x12340000)
end

-----------------------------------------------------------------------
-- TEST G: Two-ch, CONNECT read, CONNECT write, CONNECT both
-----------------------------------------------------------------------
local function test_g()
    log("=== TEST G: Two-ch connect variants ===")
    local RD, WR = 0x29, 0x2A
    local RD_BASE, WR_BASE = get_mmio_base(RD), get_mmio_base(WR)

    -- G1: connect read only
    log("  --- G1: connect read ---")
    prepare_memory(0x56780000)
    fw_call(ELD.reset, RD); fw_call(ELD.reset, WR)
    fw_call(ELD.config, RD, DEFAULT_CALLBACK, 0)
    fw_call(ELD.config, WR, DEFAULT_CALLBACK, 0)
    fw_call(ELD.clear_enable, RD); fw_call(ELD.clear_enable, WR)
    fw_call(ELD.set_addr, RD, SRC_ADDR)
    fw_call(ELD.set_addr, WR, DST_ADDR)
    set_raw_geom(RD_BASE, TEST_SIZE); set_raw_geom(WR_BASE, TEST_SIZE)
    fw_call(ELD.start_arm, RD); fw_call(ELD.start_arm, WR)
    fw_call(ELD.connect, RD)
    msleep(100)
    log(string.format("  G1 DST: 0x%08X", peek(DST_ADDR)))
    check_result(0x56780000)

    -- G2: connect both
    log("  --- G2: connect both ---")
    prepare_memory(0x9ABC0000)
    fw_call(ELD.reset, RD); fw_call(ELD.reset, WR)
    fw_call(ELD.config, RD, DEFAULT_CALLBACK, 0)
    fw_call(ELD.config, WR, DEFAULT_CALLBACK, 0)
    fw_call(ELD.clear_enable, RD); fw_call(ELD.clear_enable, WR)
    fw_call(ELD.set_addr, RD, SRC_ADDR)
    fw_call(ELD.set_addr, WR, DST_ADDR)
    set_raw_geom(RD_BASE, TEST_SIZE); set_raw_geom(WR_BASE, TEST_SIZE)
    fw_call(ELD.start_arm, RD); fw_call(ELD.start_arm, WR)
    fw_call(ELD.connect, WR)
    fw_call(ELD.connect, RD)
    msleep(100)
    log(string.format("  G2 DST: 0x%08X", peek(DST_ADDR)))
    check_result(0x9ABC0000)

    -- G3: connect both (reversed order: read first)
    log("  --- G3: connect read then write ---")
    prepare_memory(0xAAAA0000)
    fw_call(ELD.reset, RD); fw_call(ELD.reset, WR)
    fw_call(ELD.config, RD, DEFAULT_CALLBACK, 0)
    fw_call(ELD.config, WR, DEFAULT_CALLBACK, 0)
    fw_call(ELD.clear_enable, RD); fw_call(ELD.clear_enable, WR)
    fw_call(ELD.set_addr, RD, SRC_ADDR)
    fw_call(ELD.set_addr, WR, DST_ADDR)
    set_raw_geom(RD_BASE, TEST_SIZE); set_raw_geom(WR_BASE, TEST_SIZE)
    fw_call(ELD.start_arm, RD); fw_call(ELD.start_arm, WR)
    fw_call(ELD.connect, RD)
    fw_call(ELD.connect, WR)
    msleep(100)
    log(string.format("  G3 DST: 0x%08X", peek(DST_ADDR)))
    check_result(0xAAAA0000)

    return false
end

-----------------------------------------------------------------------
-- TEST H: Direct MMIO poke - absolute minimum DMA attempt
-- Skip ALL ELD functions. Just raw register writes.
-----------------------------------------------------------------------
local function test_h()
    log("=== TEST H: Raw MMIO poke (ch 0x29) ===")
    prepare_memory(0xBBBB0000)
    local BASE = get_mmio_base(0x29)

    -- Hard reset
    mmio_write(BASE + 0x00, 0x80000000)  -- CTRL reset
    msleep(10)

    -- Set addresses
    mmio_write(BASE + 0xA0, SRC_ADDR)    -- source
    -- Enable addr2 mode
    local mode = mmio_read(BASE + 0xC0)
    mmio_write(BASE + 0xC0, mode + 0x40) -- set bit 6 (or with 0x40)
    mmio_write(BASE + 0xAC, DST_ADDR)    -- destination
    -- Geometry
    set_raw_geom(BASE, TEST_SIZE)
    -- Enable
    mmio_write(BASE + 0x20, 1)
    mmio_write(BASE + 0x24, 1)

    dump_ch("pre-H", 0x29)

    -- Try ARM + CONNECT
    mmio_write(BASE + 0x04, 1)
    mmio_write(BASE + 0x08, 0x12)
    msleep(100)

    dump_ch("H-connect", 0x29)
    log(string.format("  H-connect DST: 0x%08X", peek(DST_ADDR)))

    -- Also try trigger
    mmio_write(BASE + 0xDC, 1)
    msleep(100)

    dump_ch("H-trigger", 0x29)
    log(string.format("  H-trigger DST: 0x%08X 0x%08X 0x%08X 0x%08X",
        peek(DST_ADDR), peek(DST_ADDR+4), peek(DST_ADDR+8), peek(DST_ADDR+12)))
    return check_result(0xBBBB0000)
end

-----------------------------------------------------------------------
-- MAIN
-----------------------------------------------------------------------
log("===================================================")
log("EDMAC DMA Test v4.1 - MMIO geom + connect + two-ch")
log(string.format("Date: %s", os.date()))
log("===================================================")

local tests = {
    {"A", test_a}, {"B", test_b}, {"C", test_c}, {"D", test_d},
    {"E", test_e}, {"F", test_f}, {"G", test_g}, {"H", test_h},
}

for _, t in ipairs(tests) do
    local ok, err = pcall(t[2])
    if not ok then log("TEST " .. t[1] .. " ERROR: " .. tostring(err)) end
    log("")
end

log("=== Cleanup ===")
pcall(function()
    fw_call(ELD.init, 0x29)
    fw_call(ELD.init, 0x2A)
end)
log("All tests complete.")
log("===================================================")
