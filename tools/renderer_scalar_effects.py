"""Generic, fail-closed scalar effects for the frozen renderer scalar corpus."""
import argparse
import hashlib
import json
import re
from pathlib import Path

import renderer_scalar_analysis as frozen

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "tests/fixtures/renderer-scalar-pilot.json"
BOUNDARIES = ROOT / "tests/fixtures/renderer-scalar-boundaries.json"
RUN_ID = "scalar-effect-engine-v1"


def _s16(value):
    return value - 0x10000 if value & 0x8000 else value


def decode(value):
    word = int(value, 16) if isinstance(value, str) else value
    if not 0 <= word <= 0xFFFFFFFF:
        raise ValueError("instruction word outside u32")
    op = word >> 26
    d = {"word": f"{word:08X}", "primary_opcode": op, "supported": True,
         "record_condition": False}
    if op in (32, 34, 36, 38, 40):
        d.update(operation={32: "lwz", 34: "lbz", 36: "stw", 38: "stb", 40: "lhz"}[op],
                 register=(word >> 21) & 31, base=(word >> 16) & 31,
                 displacement=_s16(word & 0xFFFF), width={32: 32, 34: 8, 36: 32, 38: 8, 40: 16}[op])
    elif op in (58, 62) and (word & 3) == 0:
        d.update(operation="ld" if op == 58 else "std", register=(word >> 21) & 31,
                 base=(word >> 16) & 31,
                 displacement=_s16(word & 0xFFFC), width=64)
    elif op == 14:
        d.update(operation="addi", destination=(word >> 21) & 31,
                 base=(word >> 16) & 31, immediate=_s16(word & 0xFFFF))
    elif op in (24, 25):
        d.update(operation="ori" if op == 24 else "oris", source=(word >> 21) & 31,
                 destination=(word >> 16) & 31,
                 immediate=(word & 0xFFFF) << (16 if op == 25 else 0))
    elif op in (20, 21):
        d.update(operation="rlwimi" if op == 20 else "rlwinm",
                 source=(word >> 21) & 31, destination=(word >> 16) & 31,
                 shift=(word >> 11) & 31, mask_begin=(word >> 6) & 31,
                 mask_end=(word >> 1) & 31, record_condition=bool(word & 1))
    elif op == 30 and ((word >> 2) & 7) == 1:
        d.update(operation="rldicr", source=(word >> 21) & 31,
                 destination=(word >> 16) & 31,
                 shift=((word >> 11) & 31) | (((word >> 1) & 1) << 5),
                 mask_end=((word >> 6) & 31) | (((word >> 5) & 1) << 5),
                 record_condition=bool(word & 1))
    elif op == 31 and ((word >> 1) & 0x3FF) == 444:
        d.update(operation="or", source=(word >> 21) & 31,
                 destination=(word >> 16) & 31, other=(word >> 11) & 31,
                 record_condition=bool(word & 1))
    elif word == 0x4E800020:
        d.update(operation="blr")
    else:
        d.update(operation="unsupported", supported=False)
    return d


def _unsupported(record, facts, reason):
    return {"function": record["function"], "partition": record["partition"],
            "status": "unsupported", "reason": reason, "facts": facts}


def _site(fact, text):
    return f"{fact['address']}: opcode {fact['word']} {text}"


def _boundary_words(boundary):
    if boundary is None:
        return None
    if "instructions" in boundary:
        return [(x["address"].upper(), x.get("word", x.get("bytes", "")).upper())
                for x in boundary["instructions"]]
    return None


def analyze_record(record, boundary):
    """Derive an SSA-like expression/state program without function-name admission."""
    facts = [dict(address=i["address"].upper(), **decode(i["word"]))
             for i in record["instructions"]]
    entry = int(record["function"].removeprefix("sub_"), 16)
    expected = [(f"{entry + 4*index:08X}", instruction["word"].upper())
                for index, instruction in enumerate(record["instructions"])]
    actual = [(i["address"].upper(), i["word"].upper()) for i in record["instructions"]]
    if actual != expected:
        return _unsupported(record, facts, "noncontiguous body or entry mismatch")
    if boundary is None or not boundary.get("decompile_completed", False):
        return _unsupported(record, facts, "independent Ghidra boundary unavailable")
    if _boundary_words(boundary) != actual:
        return _unsupported(record, facts, "independent Ghidra boundary differs from frozen body")
    for fact in facts:
        if not fact["supported"]:
            return _unsupported(record, facts, _site(fact, "unsupported instruction"))
        if fact["record_condition"]:
            return _unsupported(record, facts, _site(fact, "condition-register update"))
    if not facts or facts[-1]["operation"] != "blr":
        fact = facts[-1] if facts else {"address": f"{entry:08X}", "word": "<end>"}
        return _unsupported(record, facts, _site(fact, "missing canonical blr"))
    if any(f["operation"] == "blr" for f in facts[:-1]):
        fact = next(f for f in facts[:-1] if f["operation"] == "blr")
        return _unsupported(record, facts, _site(fact, "return before end of body"))

    mem_positions = [i for i, f in enumerate(facts) if f["operation"] in ("lbz", "lhz", "lwz", "ld", "stw", "stb", "std")]
    last_mem = max(mem_positions, default=-1)
    regs = [{"op": "input", "register": i, "width": 64} for i in range(32)]
    statements, reads, writes = [], [], []
    for index, fact in enumerate(facts[:-1]):
        operation = fact["operation"]
        if operation == "rlwimi" and fact["mask_begin"] > fact["mask_end"]:
            return _unsupported(record, facts, _site(fact, "wrapping rlwimi is outside reviewed semantics"))
        if operation in ("lbz", "lhz", "lwz", "ld", "stw", "stb", "std"):
            if fact["base"] != 3:
                return _unsupported(record, facts, _site(fact, "memory base is not receiver r3"))
            if fact["displacement"] % (fact["width"] // 8):
                return _unsupported(record, facts, _site(fact, "unaligned ordinary-memory access"))
            address = {"op": "receiver_address", "displacement": fact["displacement"], "width": 32}
            if operation in ("lbz", "lhz", "lwz", "ld"):
                if fact["register"] == 3 and index < last_mem:
                    return _unsupported(record, facts, _site(fact, "receiver r3 changes before final memory access"))
                expr = {"op": "memory_read_be", "width": fact["width"], "address": address,
                        "visible_prior_writes": len(writes), "read_id": len(reads)}
                regs[fact["register"]] = expr
                event = {"kind": "read", "site": fact["address"], "width": fact["width"],
                         "displacement": fact["displacement"], "destination": fact["register"],
                         "visible_prior_writes": len(writes), "read_id": expr["read_id"]}
                reads.append(event); statements.append(event)
            else:
                expr = regs[fact["register"]]
                event = {"kind": "write", "site": fact["address"], "width": fact["width"],
                         "displacement": fact["displacement"], "value": expr,
                         "source": fact["register"]}
                writes.append(event); statements.append(event)
            continue
        if operation == "addi":
            source = {"op": "constant", "value": 0, "width": 64} if fact["base"] == 0 else regs[fact["base"]]
            expr = {"op": "add", "left": source,
                    "right": {"op": "constant", "value": fact["immediate"] & 0xFFFFFFFFFFFFFFFF, "width": 64},
                    "width": 64}
            destination = fact["destination"]
        elif operation in ("ori", "oris"):
            expr = {"op": "or", "left": regs[fact["source"]],
                    "right": {"op": "constant", "value": fact["immediate"], "width": 64}, "width": 64}
            destination = fact["destination"]
        elif operation == "or":
            expr = {"op": "or", "left": regs[fact["source"]], "right": regs[fact["other"]], "width": 64}
            destination = fact["destination"]
        elif operation in ("rlwinm", "rlwimi"):
            expr = {"op": operation, "source": regs[fact["source"]], "shift": fact["shift"],
                    "mask_begin": fact["mask_begin"], "mask_end": fact["mask_end"], "width": 64}
            destination = fact["destination"]
            if operation == "rlwimi":
                expr["preserved_destination"] = regs[destination]
        elif operation == "rldicr":
            expr = {"op": operation, "source": regs[fact["source"]], "shift": fact["shift"],
                    "mask_end": fact["mask_end"], "width": 64}
            destination = fact["destination"]
        else:
            return _unsupported(record, facts, _site(fact, "unsupported straight-line operation"))
        if destination == 3 and index < last_mem:
            return _unsupported(record, facts, _site(fact, "receiver r3 changes before final memory access"))
        regs[destination] = expr
        statements.append({"kind": "assign", "site": fact["address"], "destination": destination, "value": expr})

    changed = [i for i, value in enumerate(regs) if value != {"op": "input", "register": i, "width": 64}]
    contract = {"version": 1, "entry": f"{entry:08X}", "statements": statements,
                "memory_reads_ordered": reads, "memory_writes_ordered": writes,
                "register_expressions": {str(i): regs[i] for i in changed},
                "changed_registers": changed, "return": "lr", "calls": [],
                "preconditions": ["Receiver r3 low32 plus signed displacement modulo 2^32",
                                  "Aligned mapped ordinary guest memory; no MMIO or physical aliases",
                                  "Valid caller return context; no concurrent mutation"]}
    return {"function": record["function"], "partition": record["partition"],
            "status": "local_contract", "reason": "", "facts": facts, "contract": contract}


def load_boundaries(path=BOUNDARIES):
    data = json.loads(Path(path).read_text())
    if data.get("language") != "PowerPC:BE:64:64-32addr":
        raise ValueError("unexpected boundary processor language")
    return data


def analyze(manifest=None, boundaries=None):
    manifest = manifest or frozen.load_manifest(MANIFEST)
    boundaries = boundaries or load_boundaries()
    indexed = boundaries["functions"]
    return [analyze_record(record, indexed.get(record["function"])) for record in manifest["functions"]]


def analyze_all(manifest=None, boundaries=None):
    """Named convenience API used by the pipeline cache builder."""
    return analyze(manifest, boundaries)


def _mask32(begin, end):
    bits = [31 - p for p in range(32) if (begin <= p <= end if begin <= end else p >= begin or p <= end)]
    return sum(1 << bit for bit in bits)


def _word_mask64(begin, end):
    first, last = 32 + begin, 32 + end
    return sum(1 << (63 - p) for p in range(64)
               if (first <= p <= last if first <= last else p >= first or p <= last))


def _mask64_end(end):
    return ((1 << 64) - 1) ^ ((1 << (63 - end)) - 1) if end < 63 else (1 << 64) - 1


def _cpp_expr(expr):
    op = expr["op"]
    if op == "input": return f"in[{expr['register']}]"
    if op == "constant": return f"UINT64_C(0x{expr['value'] & 0xFFFFFFFFFFFFFFFF:016X})"
    if op == "add": return f"({_cpp_expr(expr['left'])} + {_cpp_expr(expr['right'])})"
    if op == "or": return f"({_cpp_expr(expr['left'])} | {_cpp_expr(expr['right'])})"
    if op == "memory_read_be":
        return f"read{expr['read_id']}"
    if op in ("rlwinm", "rlwimi"):
        mask = _mask32(expr["mask_begin"], expr["mask_end"])
        rotated = f"EngineRotL32(uint32_t({_cpp_expr(expr['source'])}), {expr['shift']})"
        if op == "rlwinm":
            mask64 = _word_mask64(expr["mask_begin"], expr["mask_end"])
            return f"((uint64_t({rotated}) | (uint64_t({rotated}) << 32)) & UINT64_C(0x{mask64:016X}))"
        old = _cpp_expr(expr["preserved_destination"])
        return f"(({old} & UINT64_C(0xFFFFFFFF00000000)) | uint64_t(({rotated} & UINT32_C(0x{mask:08X})) | (uint32_t({old}) & UINT32_C(0x{(~mask)&0xFFFFFFFF:08X}))))"
    if op == "rldicr":
        return f"(EngineRotL64({_cpp_expr(expr['source'])}, {expr['shift']}) & UINT64_C(0x{_mask64_end(expr['mask_end']):016X}))"
    raise ValueError("unknown IR expression " + op)


def emit_cpp(manifest, results, output, root=ROOT):
    sources = frozen.validate_sources(manifest, root)
    accepted = [r for r in results if r["status"] == "local_contract"]
    by_name = {r["function"]: r for r in manifest["functions"]}
    chunks = ["// Generated scalar effect fixture: original bodies plus symbolic contracts.",
              "static uint64_t EngineReadBE(uint8_t* base, uint32_t address, unsigned width) { uint64_t v=0; for(unsigned i=0;i<width/8;++i) v=(v<<8)|base[address+i]; return v; }",
              "static uint32_t EngineRotL32(uint32_t v,unsigned n) { n&=31; return n ? (v<<n)|(v>>(32-n)) : v; }",
              "static uint64_t EngineRotL64(uint64_t v,unsigned n) { n&=63; return n ? (v<<n)|(v>>(64-n)) : v; }"]
    for item in accepted:
        record = by_name[item["function"]]
        chunks.append(frozen.generated_body(sources[record["source"]], record["function"]))
        name = item["function"]
        lines = [f"static void contract_{name}(PPCContext& ctx, uint8_t* base) {{",
                 "  uint64_t in[32] = {" + ", ".join(f"ctx.r{i}.u64" for i in range(32)) + "};"]
        for statement in item["contract"]["statements"]:
            if statement["kind"] == "assign":
                lines.append(f"  ctx.r{statement['destination']}.u64 = {_cpp_expr(statement['value'])};")
            elif statement["kind"] == "read":
                read_id = statement["read_id"]
                lines.append(f"  const uint64_t read{read_id} = EngineReadBE(base, uint32_t(in[3]) + uint32_t({statement['displacement']}), {statement['width']});")
                lines.append(f"  ctx.r{statement['destination']}.u64 = read{read_id};")
            else:
                lines.append(f"  EngineTraceStore(base, uint32_t(in[3]) + uint32_t({statement['displacement']}), {statement['width']}, {_cpp_expr(statement['value'])});")
        lines.append("}")
        chunks.append("\n".join(lines))
        chunks.append(f"static constexpr uint32_t words_{name}[] = {{" + ", ".join("UINT32_C(0x" + i["word"] + ")" for i in record["instructions"]) + "};")
    chunks.append("static const EngineCase kEngineCases[] = {")
    for item in accepted:
        r = by_name[item["function"]]; name = item["function"]
        contract_sha = hashlib.sha256(json.dumps(item, sort_keys=True, separators=(',', ':')).encode()).hexdigest()
        chunks.append(f'  {{"{name}", &__imp__{name}, &contract_{name}, UINT32_C(0x{name[4:]}), words_{name}, std::size(words_{name}), {str(r["partition"] == "holdout").lower()}, "{r["body_sha256"]}", "{RUN_ID}", "{contract_sha}"}},')
    chunks.append("};")
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text("\n\n".join(chunks) + "\n")
    return len(accepted)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, default=MANIFEST)
    parser.add_argument("--boundaries", type=Path, default=BOUNDARIES)
    parser.add_argument("--emit-cpp", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args(argv)
    manifest = frozen.load_manifest(args.manifest)
    results = analyze(manifest, load_boundaries(args.boundaries))
    if args.emit_cpp: emit_cpp(manifest, results, args.emit_cpp)
    if args.json: args.json.write_text(json.dumps(results, indent=2) + "\n")
    print(json.dumps({"functions": len(results), "local_contract": sum(r["status"] == "local_contract" for r in results), "unsupported": sum(r["status"] == "unsupported" for r in results)}))


if __name__ == "__main__":
    main()
