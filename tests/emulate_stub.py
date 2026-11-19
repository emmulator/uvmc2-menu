#!/usr/bin/env python3
"""Runs the launch stub in a Cortex-M33 emulator, from the bytes launch_go copies. Needs unicorn, pyelftools.
Checks: blocks copied exactly, scratch 0 set, reboot called as the firmware calls it. Usage: emulate_stub.py <elf>"""
import struct, sys
from unicorn import *
from unicorn.arm_const import *
from elftools.elf.elffile import ELFFile
elf = ELFFile(open(sys.argv[1], 'rb')); sym = elf.get_section_by_name('.symtab')
beg = sym.get_symbol_by_name('launch_stub_begin')[0]['st_value']; end = sym.get_symbol_by_name('launch_stub_end')[0]['st_value']
text = elf.get_section_by_name('.text')
# what launch.c does: clear bit 0, copy begin..end
b = beg & ~1
code = text.data()[b - text['sh_addr']:end - text['sh_addr']]
STUB, ARGS, PSRAM, WD, ROM = 0x20080400, 0x20080100, 0x15000000, 0x400d8000, 0x00008000
u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS); u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M33) if hasattr(u, 'ctl_set_cpu_model') else None
u.mem_map(0x20000000, 0x90000); u.mem_map(PSRAM, 0x100000); u.mem_map(WD, 0x1000); u.mem_map(ROM, 0x1000)
A = bytes(range(256)) * 16; B = bytes(reversed(range(256))) * 4
u.mem_write(PSRAM, A + B)
u.mem_write(STUB, bytes(code))
blocks = [(0x20000000, len(A) // 4), (0x20040000, len(B) // 4)]
args = struct.pack('<5I', ROM | 1, 2, PSRAM, blocks[0][0], len(A) + len(B)) + b''.join(struct.pack('<II', *x) for x in blocks)
u.mem_write(ARGS, args)
hit = {}
def rom(uc, addr, size, ud):
    hit['r'] = [uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3)]; uc.emu_stop()
u.hook_add(UC_HOOK_CODE, rom, begin=ROM, end=ROM + 4)
u.mem_write(ROM, b'\x00\xbf\x00\xbf')
u.reg_write(UC_ARM_REG_SP, 0x20082000); u.reg_write(UC_ARM_REG_R0, ARGS)
u.emu_start(STUB | 1, 0, count=200000)
assert hit, 'reboot never called'
assert hit['r'] == [0x103, 10, 0x20000000, len(A) + len(B)], hit
assert bytes(u.mem_read(0x20000000, len(A))) == A and bytes(u.mem_read(0x20040000, len(B))) == B, 'copy wrong'
assert struct.unpack('<I', u.mem_read(WD + 12, 4))[0] == 0x554d4332, 'scratch'
print('stub OK: %d bytes copied, reboot(0x103, 10, %#x, %d), scratch0 = 2CMU' % (len(A) + len(B), hit['r'][2], hit['r'][3]))
