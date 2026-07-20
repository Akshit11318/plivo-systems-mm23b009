# PRD — The Flaky Network (Plivo Systems Track) — FINAL, LOCKED

## Goal
Lowest gradeable `delay_ms` with **miss ≤ 1%** and **relay bytes ≤ 2.0× raw** (320 B/frame,
both directions, dropped packets count) on **unseen** profiles. Score = stated delay_ms; lower wins.

## Locked decisions (debated & frozen)
| Decision | Choice | Why |
|---|---|---|
| Language | **C++20, disciplined C style** (no exceptions, no STL/heap on wire path) | compile-time checks free; harness expects `make` → `./sender ./receiver`; Rust = toolchain friction for 50 pps; plain C = same code minus safety |
| Architecture | **#3: immediate copy + staggered dup (lag-1) + NACK retx + byte governor** | latency-optimal member of the burst-robust set (see below) |
| Byte cap | **1.95× total = sender 1.93× + receiver 0.02×**, hard-enforced both sides | byte accounting is exact (we count the same bytes the relay counts) → boundary risk ~0; backing to 1.90 costs ~10% dup coverage ≈ +0.3% miss on B |
| Jitter buffer | **NONE** | verified in endpoints.py:52-59 — player scores first arrival before deadline, order-independent → buffering is pure added latency |
| FEC | **Rejected** | source paces frames (PDF: "you cannot send a frame early") → parity for frame i cannot exist before the staggered dup does; same latency, worse recovery prob (needs 2 survivors vs 1) |
| ARQ-only | **Rejected** | recovery = detect(20ms) + RTT(2 hostile draws) ≈ 60–180ms on B → forces delay ~180 |
| Stated delay_ms | **data-driven**: sweep A, B + synthetic burst/spike/high-jitter profiles, lowest passing everywhere + ~10ms margin (expect ~110) | hidden profiles differ in SHAPE; tune the mechanism, not profile A |

## Architecture (3 layers, priority: original > retransmit > duplicate)
1. **Copy 1**: forward frame i to relay the instant it arrives (164B = u32 BE seq + 160B payload).
2. **Copy 2**: separate packet, sent when frame i+1 arrives (+20ms). Lane packet order
   `[orig i][dup i-1][orig i+1][dup i]` → copies always 2 packets apart in the relay's
   per-packet Gilbert-Elliott chain (relay.py:30-42): a 3-packet burst causes ZERO misses.
   Governor skips dups when over 1.93×.
3. **NACK**: receiver detects seq gaps → `0x4E + u16 count + u32 seqs` to 47003; re-NACK every
   ~30ms while frame deadline still reachable (T0/DELAY_MS from env), max 4; receiver self-caps
   at 0.02×. Sender retransmits only if deadline reachable and under 1.93×... retx cap 1.93 shared,
   priority above dup.

## Wire/process layout
- `sender`: binds 47010 (source) + 47004 (feedback), sends 47001. Ring buffer 1024 slots. poll() loop, 5ms tick.
- `receiver`: binds 47002, sends 47020 (player, verbatim 164B — our media format == harness format)
  + 47003 (NACKs). Seen-bitset dedup (relay dups + our dups), gap scan from a sliding base.

## Predictions (hold me to these)
A: VALID @ ~45ms, miss ~0.3%. B: VALID @ ~100ms, miss ~0.8%, overhead ~1.95×. Stated ≈ 110.

## Rules (process — non-negotiable)
1. **Never modify harness files** (run.py, relay.py, endpoints.py, common.py, score.py, profiles/).
2. Deliverables exactly as PDF: source + Makefile (`make` → `./sender`, `./receiver`), RUNLOG.md
   (graded; per experiment: profile, delay_ms, miss %, overhead, what changed & why), NOTES.md
   (≤10 sentences, stated delay_ms, what breaks it), SUMMARY.html (architecture deep-dive).
3. **Docs updated at every commit** — RUNLOG.md grows with each real run; no undocumented experiments.
4. Commits: plain messages, **no Co-Authored-By lines**, milestone-sized.
5. CHANGELOG.local.md = private engineering log (gitignored, Haiku agent maintains it).
6. Final repo: public GitHub `plivo-systems-mm23b009`, all deliverables at root, untouched after deadline.
7. No side channels between sender/receiver (relay ports only); stdlib only.

## Stages
1. ✅ PRD + design debate frozen
2. sender.cpp / receiver.cpp / Makefile → build → VALID on A @ 60 [commit]
3. Sweeps: A (60→45), B (110→95) + synthetic C_burst, D_spike/high-jitter [commit]
4. RUNLOG.md + NOTES.md + SUMMARY.html finalized [commit]
5. Assemble clean public repo, push, verify `make` from scratch, submit form
