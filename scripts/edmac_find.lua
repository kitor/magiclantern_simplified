-- EDMAC Function Finder v3.3 for M50 (DIGIC 8)
-- Dumps key Canon firmware data structures to help find EDMAC/DMA functions
-- Run from ML Scripts menu while in LiveView
--
-- SAFE: Only reads from RAM via dryos.peek() (validates address ranges).
-- ROM reads use dryos.shamem_read() which goes through CPU1 RPC.
-- MMIO reads are blocked (shamem_read returns 0 for 0xC0/0xD0 ranges).
-- Each section written to ML/LOGS/SEC_XX.LOG via dryos.write_file().

console.show()
print("EDMAC Function Finder v3.3")
print("==========================")

-- Section counter and buffer
local sec_num = 0
local log_buf = {}

-- Flush buffer to ML/LOGS/SEC_XX.LOG (one file per section, using write_file which works)
local function logflush()
    if #log_buf > 0 then
        sec_num = sec_num + 1
        local fname = string.format("ML/LOGS/SEC_%02d.LOG", sec_num)
        local chunk = table.concat(log_buf, "\n") .. "\n"
        local ok = dryos.write_file(fname, chunk)
        if ok then
            print("  -> wrote " .. fname)
        else
            print("  -> FAILED " .. fname)
        end
        log_buf = {}
    end
end

-- Unified log+print function
local function log(fmt, ...)
    local s = string.format(fmt, ...)
    print(s)
    log_buf[#log_buf + 1] = s
end

local function hex(val)
    if val == nil then return "nil" end
    if val < 0 then val = val + 0x100000000 end
    return string.format("0x%08X", val)
end

local function peek(addr)
    local ok, val = pcall(dryos.peek, addr)
    if ok and val ~= nil then return val end
    return nil
end

local function shamem(addr)
    local ok, val = pcall(dryos.shamem_read, addr)
    if ok and val ~= nil then return val end
    return nil
end

-- Check if a value looks like a ROM function pointer (0xE0xxxxxx)
local function is_rom_ptr(val)
    if val == nil then return false end
    if val < 0 then val = val + 0x100000000 end
    local top = math.floor(val / 0x10000000)
    return top == 0xE
end

-- Check if a value looks like an MMIO address (0xC0/0xD0)
local function is_mmio(val)
    if val == nil then return false end
    if val < 0 then val = val + 0x100000000 end
    local top = math.floor(val / 0x10000000)
    return top == 0xC or top == 0xD
end

log("EDMAC Function Finder v3.3 - M50 1.1.0")
log("========================================")
logflush()

---------------------------------------------------------------
-- Section 1: Basic memory access test
---------------------------------------------------------------
log("\n--- Section 1: Basic memory access ---")
local test = peek(0x1028)
if test then
    log("current_task ptr: %s --- peek() works!", hex(test))
else
    log("FAILED to read current_task at 0x1028!")
    logflush()
    return
end
logflush()

---------------------------------------------------------------
-- Section 2: Test shamem_read (ROM access via CPU1 RPC)
---------------------------------------------------------------
log("\n--- Section 2: shamem_read ROM test ---")
local rom_val = shamem(0xE0040000)
if rom_val then
    log("shamem_read(0xE0040000) = %s  (ROM readable!)", hex(rom_val))
else
    log("shamem_read(0xE0040000) = nil  (ROM NOT readable)")
end
-- Also test that MMIO returns 0 (our safety guard)
local mmio_val = shamem(0xC0F00000)
log("shamem_read(0xC0F00000) = %s  (MMIO, expect 0)", hex(mmio_val))
logflush()

---------------------------------------------------------------
-- Section 3: Raw state variables
---------------------------------------------------------------
log("\n--- Section 3: Raw state variables ---")
local raw_state_addrs = {
    {0x0000D1C8, "raw_info state (lv_raw)"},
    {0x0000D1CC, "raw_info +4"},
    {0x0000D1D0, "raw_info +8"},
    {0x0000D1D4, "raw_info +12"},
}
for _, entry in ipairs(raw_state_addrs) do
    local val = peek(entry[1])
    log("  [%s] = %s  (%s)", hex(entry[1]), hex(val), entry[2])
end
logflush()

---------------------------------------------------------------
-- Section 4: ELD/Messi area (EDMAC driver structures)
---------------------------------------------------------------
log("\n--- Section 4: ELD/Messi area (0x38300) ---")
local eld_count = 0
for i = 0, 127 do
    local addr = 0x38300 + i * 4
    local val = peek(addr)
    if val then
        local marks = ""
        if is_rom_ptr(val) then marks = " <-- ROM func" end
        if is_mmio(val) then marks = " <-- MMIO" end
        if marks ~= "" or (i < 8) then
            log("  [%s] = %s%s", hex(addr), hex(val), marks)
            eld_count = eld_count + 1
        end
    end
end
log("  (%d interesting entries shown)", eld_count)
logflush()

---------------------------------------------------------------
-- Section 5: IRQ vectors (33-90, EDMAC interrupts)
---------------------------------------------------------------
log("\n--- Section 5: IRQ vectors (33-90) ---")
for irq = 33, 90 do
    local addr = 0x17884 + (irq - 33) * 4
    local val = peek(addr)
    if val and is_rom_ptr(val) then
        log("  IRQ[%d] @ [%s] = %s", irq, hex(addr), hex(val))
    end
end
logflush()

---------------------------------------------------------------
-- Section 6: Integ Pipeline table (0x410300)
---------------------------------------------------------------
log("\n--- Section 6: Integ Pipeline table (0x410300) ---")
local pipe_rom = 0
local pipe_mmio = 0
for i = 0, 255 do
    local addr = 0x410300 + i * 4
    local val = peek(addr)
    if val then
        local marks = ""
        if is_rom_ptr(val) then marks = " <-- ROM func"; pipe_rom = pipe_rom + 1 end
        if is_mmio(val) then marks = " <-- MMIO"; pipe_mmio = pipe_mmio + 1 end
        if marks ~= "" then
            log("  [%s] = %s%s", hex(addr), hex(val), marks)
        end
    end
end
log("  Summary: %d ROM ptrs, %d MMIO refs", pipe_rom, pipe_mmio)
logflush()

---------------------------------------------------------------
-- Section 7: MvRawProPath / EfmErscCreateLockEntry area
---------------------------------------------------------------
log("\n--- Section 7: MvRawProPath / Lock areas ---")
local lock_regions = {
    {0x40D600, 96, "MvRawProPath"},
    {0x40DA00, 128, "EfmErscCreateLockEntry"},
}
for _, r in ipairs(lock_regions) do
    log("  [%s] (%s):", hex(r[1]), r[3])
    for i = 0, r[2]-1 do
        local addr = r[1] + i * 4
        local val = peek(addr)
        if val then
            if is_rom_ptr(val) or is_mmio(val) then
                local marks = ""
                if is_rom_ptr(val) then marks = " <-- ROM func" end
                if is_mmio(val) then marks = " <-- MMIO" end
                log("    [%s] = %s%s", hex(addr), hex(val), marks)
            end
        end
    end
end
logflush()

---------------------------------------------------------------
-- Section 8: DryOS function tables
---------------------------------------------------------------
log("\n--- Section 8: DryOS function tables ---")
local func_tables = {
    {0x5400, 64, "func_table @0x5400"},
    {0xD400, 64, "func_table @0xD400"},
    {0x10C00, 64, "func_table @0x10C00"},
}
for _, ft in ipairs(func_tables) do
    local rom_count = 0
    for i = 0, ft[2]-1 do
        local addr = ft[1] + i * 4
        local val = peek(addr)
        if val and is_rom_ptr(val) then
            rom_count = rom_count + 1
            if rom_count <= 16 then
                log("  [%s] = %s", hex(addr), hex(val))
            end
        end
    end
    log("  %s: %d ROM function pointers", ft[3], rom_count)
end
logflush()

---------------------------------------------------------------
-- Section 9: Known EDMAC-related addresses (from RAM dump)
---------------------------------------------------------------
log("\n--- Section 9: Known EDMAC-related RAM locations ---")
-- These addresses were found by offline analysis of RAM4.BIN
-- containing EDMAC MMIO refs (0xD042xxxx) and nearby ROM pointers
local known_edmac_locs = {
    -- EDMAC channel base addresses found in RAM dump
    {0x38300, 32, "ELD base (EDMAC driver)"},
    {0x38400, 32, "ELD +0x100"},
    {0x38500, 32, "ELD +0x200"},
    -- Known EDMAC MMIO ref locations (from prior RAM4.BIN scan)
    {0x40D000, 32, "Pipeline area 1"},
    {0x40D100, 32, "Pipeline area 2"},
    {0x40D200, 32, "Pipeline area 3"},
    {0x40D300, 32, "Pipeline area 4"},
    {0x40D400, 32, "Pipeline area 5"},
    {0x40D500, 32, "Pipeline area 6"},
    {0x411000, 32, "Integ pipeline ext"},
    {0x411100, 32, "Integ pipeline ext2"},
    {0x411200, 32, "Integ pipeline ext3"},
}
for _, loc in ipairs(known_edmac_locs) do
    local found_interesting = false
    for i = 0, loc[2]-1 do
        local addr = loc[1] + i * 4
        local val = peek(addr)
        if val then
            if is_rom_ptr(val) or is_mmio(val) then
                if not found_interesting then
                    log("  [%s] (%s):", hex(loc[1]), loc[3])
                    found_interesting = true
                end
                local marks = ""
                if is_rom_ptr(val) then marks = " <-- ROM func" end
                if is_mmio(val) then marks = " <-- MMIO" end
                log("    [%s] = %s%s", hex(addr), hex(val), marks)
            end
        end
    end
    task.yield(10)
end
logflush()

---------------------------------------------------------------
-- Section 10: ROM function candidates - dump prologues
---------------------------------------------------------------
log("\n--- Section 10: ROM function candidates (via shamem_read) ---")
local candidates = {
    {0xE016C4D4, "CreateResLockEntry?"},
    {0xE008F7E4, "LockEngineResources?"},
    {0xE0558ECE, "EngRscLock entry 1"},
    {0xE01C842E, "EngRscLock entry 2"},
    {0xE00E9808, "MvRawProPath func"},
    {0xE0DD7BA0, "near EDMAC ch7"},
    {0xE0558F24, "near raw buffer"},
    {0xE0572BB8, "integ pipeline func"},
    {0xE057807A, "near SapPath"},
    {0xE0096464, "SCAN_GetIntegDataCBR"},
}
for _, c in ipairs(candidates) do
    log("  Candidate: %s @ %s", c[2], hex(c[1]))
    -- Read 8 words (32 bytes) of prologue
    local readable = false
    for off = 0, 28, 4 do
        local val = shamem(c[1] + off)
        if val and val ~= 0 then
            readable = true
            log("    [%s+%02d] = %s", hex(c[1]), off, hex(val))
        elseif val == 0 then
            log("    [%s+%02d] = 0x00000000 (shamem returned 0)", hex(c[1]), off)
        else
            log("    [%s+%02d] = nil", hex(c[1]), off)
        end
    end
    if not readable then
        log("    (all zeros or unreadable)")
    end
    task.yield(50) -- Give CPU1 time between RPC calls
end
logflush()

---------------------------------------------------------------
-- Done
---------------------------------------------------------------
log("\n========================================")
log("=== DONE ===")
logflush()
print("All sections saved to ML/LOGS/SEC_XX.LOG")
