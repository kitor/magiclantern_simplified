#!/usr/bin/env python3
"""
Scan Canon M50 ROM dump for BL instructions calling EDMAC functions
in the ShootVfx region (0xE07F0000 - 0xE0840000).

ROM0.BIN is 32MB, mapped at virtual address 0xE0000000.
"""

import struct
import sys
import subprocess
import os

ROM_PATH = "/home/krzysztof/canon/dumps/m50_decrypted_rom/ROM0.BIN"
ROM_BASE = 0xE0000000

# ELD function targets (Thumb-2 code, BL target will be even address)
TARGETS = {
    "eld_edmac_set_addr":    0xE054A9AE,
    "eld_edmac_set_addr2":   0xE054AFA8,
    "eld_edmac_set_geom":    0xE054A9BA,
    "eld_edmac_set_mode":    0xE054AEB0,
    "eld_edmac_config":      0xE0549F76,
    "eld_edmac_set_enable":  0xE054A4F2,
    "eld_edmac_trigger":     0xE054BF18,
}

# Scan region
SCAN_START = 0xE07F0000
SCAN_END   = 0xE0840000

def va_to_offset(va):
    return va - ROM_BASE

def offset_to_va(off):
    return off + ROM_BASE

def decode_bl_target(hw1, hw2, pc):
    """
    Decode Thumb-2 BL instruction.
    hw1: first halfword (0xF000-0xF7FF range, bits[15:11]=11110)
    hw2: second halfword (bit15=1, bit14=1, bit12=1 for BL)
    pc: address of the BL instruction (address of hw1)
    
    Encoding:
      hw1: 11110 S imm10
      hw2: 11 J1 1 J2 imm11
      
    I1 = NOT(J1 XOR S)
    I2 = NOT(J2 XOR S)
    offset = sign_extend(S:I1:I2:imm10:imm11:0, 25)
    target = (pc + 4) + offset
    """
    # Check it's actually a BL
    if (hw1 >> 11) != 0x1E:  # bits[15:11] = 11110
        return None
    if (hw2 & 0xD000) != 0xD000:  # bit15=1, bit14=1, bit12=1
        return None
    
    S = (hw1 >> 10) & 1
    imm10 = hw1 & 0x3FF
    J1 = (hw2 >> 13) & 1
    J2 = (hw2 >> 11) & 1
    imm11 = hw2 & 0x7FF
    
    I1 = (~(J1 ^ S)) & 1
    I2 = (~(J2 ^ S)) & 1
    
    # Build 25-bit offset: S:I1:I2:imm10:imm11:0
    offset = (S << 24) | (I1 << 23) | (I2 << 22) | (imm10 << 12) | (imm11 << 1)
    
    # Sign extend from 25 bits
    if offset & (1 << 24):
        offset -= (1 << 25)
    
    target = (pc + 4) + offset
    return target & 0xFFFFFFFF

def scan_for_bl_targets(rom_data, scan_start, scan_end, target_addrs):
    """
    Scan ROM for BL instructions targeting any of the given addresses.
    Returns dict: target_name -> list of call site VAs
    """
    results = {name: [] for name in target_addrs}
    target_lookup = {}
    for name, addr in target_addrs.items():
        target_lookup[addr] = name
        # Also check addr+1 (Thumb bit) - but BL target is always even
        # The function address might be stored as odd (Thumb indicator) 
        # but BL computes even target
    
    start_off = va_to_offset(scan_start)
    end_off = va_to_offset(scan_end)
    
    # Scan every 2 bytes (Thumb alignment)
    for off in range(start_off, end_off - 2, 2):
        hw1 = struct.unpack_from('<H', rom_data, off)[0]
        # Quick check: is this the first halfword of a BL?
        if (hw1 >> 11) != 0x1E:
            continue
        
        # Read second halfword
        hw2 = struct.unpack_from('<H', rom_data, off + 2)[0]
        
        pc = offset_to_va(off)
        target = decode_bl_target(hw1, hw2, pc)
        
        if target is not None and target in target_lookup:
            name = target_lookup[target]
            results[name].append(pc)
    
    return results

def disassemble_region(rom_data, center_va, before=64, after=64):
    """
    Disassemble a region around center_va using arm-none-eabi-objdump.
    """
    start_va = center_va - before
    end_va = center_va + after
    
    start_off = va_to_offset(start_va)
    end_off = va_to_offset(end_va)
    
    if start_off < 0:
        start_off = 0
        start_va = ROM_BASE
    if end_off > len(rom_data):
        end_off = len(rom_data)
    
    chunk = rom_data[start_off:end_off]
    
    # Write temp binary
    tmp_path = "/tmp/edmac_chunk.bin"
    with open(tmp_path, 'wb') as f:
        f.write(chunk)
    
    # Disassemble with objdump
    cmd = [
        "arm-none-eabi-objdump",
        "-D",
        "-b", "binary",
        "-m", "arm",
        "-M", "force-thumb",
        "--adjust-vma=0x%X" % start_va,
        tmp_path
    ]
    
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        return result.stdout
    except Exception as e:
        return f"Disassembly error: {e}"

def find_function_bounds(rom_data, call_site_va, max_back=512, max_fwd=512):
    """
    Try to find rough function boundaries around a call site.
    Look for PUSH {..., LR} before and POP {..., PC} after.
    Returns (start_va, end_va) or None.
    """
    start_off = va_to_offset(call_site_va)
    
    # Search backwards for PUSH {... LR}
    func_start = None
    for off in range(start_off, max(start_off - max_back, 0), -2):
        hw = struct.unpack_from('<H', rom_data, off)[0]
        # PUSH with LR: 1011 0101 xxxx xxxx (0xB5xx)
        if (hw & 0xFF00) == 0xB500:
            func_start = offset_to_va(off)
            break
        # Also check 32-bit PUSH: E92D 4xxx or E92D Cxxx
        if off >= 2:
            hw_prev = struct.unpack_from('<H', rom_data, off - 2)[0]
            if hw_prev == 0xE92D and (hw & 0x4000):  # STM with LR
                func_start = offset_to_va(off - 2)
                break
    
    # Search forwards for POP {... PC}
    func_end = None
    for off in range(start_off, min(start_off + max_fwd, len(rom_data) - 2), 2):
        hw = struct.unpack_from('<H', rom_data, off)[0]
        # POP with PC: 1011 1101 xxxx xxxx (0xBDxx)
        if (hw & 0xFF00) == 0xBD00:
            func_end = offset_to_va(off) + 2
            break
        # Also check 32-bit POP: E8BD 8xxx
        if off + 2 < len(rom_data):
            hw2 = struct.unpack_from('<H', rom_data, off + 2)[0]
            if hw == 0xE8BD and (hw2 & 0x8000):
                func_end = offset_to_va(off) + 4
                break
    
    return func_start, func_end

def scan_function_for_calls(rom_data, func_start_va, func_end_va, targets):
    """
    Scan a function region for BL calls to any of the target functions.
    Returns dict: target_name -> list of call site VAs within this function.
    """
    results = {name: [] for name in targets}
    target_lookup = {addr: name for name, addr in targets.items()}
    
    start_off = va_to_offset(func_start_va)
    end_off = va_to_offset(func_end_va)
    
    for off in range(start_off, end_off - 2, 2):
        hw1 = struct.unpack_from('<H', rom_data, off)[0]
        if (hw1 >> 11) != 0x1E:
            continue
        hw2 = struct.unpack_from('<H', rom_data, off + 2)[0]
        pc = offset_to_va(off)
        target = decode_bl_target(hw1, hw2, pc)
        if target is not None and target in target_lookup:
            name = target_lookup[target]
            results[name].append(pc)
    
    return results

def main():
    print(f"Loading ROM from {ROM_PATH}...")
    with open(ROM_PATH, 'rb') as f:
        rom_data = f.read()
    print(f"ROM size: {len(rom_data)} bytes (0x{len(rom_data):08X})")
    print(f"Scan region: 0x{SCAN_START:08X} - 0x{SCAN_END:08X}")
    print()
    
    # Step 1: Find all BL calls to eld_edmac_set_addr2 in scan region
    print("=" * 80)
    print("STEP 1: Scanning for BL calls to eld_edmac_set_addr2")
    print("=" * 80)
    
    addr2_results = scan_for_bl_targets(
        rom_data, SCAN_START, SCAN_END,
        {"eld_edmac_set_addr2": TARGETS["eld_edmac_set_addr2"]}
    )
    
    addr2_sites = addr2_results["eld_edmac_set_addr2"]
    print(f"\nFound {len(addr2_sites)} call sites for eld_edmac_set_addr2:")
    for site in addr2_sites:
        print(f"  0x{site:08X}")
    print()
    
    # Step 2 & 3: For each call site, find function bounds, disassemble, and check for other calls
    print("=" * 80)
    print("STEP 2 & 3: Analyzing each call site")
    print("=" * 80)
    
    seen_functions = set()
    
    for site in addr2_sites:
        func_start, func_end = find_function_bounds(rom_data, site, max_back=1024, max_fwd=1024)
        
        # Deduplicate by function start
        func_key = func_start if func_start else site
        if func_key in seen_functions:
            continue
        seen_functions.add(func_key)
        
        print()
        print("-" * 80)
        print(f"Call site: 0x{site:08X}")
        if func_start:
            print(f"Function bounds: 0x{func_start:08X} - 0x{func_end:08X} ({func_end - func_start} bytes)")
        else:
            print(f"Could not determine function start (searching back 1024 bytes)")
        print("-" * 80)
        
        # Disassemble the function or region around call site
        if func_start and func_end:
            disasm_start = func_start
            disasm_end = func_end
            # Cap at reasonable size
            if disasm_end - disasm_start > 2048:
                disasm_start = max(func_start, site - 256)
                disasm_end = min(func_end, site + 256)
        else:
            disasm_start = site - 64
            disasm_end = site + 64
        
        disasm = disassemble_region(rom_data, disasm_start, before=0, 
                                     after=disasm_end - disasm_start)
        
        # Annotate the disassembly with function names
        annotated_lines = []
        for line in disasm.split('\n'):
            annotated = line
            # Check if any target address appears in the line
            for name, addr in TARGETS.items():
                hex_addr = f"{addr:x}"
                if hex_addr in line.lower():
                    annotated += f"  ; <<< {name}"
            # Mark the call site itself
            site_hex = f"{site:x}"
            if line.strip().startswith(f"e{site_hex[1:]}") or site_hex in line.lower():
                if "bl" in line.lower() and "set_addr2" not in annotated:
                    pass  # already annotated
            annotated_lines.append(annotated)
        
        print('\n'.join(annotated_lines))
        
        # Check for other EDMAC calls in this function
        if func_start and func_end:
            print()
            print("  EDMAC function calls in this function:")
            func_calls = scan_function_for_calls(rom_data, func_start, func_end, TARGETS)
            for name in sorted(TARGETS.keys()):
                sites_list = func_calls[name]
                if sites_list:
                    print(f"    {name}: {len(sites_list)} call(s) at {', '.join(f'0x{s:08X}' for s in sites_list)}")
                else:
                    print(f"    {name}: NOT FOUND")
            
            # Highlight if both set_addr and set_addr2 are present
            has_addr = len(func_calls["eld_edmac_set_addr"]) > 0
            has_addr2 = len(func_calls["eld_edmac_set_addr2"]) > 0
            if has_addr and has_addr2:
                print()
                print("  *** DUAL-ADDRESS MEM-TO-MEM CANDIDATE! ***")
                print(f"      set_addr  calls: {func_calls['eld_edmac_set_addr']}")
                print(f"      set_addr2 calls: {func_calls['eld_edmac_set_addr2']}")
        
        print()
    
    # Summary
    print("=" * 80)
    print("SUMMARY")
    print("=" * 80)
    print(f"Total eld_edmac_set_addr2 call sites in scan region: {len(addr2_sites)}")
    print()
    
    # Also do a broader scan for ALL edmac functions in the region
    print("Bonus: All EDMAC function calls in 0xE07F0000-0xE0840000:")
    all_results = scan_for_bl_targets(rom_data, SCAN_START, SCAN_END, TARGETS)
    for name in sorted(TARGETS.keys()):
        sites_list = all_results[name]
        print(f"  {name}: {len(sites_list)} call(s)")
        for s in sites_list:
            print(f"    0x{s:08X}")
    
    print("\nDone.")

if __name__ == "__main__":
    main()
