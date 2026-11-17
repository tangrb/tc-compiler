#!/usr/bin/env python3
"""S8 ABI fuzz suite.

Random struct signatures, HFA/large composites, scalar and aggregate arguments and indirect calls.  Every case is generated deterministically from a fixed seed and its
expected observable behavior is derived by an independent Python model of the
standard rules, never copied from tool output.  ``--seeds N`` runs a larger
campaign than the default gate set.

Run:
    python3 tests/conformance/diff/s8_abi_fuzz.py --check
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

SCALARS = ['int8','int16','int32','int64','uint8','uint16','uint32','uint64','bool','float32','float64']
INT_TYPES = ['int8','int16','int32','int64','uint8','uint16','uint32','uint64']
FLOAT_TYPES = ['float32','float64']
VALUES = {
 'int8': [-3,-1,0,2,5], 'int16': [-3,-1,0,2,5,300,1000], 'int32': [-3,-1,0,2,5,100000],
 'int64': [-3,-1,0,2,5,10**12],
 'uint8': [0,1,2,5,13], 'uint16': [0,1,2,5,13,1000], 'uint32': [0,1,2,5,13,100000],
 'uint64': [0,1,2,5,13,10**12],
 'bool': [False,True],
 'float32': [0.5,1.5,2.5,-3.5,4.25,-0.25],
 'float64': [0.5,1.5,2.5,-3.5,4.25,10.125,-0.25],
}

def py_lit(t, v):
    if t == 'bool': return 'true' if v else 'false'
    if t == 'float32': return f'{v}f'
    if t == 'float64': return f'{v}'
    return str(v)

def fmt(t, v):
    if t == 'bool': return 'true' if v else 'false'
    if t in FLOAT_TYPES: return f'{float(v):.6g}'
    return str(v)

def cast_i64(t, v):
    if t == 'bool': return 1 if v else 0
    return int(v)

class Case:
    def __init__(self, seed):
        self.rng = random.Random(seed)
        self.types = []
        self.lib = []
        self.root = ['#program','import lib']
        self.out = []
        self.structs = []   # (name, fields, values, sumfunc)
        self.counter = 0

    def uniq(self, prefix):
        self.counter += 1
        return f'{prefix}{self.counter}'

    def struct(self):
        rng=self.rng
        name=self.uniq('S')
        kind=rng.choice(['mixed','int','hfa32','hfa64','mixed','int'])
        if kind=='hfa32':
            n=rng.randint(1,4); fields=['float32']*n
        elif kind=='hfa64':
            n=rng.randint(1,4); fields=['float64']*n
        else:
            n=rng.randint(1,5)
            pool=INT_TYPES+['bool']+(['float32','float64'] if kind=='mixed' else [])
            fields=[rng.choice(pool) for _ in range(n)]
        self.types.append(f'public struct {name} then')
        for i,t in enumerate(fields):
            pad=rng.choice([0,0,0,0,1,2,3,4,8,16])
            if pad:
                self.types.append(f'var f{i}: {t} @padding({pad})')
            else:
                self.types.append(f'var f{i}: {t}')
        self.types.append('end')
        vals=[rng.choice(VALUES[t]) for t in fields]
        sumname=f'sum{name}'
        self.lib.append(f'public func id{name}(x: {name}) {name} then')
        self.lib.append('_ = return x')
        self.lib.append('end')
        self.lib.append(f'public func {sumname}(x: {name}) int64 then')
        prev=None
        for i,t in enumerate(fields):
            self.lib.append(f'var c{i}: int64 = cast int64, x.f{i}')
            if prev is None:
                self.lib.append(f'var s{i}: int64 = copy int64, c{i}')
            else:
                self.lib.append(f'var s{i}: int64 = add int64, s{prev}, c{i}')
            prev=i
        if prev is None:
            self.lib.append('var s0: int64 = copy int64, 0'); prev=0
        self.lib.append(f'_ = return s{prev}')
        self.lib.append('end')
        self.structs.append((name,fields,vals,sumname))
        return self.structs[-1]

    def struct_value(self, st, prefix):
        name,fields,vals,sumname=st
        varname=self.uniq(prefix)
        args=', '.join(py_lit(t,v) for t,v in zip(fields, vals))
        self.root.append(f'var {varname}: lib.{name} = make lib.{name}, {args}')
        return varname

    def emit_struct_checks(self):
        for st in self.structs:
            name,fields,vals,sumname=st
            v=self.struct_value(st,'v')
            w=self.uniq('w')
            self.root.append(f'var {w}: lib.{name} = call lib.id{name}, {v}')
            for i,(t,vv) in enumerate(zip(fields,vals)):
                self.root.append(f'_ = writeln {t}, {w}.f{i}')
                self.out.append(fmt(t,vv))
            r=self.uniq('rs')
            self.root.append(f'var {r}: int64 = call lib.{sumname}, {v}')
            self.root.append(f'_ = writeln int64, {r}')
            self.out.append(str(sum(cast_i64(t,vv) for t,vv in zip(fields,vals))))

    def emit_mix(self):
        rng=self.rng
        params=[]
        nparams=rng.randint(0,12)
        for _ in range(nparams):
            if self.structs and rng.random()<0.3:
                params.append(('struct', rng.choice(self.structs)))
            else:
                params.append(('scalar', rng.choice(SCALARS)))
        decls=[]; body=[]; contribution=0; call_args=[]
        for pi,(kind,payload) in enumerate(params):
            if kind=='struct':
                name,fields,vals,sumname=payload
                var=self.struct_value(payload,'p')
                decls.append(f'p{pi}: {name}')
                call_args.append(var)
                # callee calls sum function
                body.append(f'var q{pi}: int64 = call Self.{sumname}, p{pi}')
                body.append(f'var c{pi}: int64 = copy int64, q{pi}')
                contribution += sum(cast_i64(t,vv) for t,vv in zip(fields,vals))
            else:
                t=payload
                v=rng.choice(VALUES[t])
                decls.append(f'p{pi}: {t}')
                call_args.append(py_lit(t,v))
                body.append(f'var c{pi}: int64 = cast int64, p{pi}')
                contribution += cast_i64(t,v)
            if pi==0:
                body.append(f'var s{pi}: int64 = copy int64, c{pi}')
            else:
                body.append(f'var s{pi}: int64 = add int64, s{pi-1}, c{pi}')
        if not params:
            body.append('var s0: int64 = copy int64, 0')
        mixname=self.uniq('mix')
        self.lib.append(f'public func {mixname}({", ".join(decls)}) int64 then')
        self.lib += body
        self.lib.append(f'_ = return s{len(params)-1 if params else 0}')
        self.lib.append('end')
        args=', '.join(call_args)
        r=self.uniq('r')
        self.root.append(f'var {r}: int64 = call lib.{mixname}, {args}' if args else f'var {r}: int64 = call lib.{mixname}')
        self.root.append(f'_ = writeln int64, {r}')
        self.out.append(str(contribution))
        # indirect call with the same signature
        param_types=[payload if kind=='scalar' else f'lib.{payload[0]}' for kind,payload in params]
        fpname=self.uniq('fp')
        self.root.append(f'var {fpname}: fnptr<int64{", " if param_types else ""}{", ".join(param_types)}> = funcaddr lib.{mixname}')
        self.root.append('unsafe then')
        ri=self.uniq('ri')
        self.root.append(f'var {ri}: int64 = call_indirect {fpname}, {args}' if args else f'var {ri}: int64 = call_indirect {fpname}')
        self.root.append(f'_ = writeln int64, {ri}')
        self.root.append('end')
        self.out.append(str(contribution))

    def text(self):
        libtext='\n'.join(['#lib']+self.types+self.lib)+'\n'
        return '\n'.join(self.root)+'\n', libtext

def build(seed):
    c=Case(seed)
    # 1-2 random structs
    for _ in range(c.rng.randint(1,2)):
        c.struct()
    c.emit_struct_checks()
    c.emit_mix()
    return c.text(), '\n'.join(c.out)+'\n'

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--seeds", type=int, default=24)
    args = parser.parse_args()
    if not args.check:
        parser.error("use --check")
    if args.seeds < 1:
        parser.error("--seeds must be positive")
    failures = 0
    for seed in range(args.seeds):
        try:
            (root, lib), expected = build(seed)
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
        print(f"S8 ABI fuzz suite: {failures} failure(s) over {args.seeds} seed(s)")
        return 1
    print(f"S8 ABI fuzz suite: {args.seeds} seeds pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
