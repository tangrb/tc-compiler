#!/usr/bin/env python3
"""S8 memory-operation fuzz suite.

Typed stores and loads, bool normalization and overlapping memmove images.  Every case is generated deterministically from a fixed seed and its
expected observable behavior is derived by an independent Python model of the
standard rules, never copied from tool output.  ``--seeds N`` runs a larger
campaign than the default gate set.

Run:
    python3 tests/conformance/diff/s8_mem_fuzz.py --check
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

SCALARS=['int8','int16','int32','int64','uint8','uint16','uint32','uint64','bool','float32','float64']
SIZES={'int8':1,'uint8':1,'bool':1,'int16':2,'uint16':2,'int32':4,'uint32':4,
       'float32':4,'int64':8,'uint64':8,'float64':8}
VALS={'int8':[-3,0,5,127,-128],'int16':[-300,0,5,1000,-32768],'int32':[-100000,0,5,100000],
      'int64':[-10**12,0,5,10**12],'uint8':[0,1,2,5,255],'uint16':[0,1,1000,65535],
      'uint32':[0,1,100000,4294967295],'uint64':[0,1,10**12,2**64-1],
      'bool':[False,True],'float32':[0.5,1.5,-3.5,4.25,0.0,-0.0],
      'float64':[0.5,1.5,-3.5,10.125,0.0,-0.0]}

def lit(t,v):
    if t=='bool': return 'true' if v else 'false'
    if t=='float32': return f'{v}f'
    if t=='float64': return f'{v}'
    if t.startswith('uint'): return f'{v}u'
    return str(v)

def enc(t,v):
    if t=='bool': return b'\x01' if v else b'\x00'
    if t=='float32': return struct.pack('<f',v)
    if t=='float64': return struct.pack('<d',v)
    if t.startswith('uint'): return int(v).to_bytes(SIZES[t],'little')
    return int(v).to_bytes(SIZES[t],'little',signed=True)

def dec(t,b):
    if t=='bool': return b[0]!=0
    if t=='float32': return struct.unpack('<f',b)[0]
    if t=='float64': return struct.unpack('<d',b)[0]
    if t.startswith('uint'): return int.from_bytes(b,'little')
    return int.from_bytes(b,'little',signed=True)

def fmt(t,v):
    if t=='bool': return 'true' if v else 'false'
    if t in ('float32','float64'):
        if math.isnan(v): return 'nan'
        if math.isinf(v): return '-inf' if v<0 else 'inf'
        return format(float(v),'.6g')
    return str(v)

def gen(seed):
    rng=random.Random(seed)
    size=rng.randint(8,64)
    pad=size-8
    root=['#program']

    root.append('struct B then')
    if pad: root.append(f'var a: int64 @padding({pad})')
    else: root.append('var a: int64')
    root.append('end')
    av=rng.choice(VALS['int64'])
    root.append(f'var s: B = make B, {lit("int64",av)}')
    image=bytearray(size)
    image[0:8]=enc('int64',av)
    root.append('var pa: ptr<B> = addr B, s')
    root.append('var p: ptr<uint8> = bitcast ptr<uint8>, pa')
    root.append('unsafe then')
    out=[]
    for k in range(20):
        kind=rng.choice(['store','load','memmove1','memmoveT','rawbool'])
        if kind=='store':
            t=rng.choice(SCALARS); sz=SIZES[t]
            off=rng.randint(0,size-sz); v=rng.choice(VALS[t])
            root.append(f'var q{k}: ptr<uint8> = ptradd uint8, p, {off}' if off else f'var q{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'var q{k}t: ptr<{t}> = bitcast ptr<{t}>, q{k}')
            root.append(f'_ = store {t}, q{k}t, {lit(t,v)}')
            image[off:off+sz]=enc(t,v)
        elif kind=='load':
            t=rng.choice(SCALARS); sz=SIZES[t]
            off=rng.randint(0,size-sz)
            root.append(f'var q{k}: ptr<uint8> = ptradd uint8, p, {off}' if off else f'var q{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'var q{k}t: ptr<{t}> = bitcast ptr<{t}>, q{k}')
            root.append(f'var v{k}: {t} = load {t}, q{k}t')
            root.append(f'_ = writeln {t}, v{k}')
            out.append(fmt(t,dec(t,image[off:off+sz])))
        elif kind=='memmove1':
            n=rng.randint(0, min(8,size))
            src=rng.randint(0,size-n); dst=rng.randint(0,size-n)
            root.append(f'var ms{k}: ptr<uint8> = ptradd uint8, p, {src}' if src else f'var ms{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'var md{k}: ptr<uint8> = ptradd uint8, p, {dst}' if dst else f'var md{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'_ = memmove uint8, md{k}, ms{k}, {n}')
            tmp=image[src:src+n]
            image[dst:dst+n]=tmp
        elif kind=='memmoveT':
            t=rng.choice(['int8','int16','int32','float32','int64','float64']); sz=SIZES[t]
            maxn=max(0,size//sz)
            n=rng.randint(0,maxn) if maxn>0 else 0
            src=rng.randint(0,max(0,size-n*sz)); dst=rng.randint(0,max(0,size-n*sz))
            # keep ranges in bounds
            if src+n*sz>size or dst+n*sz>size: continue
            root.append(f'var ts{k}: ptr<uint8> = ptradd uint8, p, {src}' if src else f'var ts{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'var td{k}: ptr<uint8> = ptradd uint8, p, {dst}' if dst else f'var td{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'var ts{k}t: ptr<{t}> = bitcast ptr<{t}>, ts{k}')
            root.append(f'var td{k}t: ptr<{t}> = bitcast ptr<{t}>, td{k}')
            root.append(f'_ = memmove {t}, td{k}t, ts{k}t, {n}')
            tmp=image[src:src+n*sz]
            image[dst:dst+n*sz]=tmp
        else:  # raw bool normalization
            off=rng.randint(0,size-1); raw=rng.randint(0,255)
            root.append(f'var q{k}: ptr<uint8> = ptradd uint8, p, {off}' if off else f'var q{k}: ptr<uint8> = copy ptr<uint8>, p')
            root.append(f'_ = store uint8, q{k}, {raw}u')
            root.append(f'var q{k}b: ptr<bool> = bitcast ptr<bool>, q{k}')
            root.append(f'var v{k}: bool = load bool, q{k}b')
            root.append(f'_ = writeln bool, v{k}')
            image[off]=raw
            out.append('true' if raw!=0 else 'false')
    # final byte image (first 32 bytes)
    limit=min(size,32)
    for pos in range(limit):
        root.append(f'var f{pos}: ptr<uint8> = ptradd uint8, p, {pos}' if pos else f'var f{pos}: ptr<uint8> = copy ptr<uint8>, p')
        root.append(f'var b{pos}: uint8 = load uint8, f{pos}')
        root.append(f'_ = writeln uint8, b{pos}')
        out.append(str(image[pos]))
    root.append('end')
    return '\n'.join(root)+'\n', '\n'.join(out)+'\n'

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
            root, expected = gen(seed)
            case = engine.case(
                f"TC-FUZZ-{seed}", "generated case",
                files={"root.tc": root}, out=expected)
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
        print(f"S8 memory-operation fuzz suite: {failures} failure(s) over {args.seeds} seed(s)")
        return 1
    print(f"S8 memory-operation fuzz suite: {args.seeds} seeds pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
