"""Structural contract check; target assembler compilation is a separate required check."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
s=(root/'hal/esp32p4/context_switch.S').read_text()
t=(root/'hal/esp32p4/vectors_p4.S').read_text()
i=(root/'hal/esp32p4/context_init.c').read_text()
assert 'CTX_FRAME_BYTES  128u' in i
assert 'addi    sp, sp, -128' in s and 'addi    sp, sp, 128' in s
for n in range(12):
    off=64+4*n
    assert re.search(r'fsw\s+fs%d,\s*%d\(sp\)'%(n,off),s)
    assert re.search(r'flw\s+fs%d,\s*%d\(sp\)'%(n,off),s)
assert s.index('csrs    mstatus, t0') < s.index('fsw     fs0,')
assert t.index('csrs    mstatus, t0') < t.index('fsw     f0,')
assert 'or      t2, t2, t0' in t
assert 'sw      t0, 112(sp)' in s and 'lw      t0, 112(sp)' in s
assert 'addi    sp, sp, -224' in t and 'addi    sp, sp, 224' in t
for n in range(32):
    off=80+4*n
    assert re.search(r'fsw\s+f%d,\s*%d\(sp\)'%(n,off),t)
    assert re.search(r'flw\s+f%d,\s*%d\(sp\)'%(n,off),t)
assert t.index('fsw     f0,') < t.index('call    m5_tick_dispatch') < t.index('flw     f0,')
assert t.index('fscsr') < t.index('csrw    mstatus, t2')
assert 'sw      t0, 208(sp)' in t and 'lw      t0, 208(sp)' in t
print('PASS: cooperative and asynchronous FP frame layouts, 16-byte alignment, restore order')
