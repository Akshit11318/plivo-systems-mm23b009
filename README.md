# The Flaky Network — Plivo Systems Assignment

Sender/receiver pair that carries a 160-byte-per-20ms live stream across a hostile UDP relay
(drops, jitter, reordering, duplication, burst loss) with ≤1% deadline misses and ≤2.0×
bandwidth, at the lowest possible playout delay.

**Grade at `delay_ms = 108`** (see NOTES.md).

## Build & run

```bash
make                                                   # -> ./sender ./receiver (C++20, stdlib only)
python3 run.py --profile profiles/A.json --delay_ms 60
python3 run.py --profile profiles/B.json --delay_ms 108
```

## Layout

| path | what |
|---|---|
| `src/sender.cpp`, `src/receiver.cpp` | the two programs (single-threaded, nonblocking, zero heap after startup) |
| `Makefile` | `make` → `./sender`, `./receiver` at repo root (where run.py expects them) |
| `run.py`, `relay.py`, `endpoints.py`, `common.py`, `score.py` | harness — unmodified handout copies |
| `profiles/A.json`, `B.json` | handout practice profiles |
| `profiles/C_burst*.json`, `D_spike.json`, `E_clean.json`, `F_jitter.json` | our synthetic stress profiles (burst loss, delay spikes, extreme jitter) used to tune for the mechanism, not for profile A |
| `RUNLOG.md` | every experiment in order, with what changed and why (graded) |
| `NOTES.md` | design in ≤10 sentences + stated delay_ms + what breaks it |
| `SUMMARY.html` | architecture deep-dive |
| `PRD.md` | the plan the work followed, decisions and predictions frozen before coding |

## Design in one paragraph

The receiver has **no jitter buffer** — the harness player scores the *first* correct arrival
against each frame's deadline (endpoints.py), so the receiver dedups and forwards instantly and
all jitter absorption lives in the delay_ms number itself. Every frame crosses the relay twice:
once the instant it arrives, and once as a staggered duplicate one frame period later — a
separate packet, kept ~7 packets apart from the original in the relay's per-packet burst-loss
Markov chain by two interleaved 1-byte chaff packets (wire format: u16 seq + payload = 162B),
so loss bursts rarely span both copies.
A NACK layer retransmits the rare frames that lose both copies, skipping frames whose deadline
is already unreachable. A byte governor counts every byte before allowing a send (priority:
original > retransmit > duplicate) against the same totals the relay counts, which makes the
2.0× overhead cap unbreakable by construction rather than probable by measurement.
