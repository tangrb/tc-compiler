#!/usr/bin/env python3
"""S8 layout fuzz suite.

Compact struct offsets, padding bytes, sizeof and typed/byte loads.  Every case is generated deterministically from a fixed seed and its
expected observable behavior is derived by an independent Python model of the
standard rules, never copied from tool output.  ``--seeds N`` runs a larger
campaign than the default gate set.

Run:
    python3 tests/conformance/diff/s8_layout_fuzz.py --check
"""

from __future__ import annotations

import argparse
import math
import random
import struct
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / "tests"))

from diff import engine  # noqa: E402

TYPES=['int8','int16','int32','int64','uint8','uint16','uint32','uint64','bool','float32','float64']
SIZES={'int8':1,'uint8':1,'bool':1,'int16':2,'uint16':2,'int32':4,'uint32':4,'float32':4,
       'int64':8,'uint64':8,'float64':8}
VALS={
 'int8':[-3,-1,0,2,5,127,-128], 'int16':[-300,-1,0,2,5,1000,-32768], 'int32':[-100000,-1,0,2,5,100000],
 'int64':[-10**12,-1,0,2,5,10**12], 'uint8':[0,1,2,5,13,255], 'uint16':[0,1,2,5,1000,65535],
 'uint32':[0,1,2,5,100000,4294967295], 'uint64':[0,1,2,5,10**12,18446744073709551615],
 'bool':[False,True], 'float32':[0.5,1.5,2.5,-3.5,4.25,-0.25,1.5e38],
 'float64':[0.5,1.5,2.5,-3.5,4.25,10.125,-0.25,1.7e308],
}

def bits(t,v):
    if t=='bool': return b'\x01' if v else b'\x00'
    if t=='float32': return struct.pack('<f', v)
    if t=='float64': return struct.pack('<d', v)
    if t.startswith('uint'): return int(v).to_bytes(SIZES[t],'little',signed=False)
    return int(v).to_bytes(SIZES[t],'little',signed=True)

def lit(t,v):
    if t=='bool': return 'true' if v else 'false'
    if t=='float32': return f'{v}f'
    if t=='float64': return f'{v}'
    if t.startswith('uint'): return f'{v}u'
    return str(v)

def fmt(t,v):
    if t=='bool': return 'true' if v else 'false'
    if t in ('float32','float64'): return f'{float(v):.6g}'
    return str(v)

def gen(seed):
    rng=random.Random(seed)
    fields=[]
    off=0
    n=rng.randint(1,6)
    for i in range(n):
        t=rng.choice(TYPES)
        pad=rng.choice([0,0,0,0,1,2,3,4,7,8,16])
        fields.append((t,pad))
        off += SIZES[t]+pad
    size=off
    vals=[rng.choice(VALS[t]) for t,_ in fields]
    offsets=[]
    o=0
    for (t,pad),v in zip(fields,vals):
        offsets.append(o)
        o += SIZES[t]+pad
    # image bytes
    image=bytearray(size)
    for (t,pad),v,o2 in zip(fields,vals,offsets):
        b=bits(t,v)
        image[o2:o2+len(b)]=b
    root=['#program','import lib']
    out=[]
    lib=['#lib']
    lib.append('public struct S then')
    for i,(t,pad) in enumerate(fields):
        if pad: lib.append(f'var f{i}: {t} @padding({pad})')
        else: lib.append(f'var f{i}: {t}')
    lib.append('end')
    make=', '.join(lit(t,v) for (t,_),v in zip(fields,vals))
    root.append(f'var s: lib.S = make lib.S, {make}')
    root.append('var sz: usize = sizeof lib.S')
    root.append('_ = writeln usize, sz')
    out.append(str(size))
    root.append('var pa: ptr<lib.S> = addr lib.S, s')
    root.append('var p: ptr<int8> = bitcast ptr<int8>, pa')
    root.append('var p8: ptr<uint8> = bitcast ptr<uint8>, p')
    root.append('unsafe then')
    # field typed loads
    for i,((t,pad),v,o2) in enumerate(zip(fields,vals,offsets)):
        root.append(f'var q{i}: ptr<int8> = ptradd int8, p, {o2}')
        if t=='bool':
            root.append(f'var b{i}: ptr<bool> = bitcast ptr<bool>, q{i}')
            root.append(f'var r{i}: bool = load bool, b{i}')
            root.append(f'_ = writeln bool, r{i}')
        elif t.startswith('uint') or t in ('int8','int16','int32','int64'):
            root.append(f'var q{i}t: ptr<{t}> = bitcast ptr<{t}>, q{i}')
            root.append(f'var r{i}: {t} = load {t}, q{i}t')
            root.append(f'_ = writeln {t}, r{i}')
        elif t=='float32':
            root.append(f'var q{i}t: ptr<float32> = bitcast ptr<float32>, q{i}')
            root.append(f'var r{i}: float32 = load float32, q{i}t')
            root.append(f'_ = writeln float32, r{i}')
        else:
            root.append(f'var q{i}t: ptr<float64> = bitcast ptr<float64>, q{i}')
            root.append(f'var r{i}: float64 = load float64, q{i}t')
            root.append(f'_ = writeln float64, r{i}')
        out.append(fmt(t,v))
    # bytewise image at every offset (limit to first 40 bytes if huge)
    limit=min(size,40)
    for pos in range(limit):
        if pos:
            root.append(f'var pb{pos}: ptr<uint8> = ptradd uint8, p8, {pos}')
        else:
            root.append(f'var pb{pos}: ptr<uint8> = copy ptr<uint8>, p8')
        # cannot nest; declare ptradd only when pos>0
        root.append(f'var bb{pos}: uint8 = load uint8, pb{pos}')
        root.append(f'_ = writeln uint8, bb{pos}')
        out.append(str(image[pos]))
    root.append('end')
    return '\n'.join(root)+'\n', '\n'.join(lib)+'\n', '\n'.join(out)+'\n'

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--seeds", type=int, default=20)
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    if args.seeds < 1:
        parser.error("--seeds must be positive")
    failures = 0
    for seed in range(args.seeds):
        try:
            root, lib, expected = gen(seed)
            case = engine.case(
                f"TC-FUZZ-{seed}", "generated case",
                files={"root.tc": root, "lib.tc": lib}, out=expected)
            errs = engine.run_case(case)
        except (engine.CaseError, OSError, ValueError) as exc:
            print(f"seed {seed}: {exc}")
            failures += 1
            continue
        if errs:
            failures += 1
            for item in errs:
                print(item)
    if failures:
        print(f"S8 layout fuzz suite: {failures} failure(s) over {args.seeds} seed(s)")
        return 1
    print(f"S8 layout fuzz suite: {args.seeds} seeds pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
