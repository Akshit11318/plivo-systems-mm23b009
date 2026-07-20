# RUNLOG — every experiment, in order. (Graded deliverable)

Setup: C++20 sender/receiver, 3-layer recovery (immediate copy → staggered dup at +1 frame →
NACK retransmit), byte governor at 1.93× uplink + 0.02× feedback. No jitter buffer — the player
scores first arrival before deadline, so the receiver forwards instantly and all jitter
absorption lives in delay_ms itself.

| # | profile | delay_ms | miss % | overhead | result | change / why |
|---|---------|----------|--------|----------|--------|--------------|
| 0 | A | 40 | ~2% (baseline .c) | 1.00× | INVALID | handout baseline: every drop = miss. Confirms loss, not jitter, is the first enemy. |
| 1 | A | 60 | 0.40% | 1.93× | VALID | first run of the 3-layer design. Governor lands exactly at its 1.93× cap; 6/1500 misses = double-drops of both copies, as modeled (~p²+skip·p). |
