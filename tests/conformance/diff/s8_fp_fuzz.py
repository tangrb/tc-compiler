#!/usr/bin/env python3
"""S8 floating-point fuzz suite.

Integer/float conversions, NaN/inf/zero formatting, comparisons and IEEE arithmetic.  Every case is generated deterministically from a fixed seed and its
expected observable behavior is derived by an independent Python model of the
standard rules, never copied from tool output.  ``--seeds N`` runs a larger
campaign than the default gate set.

Run:
    python3 tests/conformance/diff/s8_fp_fuzz.py --check
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

def bits_to_f64(bits): return struct.unpack('<d', int(bits).to_bytes(8,'little'))[0]
def bits_to_f32(bits): return struct.unpack('<f', int(bits).to_bytes(4,'little'))[0]
def f64_to_bits(v): return struct.unpack('<Q', struct.pack('<d', v))[0]
def f32_to_bits(v): return struct.unpack('<I', struct.pack('<f', v))[0]
def lit64(bits): return f'0x{bits:016x}u'
def lit32(bits): return f'0x{bits:08x}u'
def fmt(v):
    if math.isnan(v): return 'nan'
    if math.isinf(v): return '-inf' if v<0 else 'inf'
    return format(v,'.6g')

def rand64(rng): return rng.getrandbits(64)
def rand32(rng): return rng.getrandbits(32)

def f64_add(a,b): return a+b
def f64_sub(a,b): return a-b
def f64_mul(a,b): return a*b
def f64_div(a,b):
    if math.isnan(a) or math.isnan(b): return float('nan')
    if b==0.0:
        if a==0.0: return float('nan')
        sign = math.copysign(1.0,a)*math.copysign(1.0,b)
        return math.copysign(float('inf'), sign)
    return a/b
def f64_mod(a,b):
    if math.isnan(a) or math.isnan(b) or math.isinf(a) or b==0.0: return float('nan')
    if math.isinf(b): return a
    return math.fmod(a,b)

def f32_round(v):
    try:
        return bits_to_f32(f32_to_bits(v))
    except OverflowError:
        return math.copysign(float('inf'), v)
def f32_add(a,b):
    return f32_round(a+b)
def f32_sub(a,b):
    return f32_round(a-b)
def f32_mul(a,b):
    # exact in binary64 for binary32 operands
    return f32_round(a*b)
def f32_div(a,b):
    if math.isnan(a) or math.isnan(b): return float('n32an')
    if b==0.0:
        if a==0.0: return float('nan')
        sign = math.copysign(1.0,a)*math.copysign(1.0,b)
        return math.copysign(float('inf'), sign)
    return f32_round(a/b)

def gen(seed):
    rng=random.Random(seed)
    root=['#program']; out=[]
    # 1. int64 -> float64 bits and back
    ints=[0,1,-1,2,-2,3,123456789,-123456789,(1<<53)-1,-((1<<53)-1),1<<53,-(1<<53),10**15,-10**15,10**18,-10**18]
    for i,v in enumerate(ints):
        root.append(f'var i{i}: int64 = copy int64, {v}')
        root.append(f'var f{i}: float64 = cast float64, i{i}')
        root.append(f'var u{i}: uint64 = bitcast uint64, f{i}')
        root.append(f'_ = writeln uint64, u{i}')
        out.append(str(f64_to_bits(float(v))))
        root.append(f'var t{i}: int64 = cast int64, f{i}')
        root.append(f'_ = writeln int64, t{i}')
        out.append(str(int(float(v))))
    # 2. comparisons on arbitrary float64 bit patterns
    for i in range(8):
        a=rand64(rng); b=rand64(rng)
        root.append(f'var ca{i}: float64 = bitcast float64, {lit64(a)}')
        root.append(f'var cb{i}: float64 = bitcast float64, {lit64(b)}')
        va=bits_to_f64(a); vb=bits_to_f64(b)
        for op,expr in (('eq',lambda x,y:x==y),('ne',lambda x,y:x!=y),('lt',lambda x,y:x<y),
                        ('le',lambda x,y:x<=y),('gt',lambda x,y:x>y),('ge',lambda x,y:x>=y)):
            root.append(f'var c{i}{op}: bool = {op} float64, ca{i}, cb{i}')
            root.append(f'_ = writeln bool, c{i}{op}')
            out.append('true' if expr(va,vb) else 'false')
    # 3. formatting of arbitrary values
    for i in range(10):
        a=rand64(rng)
        root.append(f'var fa{i}: float64 = bitcast float64, {lit64(a)}')
        root.append(f'_ = writeln float64, fa{i}')
        out.append(fmt(bits_to_f64(a)))
        c=rand32(rng)
        root.append(f'var fb{i}: float32 = bitcast float32, {lit32(c)}')
        root.append(f'_ = writeln float32, fb{i}')
        out.append(fmt(bits_to_f32(c)))
    # 4. .ieee add/sub/mul/div/mod with arbitrary finite operands (expected via Python IEEE)
    for i in range(8):
        # choose finite normal operands with moderate exponent to keep semantics simple
        def finite(rng):
            sign=rng.getrandbits(1)<<63
            exp=rng.randint(1,2046)
            frac=rng.getrandbits(52)
            return sign|(exp<<52)|frac
        a=finite(rng); b=finite(rng)
        root.append(f'var ia{i}: float64 = bitcast float64, {lit64(a)}')
        root.append(f'var ib{i}: float64 = bitcast float64, {lit64(b)}')
        va=bits_to_f64(a); vb=bits_to_f64(b)
        for op,func in (('add',f64_add),('sub',f64_sub),('mul',f64_mul)):
            root.append(f'var io{i}{op}: float64 = {op}.ieee float64, ia{i}, ib{i}')
            root.append(f'_ = writeln float64, io{i}{op}')
            out.append(fmt(func(va,vb)))
        root.append(f'var id{i}: float64 = div.ieee float64, ia{i}, ib{i}')
        root.append(f'_ = writeln float64, id{i}')
        out.append(fmt(f64_div(va,vb)))
        root.append(f'var im{i}: float64 = mod.ieee float64, ia{i}, ib{i}')
        root.append(f'_ = writeln float64, im{i}')
        out.append(fmt(f64_mod(va,vb)))
    # 5. float32 .ieee add/sub/mul with expected (exact double intermediate)
    for i in range(6):
        a=rand32(rng); b=rand32(rng)
        root.append(f'var ja{i}: float32 = bitcast float32, {lit32(a)}')
        root.append(f'var jb{i}: float32 = bitcast float32, {lit32(b)}')
        va=bits_to_f32(a); vb=bits_to_f32(b)
        for op,func in (('add',f32_add),('sub',f32_sub),('mul',f32_mul)):
            root.append(f'var jo{i}{op}: float32 = {op}.ieee float32, ja{i}, jb{i}')
            root.append(f'_ = writeln float32, jo{i}{op}')
            out.append(fmt(func(va,vb)))
    # 6. strict arithmetic with exactly representable operands
    exact_pairs=[(1.5,2.5),(4.0,2.0),(0.5,0.25),(8.0,0.5)]
    for i,(x,y) in enumerate(exact_pairs):
        root.append(f'var sa{i}: float64 = copy float64, {x}')
        root.append(f'var sb{i}: float64 = copy float64, {y}')
        for op in ('add','sub','mul','div'):
            root.append(f'var so{i}{op}: float64 = {op} float64, sa{i}, sb{i}')
            root.append(f'_ = writeln float64, so{i}{op}')
            val={'add':x+y,'sub':x-y,'mul':x*y,'div':x/y}[op]
            out.append(fmt(val))
        root.append(f'var sm{i}: float64 = mod float64, sa{i}, sb{i}')
        root.append(f'_ = writeln float64, sm{i}')
        out.append(fmt(math.fmod(x,y)))
    return '\n'.join(root)+'\n', '\n'.join(out)+'\n'

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--seeds", type=int, default=48)
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
        print(f"S8 floating-point fuzz suite: {failures} failure(s) over {args.seeds} seed(s)")
        return 1
    print(f"S8 floating-point fuzz suite: {args.seeds} seeds pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
