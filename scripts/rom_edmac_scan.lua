-- ROM EDMAC Scanner v1.0
-- Searches decrypted ROM for EDMAC/DMA related strings
-- Uses extended peek() that supports ROM addresses
-- Output: RS_XX.LOG files in ML/LOGS/

-- Write a section output to file
local function write_log(idx, lines)
    local name = string.format("ML/LOGS/RS_%02d.LOG", idx)
    dryos.write_file(name, table.concat(lines, "\n") .. "\n")
end

-- Read a block of ROM as a Lua string (256 bytes = 64 words)
local BLOCK = 256
local OVERLAP = 32  -- overlap to catch strings crossing block boundaries
local function read_block(addr)
    local bytes = {}
    for i = 0, BLOCK - 4, 4 do
        local w = dryos.peek(addr + i)
        if w == nil then return nil end
        -- little-endian byte extraction
        bytes[#bytes + 1] = string.char(
            w % 256,
            math.floor(w / 256) % 256,
            math.floor(w / 65536) % 256,
            math.floor(w / 16777216) % 256
        )
    end
    return table.concat(bytes)
end

-- Read a string from ROM starting at addr, up to max_len chars
local function read_cstring(addr, max_len)
    local chars = {}
    for i = 0, (max_len or 80) - 1, 4 do
        local w = dryos.peek(addr + i)
        if w == nil then break end
        for j = 0, 3 do
            if i + j >= (max_len or 80) then break end
            local b = math.floor(w / (2^(j*8))) % 256
            if b == 0 then return table.concat(chars) end
            if b >= 32 and b < 127 then
                chars[#chars + 1] = string.char(b)
            else
                return table.concat(chars)
            end
        end
    end
    return table.concat(chars)
end

-- MAIN
local file_idx = 0
local all_results = {}

-- First: verify ROM peek works
local test = dryos.peek(0xE00400FC)  -- cstart address
if test == nil then
    all_results[#all_results + 1] = "ERROR: peek(0xE00400FC) returned nil - ROM read not working!"
    write_log(file_idx, all_results)
    return
end
all_results[#all_results + 1] = string.format("ROM peek test: 0xE00400FC = 0x%08X (cstart)", test)

-- Try reading near DryosDebugMsg
local s = read_cstring(0xE0577FD0, 32)
all_results[#all_results + 1] = string.format("Bytes at DryosDebugMsg addr: \"%s\"", s or "nil")

-- Search terms (case-sensitive, using Lua patterns)
local search_terms = {
    "edmac", "EDMAC", "Edmac",
    "StartEDmac", "SetEDmac",
    "ConnectEDmac", "ConnectWrite", "ConnectRead",
    "RegisterEDmac",
    "mem_to_mem", "MemToMem", "mem2mem",
    "dma_copy", "DmaCopy", "dma_memcpy",
    "DmaTransfer", "dmaCh", "DMA ch",
    "ResLock", "reslock", "EngRes", "engRes",
    "channel_info", "ch_info",
    "edma_", "EDMA_",
    "dmaFlags", "dma_flag",
    "LockEng", "lockEng",
    "m2m_cbr", "m2m_copy",
}

-- ROM regions to search (8 x 1MB)
local regions = {
    {0xE0040000, 0xE0100000, "ROM_low"},
    {0xE0100000, 0xE0200000, "ROM_0x100"},
    {0xE0200000, 0xE0300000, "ROM_0x200"},
    {0xE0300000, 0xE0400000, "ROM_0x300"},
    {0xE0400000, 0xE0500000, "ROM_0x400"},
    {0xE0500000, 0xE0600000, "ROM_0x500"},
    {0xE0600000, 0xE0700000, "ROM_0x600"},
    {0xE0700000, 0xE0800000, "ROM_0x700"},
}

all_results[#all_results + 1] = string.format("Searching %d terms across %d regions (block=%d)", #search_terms, #regions, BLOCK)
write_log(file_idx, all_results)
file_idx = file_idx + 1

-- Search each region
for ri, region in ipairs(regions) do
    local start_a, end_a, name = region[1], region[2], region[3]
    all_results = {}
    all_results[#all_results + 1] = string.format("=== %s: 0x%08X - 0x%08X ===", name, start_a, end_a)
    local hits = 0
    local step = BLOCK - OVERLAP

    for addr = start_a, end_a - BLOCK, step do
        local blk = read_block(addr)
        if blk then
            for _, term in ipairs(search_terms) do
                local pos = 1
                while true do
                    local s, e = string.find(blk, term, pos, true)  -- plain search
                    if not s then break end
                    local rom_addr = addr + s - 1
                    local full = read_cstring(rom_addr, 80)
                    all_results[#all_results + 1] = string.format("  0x%08X: \"%s\"", rom_addr, full or "?")
                    hits = hits + 1
                    pos = e + 1
                end
            end
        end
        -- Yield periodically to avoid watchdog
        if (addr - start_a) % 0x8000 == 0 then
            msleep(10)
        end
        -- Flush every ~40 lines
        if #all_results > 40 then
            write_log(file_idx, all_results)
            file_idx = file_idx + 1
            all_results = {}
            collectgarbage()
        end
    end

    all_results[#all_results + 1] = string.format("--- %s: %d hits ---", name, hits)
    write_log(file_idx, all_results)
    file_idx = file_idx + 1
    all_results = {}
    collectgarbage()
end

-- Done
all_results[#all_results + 1] = "=== ROM EDMAC SCAN COMPLETE ==="
all_results[#all_results + 1] = string.format("Total log files: %d", file_idx + 1)
write_log(file_idx, all_results)
