> Historical as of 2026-09-22; current status in [renderer-status.md](renderer-status.md).

# Remaining setter families

Priority is a heuristic: straight-line integer first, then conditional, floating-point, transitive; coverage descending within each tier. Mnemonic similarity is not semantic equivalence or promised rule yield.

The fixed v1 baseline has 63 unsupported functions in 25 groups. The current rules leave 51 unsupported. Every group belongs to R09.effects; local acceptance leaves receiver, lifetime and mode obligations open.

Regenerate: `python tools/rank-renderer-setter-families.py`. Exact members, dependencies, actions and completion tests are in `out/renderer-automation-pilot/setter-backlog.json`.

| Rank | Group | Functions | Remaining | Complexity | Instruction structure |
|---|---|---:|---:|---|---|
| 1 | SET-5716e66ce6 | 12 | 0 | straight-line integer | lwz, rlwinm, li, rlwinm, rldicr, or, stw, ld, or, std, blr |
| 2 | SET-3c4cc29f7c | 7 | 7 | straight-line integer | lwz, li, rlwimi, rldicr, stw, ld, ori, std, or, std, blr |
| 3 | SET-6d7b561c2d | 5 | 5 | straight-line integer | lwz, li, rlwimi, rldicr, stw, ld, or, std, blr |
| 4 | SET-4aee0fd6c5 | 2 | 2 | straight-line integer | lwz, li, rlwinm, rldicr, or, stw, ld, or, std, blr |
| 5 | SET-4157f5f6d4 | 1 | 1 | straight-line integer | lwz, rlwimi, stw, blr |
| 6 | SET-17940d8b3e | 1 | 1 | straight-line integer | stw, ld, oris, std, blr |
| 7 | SET-96dc214afc | 1 | 1 | straight-line integer | rlwinm, stw, ld, oris, std, blr |
| 8 | SET-d07b20ea27 | 1 | 1 | straight-line integer | subfic, subfe, rlwinm, stw, lwz, rlwinm, or, stw, ld, ori, std, ld, oris, std, blr |
| 9 | SET-69aa0a43a7 | 4 | 4 | conditional integer | lwz, stw, cmplwi, bne, li, lwz, rlwimi, stw, ld, oris, std, blr |
| 10 | SET-62a4fda6fd | 4 | 4 | conditional integer | lwz, stw, cmplwi, beqlr, lwz, rlwinm, cmplwi, beq, cmplwi, beq, cmplwi, beq, cmplwi, bnelr, rlwinm, xor., beqlr, lwz, subi, li, rlwinm, rlwinm, rlwinm, addi, subi, rlwinm, and, andc, rldicr, or, rlwinm, or, stw, lwz, rlwinm, or, stw, ld, or, std, blr |
| 11 | SET-f3bb2560bb | 3 | 3 | conditional integer | lwz, lwz, rlwimi, stw, rlwinm., beqlr, rlwinm., beqlr, rlwinm, stw, stw, stw, stw, ld, ori, std, ori, std, ori, std, ori, std, blr |
| 12 | SET-360ddc32ed | 3 | 3 | conditional integer | lwz, lwz, rlwimi, stw, rlwinm., beqlr, rlwinm., lwz, bne, rlwinm, rlwinm, andi., or, rlwinm, rlwinm, rlwinm, or, stw, stw, stw, stw, ld, ori, std, ori, std, ori, std, ori, std, blr |
| 13 | SET-5e4b9f6dc0 | 2 | 2 | conditional integer | lwz, stw, cmplwi, bne, li, lwz, li, rlwimi, rldicr, stw, ld, ori, std, or, std, blr |
| 14 | SET-f7a2d8bc34 | 1 | 1 | conditional integer | cmplwi, li, bne, li, stw, cntlzw, lwz, rlwinm, rlwimi, stw, ld, ori, std, ori, std, blr |
| 15 | SET-8d37dd1e23 | 1 | 1 | conditional integer | lwz, rlwimi, stw, rlwinm., lwz, bne, rlwinm, rlwinm, andi., or, rlwinm, rlwinm, rlwinm, or, cmpwi, bne, lis, ori, stw, stw, stw, stw, ld, ori, std, ori, std, ori, std, ori, std, blr |
| 16 | SET-ac0329ef66 | 1 | 1 | conditional integer | lwz, cmpwi, rlwimi, rlwinm, stw, lwz, bne, rlwinm, rlwinm, andi., or, rlwinm, rlwinm, rlwinm, or, cmpwi, bne, lis, ori, stw, stw, stw, stw, ld, ori, std, ori, std, ori, std, ori, std, blr |
| 17 | SET-366ade272f | 5 | 5 | floating-point | stw, li, rldicr, lfs, stfs, ld, or, std, blr |
| 18 | SET-4b4714f9dd | 2 | 2 | floating-point | stw, lis, li, rldicr, lfs, subi, lfs, fmuls, stfs, fctiwz, stfiwx, lwz, sth, ld, or, std, blr |
| 19 | SET-5aa97f2c0a | 1 | 1 | floating-point | stw, lfs, stfs, ld, oris, std, blr |
| 20 | SET-61dd1cccdb | 1 | 1 | floating-point | rldicl, std, lis, lfd, fcfid, frsp, lfs, fmuls, stfs, ld, ori, std, blr |
| 21 | SET-c1b90cd410 | 1 | 1 | floating-point | stw, lis, li, rldicr, lfs, subi, lfs, fmuls, stfs, fctiwz, stfiwx, lwz, rlwinm, sth, sth, ld, or, std, blr |
| 22 | SET-24fa0d3dd0 | 1 | 1 | floating-point | stw, lis, lfs, lfs, fcmpu, lfs, stfs, stfs, bne, fcmpu, li, beq, li, lwz, lfs, fcmpu, rlwimi, stw, bne, fcmpu, li, beq, li, rlwimi, li, rldicr, stw, ld, or, li, rldicr, std, or, std, ld, ori, std, blr |
| 23 | SET-d91dc64c96 | 1 | 1 | floating-point | rlwinm, rlwinm, rlwinm, rldicl, rldicl, rldicl, rldicl, std, std, lis, std, std, lfd, lfd, fcfid, lfd, fcfid, lfd, fcfid, fcfid, frsp, frsp, lfs, frsp, frsp, fmuls, stfs, fmuls, stfs, fmuls, stfs, fmuls, stfs, ld, oris, ori, std, blr |
| 24 | SET-578a7be133 | 1 | 1 | floating-point | stw, lis, lfs, lis, lfs, fmuls, lfs, stfs, stfs, fcmpu, bne, lfs, li, fcmpu, beq, li, lwz, fcmpu, rlwimi, stw, bne, lfs, li, fcmpu, beq, li, rlwimi, li, rldicr, stw, ld, or, li, rldicr, std, or, std, ld, ori, std, blr |
| 25 | SET-f8bcaf81ab | 1 | 1 | transitive/shared-tail | mfspr, stw, std, std, stwu, lwz, lwz, lwz, lwz, lwz, lwz, lwz, cmpwi, lwz, lwz, add, stw, add, stw, stw, stw, beq, cmpw, bgt, or, cmpw, bgt, or, cmpw, blt, or, cmpw, blt, or, lwz, lwz, rlwimi, rlwimi, rlwimi, rlwimi, stw, stw, bl, addi, lwz, mtspr, ld, ld, blr, or, addi, stw, b |

For each straight-line group, decode operands and exact clobbers before writing a rule. Conditional groups require path-specific effects; floating-point groups require rounding/status semantics; transitive groups require callee summaries. Completion requires exact-byte matching and actual guest execution with state, memory and ordered-effect checks, or explicit member-level rejection.

The nine raw stack/floating-store functions optimized out of high-p-code STORE selection remain separate investigations documented in the setter pilot. They are not silently included in or removed from this 63-function baseline.
