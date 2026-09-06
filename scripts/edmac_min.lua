-- Minimal EDMAC test - just verify Lua extensions work
-- If this runs without errors, the infrastructure is OK

display.print("edmac_min starting", 10, 50)

-- Test 1: check if dryos extensions exist
local has_fw_call = (dryos.fw_call ~= nil)
local has_mmio_read = (dryos.mmio_read ~= nil)
local has_mmio_write = (dryos.mmio_write ~= nil)

display.print("fw_call: " .. tostring(has_fw_call), 10, 70)
display.print("mmio_read: " .. tostring(has_mmio_read), 10, 90)
display.print("mmio_write: " .. tostring(has_mmio_write), 10, 110)

-- Test 2: read an EDMAC register (safe read-only)
if has_mmio_read then
    local val = dryos.mmio_read(0xD0440300)
    display.print("EDMAC reg 0xD0440300 = " .. tostring(val), 10, 140)
else
    display.print("mmio_read not available!", 10, 140)
end

-- Test 3: try peek (shamem_read equivalent)  
if dryos.peek then
    local val = dryos.peek(0xD0440300)
    display.print("peek 0xD0440300 = " .. tostring(val), 10, 160)
else
    display.print("peek not available", 10, 160)
end

display.print("edmac_min DONE", 10, 190)
