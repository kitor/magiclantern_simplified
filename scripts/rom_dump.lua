-- ROM Dumper v2.0
-- Dumps decrypted ROM to binary files on SD card
-- Uses dump_memory() for fast C-level bulk writes
--
-- DIGIC 8 (M50 1.1.0) memory map:
--   ROM0: 0xE0000000 - 0xE1FFFFFF (32 MB, main firmware)
--   ROM1: 0xF0000000 - 0xF0FFFFFF (16 MB, secondary)
--   Boot: 0xDF000000 - 0xDF00FFFF (64 KB, bootloader)
--
-- Output: ML/LOGS/RD_XX.BIN (1 MB per file)
-- Status: ML/LOGS/RD_STAT.LOG

local FILE_SIZE = 1048576  -- 1 MB per output file

-- Regions to dump
local REGIONS = {
    { start = 0xE0000000, size = 0x02000000, name = "ROM0" },  -- 32 MB
    { start = 0xF0000000, size = 0x01000000, name = "ROM1" },  -- 16 MB
    { start = 0xDF000000, size = 0x00010000, name = "BOOT" },  --  64 KB
}

-- Status logging
local status_lines = {}
local function log_status(msg)
    status_lines[#status_lines + 1] = msg
    -- Write entire status file each time (append is broken on DIGIC 8)
    dryos.write_file("ML/LOGS/RD_STAT.LOG",
        table.concat(status_lines, "\n") .. "\n")
end

-- MAIN --
log_status("ROM Dumper v2.0 starting")
log_status("Using dump_memory() for fast C-level bulk writes")

-- Verify ROM peek works
local test = dryos.peek(0xE00400FC)
if test == nil then
    log_status("FATAL: peek(0xE00400FC) returned nil!")
    return
end
log_status(string.format("ROM peek OK: 0xE00400FC = 0x%08X", test))

local file_idx = 0
local total_bytes = 0

for _, region in ipairs(REGIONS) do
    local base = region.start
    local size = region.size
    local name = region.name
    
    log_status(string.format("=== %s: 0x%08X, %d MB ===",
        name, base, size / 1048576))
    
    -- Quick check: can we read the first word?
    local first = dryos.peek(base)
    if first == nil then
        log_status(string.format("SKIP %s: unreadable", name))
    else
        log_status(string.format("  First word: 0x%08X", first))
        
        local addr = base
        local region_end = base + size
        
        while addr < region_end do
            local chunk_size = FILE_SIZE
            if addr + chunk_size > region_end then
                chunk_size = region_end - addr
            end
            
            local fname = string.format("ML/LOGS/RD_%02d.BIN", file_idx)
            
            -- dump_memory writes directly from ROM address to file
            local ok = dryos.dump_memory(fname, addr, chunk_size)
            
            if ok then
                total_bytes = total_bytes + chunk_size
                log_status(string.format("  %s: %dK @ 0x%08X  (%d MB done)",
                    fname, chunk_size / 1024, addr,
                    math.floor(total_bytes / 1048576)))
            else
                log_status(string.format("  %s: FAILED @ 0x%08X", fname, addr))
            end
            
            addr = addr + chunk_size
            file_idx = file_idx + 1
            
            -- Give the camera a breather between files
            msleep(100)
        end
    end
end

log_status(string.format("=== DONE === %d files, %d MB total",
    file_idx, math.floor(total_bytes / 1048576)))
