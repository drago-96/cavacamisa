# cavacamisa

Tools for analysing the card game *cavacamisa* (a.k.a. *straccia camicia*, the
Italian beggar-my-neighbour), with one main goal: find **all infinite games**
and collect statistics on the finite ones along the way.

## The game, as simulated here

- 40-card Italian deck. Only the value matters: `1`, `2`, `3` (asso, due, tre;
  4 of each) are *court cards*, and the other 28 cards are `0`.
- A deal is 40 digits: the first 20 are player A's hand, the last 20 are B's,
  top card first. A plays first.
- Players alternate, playing their top card. After a court card worth `p`, the
  opponent must pay up to `p` cards. If one of them is a court card, the roles
  swap. Otherwise whoever played the last court card takes the pile, puts it
  under their hand and leads the next trick.
- End of the game (`--rule`):
  - `empty` (default): you lose as soon as your hand is empty. This is what all
    the code here has always done.
  - `must`: you lose only when you have to play and have no cards left.

  The two rules give a different winner for only ~0.2% of deals, and the known
  infinite games are the same under both.

There are C(40,12) × 12!/(4!·4!·4!) = 5,586,853,480 × 34,650 =
**193,582,473,082,000** distinct deals.

## Known infinite games

The first infinite game was found on 2017-02-17 (see `infinite/20170217.txt`;
it is also the configuration quoted on Wikipedia):

```
A: 0 0 3 0 2 0 2 0 3 0 3 1 0 0 0 0 0 3 1 0
B: 0 0 1 0 0 2 0 0 0 0 2 0 0 0 0 0 0 0 1 0
```

Five more infinite deals were found later. Together with the 2017 one they fall into 3 distinct cycles.
`cycles.py` runs the game backwards from each cycle and lists *every* deal that
ends in it. That gives **122 infinite deals** in total, all checked by playing
them forward (`infinite/known_cycles.txt`):

| cycle | period (steps / tricks) | deals ending in it |
|---|---|---|
| 1 (contains the 2017 deal) | 273 / 42 | 90 |
| 2 | 232 / 33 | 4 |
| 3 | 78 / 10 | 28 |

Other cycles may exist. Ruling them out requires the exhaustive search
(`orchestrate.py`, below).

## Layout

| path | what |
|---|---|
| `reference.py` | Slow, step-by-step simulator with exact loop detection. The ground truth for the rules. |
| `cvc_engine.h` | Fast game engine: header-only C, also compiles as CUDA. |
| `cvc_check.c` | Runs deals through the engine (to compare with `reference.py`) and benchmarks it. |
| `cvc_enum.c`, `cvc_enum_common.h` | Exhaustive enumerator on CPU threads, with checkpoints and `--resume`. |
| `cvc_gpu.cu`, `cvc_flat.h` | The same enumerator on an NVIDIA GPU (same arguments, same output files). |
| `gpu_check.sh` | On a GPU server: checks `cvc_gpu` against `cvc_enum`, then benchmarks it. |
| `orchestrate.py` | Splits the deal space into shards, runs them, merges the results. |
| `cycles.py` | Cycle of an infinite deal, and every deal that leads into it. |
| `infinite/` | Infinite games found so far. |
| `legacy/` | The 2017 code (Python 2 and C++), kept for history. |

## Usage

```sh
make            # builds cvc_enum and cvc_check
make test       # engine vs reference.py on 20000 random deals

./reference.py 0030202030310000031000100200002000000010           # play one deal
./reference.py --trace 0030202030310000031000100200002000000010   # every card
./cycles.py 0030202030310000031000100200002000000010              # its cycle and basin

./cvc_check --bench=20000000                # single-thread speed on random deals
./cvc_enum --out=runs/x --start=0 --count=1000   # all deals of masks 0..999

./orchestrate.py plan --dir runs/sample --sample 50   # 50 random blocks: unbiased stats
./orchestrate.py plan --dir runs/full                 # the whole space, 4096 shards
./orchestrate.py run --dir runs/full --hours 8        # interrupt and rerun any time
./orchestrate.py status --dir runs/full
./orchestrate.py merge --dir runs/full                # totals, histograms, infinite.txt
```

## Running on a GPU server

You need the CUDA toolkit (`nvcc`), `make`, a C compiler and `python3`.

```sh
make cvc_enum cvc_gpu             # ARCH=sm_90 for H100, sm_80 A100, sm_89 L4/4090 (default: native)
./gpu_check.sh                    # validate, then benchmark; output also in gpu_check_report.txt
./orchestrate.py plan --dir runs/full
./orchestrate.py run --dir runs/full --exe ./cvc_gpu --gpus 0,1,2,3   # one worker per GPU
```

`gpu_check.sh validate` runs `cvc_enum` (CPU) and `cvc_gpu` on the same slices:
both rules, `--stop-lower`, both ends of the deal space, and the mask of the
2017 infinite deal. The output files must be byte-identical. `gpu_check.sh bench`
times 4 slices of 2.3·10⁹ games each, and also the 2025 random sampler if its
source is present (`OLD_CUDA=path/to/cavacamisa_cuda.cu`). Compare games/s
together with the average steps per game, since that is the work per game.

How the GPU version works: thread *t* of a launch plays mask *t*/30, one
thirtieth of its value patterns. It uses a flat event loop (`cvc_flat.h`): one
iteration plays one court card, and a thread starts its next deal as soon as a
game ends, so a warp never waits for its longest game. Histograms live in
shared memory and are flushed once per block. The rare games over the step
limit come back as records and are re-played exactly on the host.
`make cvc_gpu_cpu` builds the same program with the kernel run on CPU threads,
so the GPU code path can be checked without a GPU (`./gpu_check.sh validate ./cvc_gpu_cpu`).

## Output files

Each run folder contains `summary.txt` (counts per outcome), `steps_hist.csv`
and `captures_hist.csv` (exact), and `records.txt`. `records.txt` lists every
infinite deal and every finite game with at least 4096 steps or 1024 captures.

## How it works

- **Engine.** A hand is a 40-bit mask of where its court cards are, plus their
  values packed 2 bits each. "Next court card" is a single count-trailing-zeros,
  so the engine jumps from court card to court card instead of playing the
  zeros one by one. Everything fits in 64-bit registers, with no arrays.
- **Exact loop detection.** At every trick boundary the state is just (leader's
  hand, follower's hand). Brent's cycle detection on these states is exact and
  cheap. The enumerator uses a step limit as a fast filter and re-checks every
  game over the limit exactly, so no limit can cause false positives or misses.
- **Enumeration.** A deal is a court-card mask (ranked in colex order) together
  with one of the 34,650 orderings of the values. Masks are split into shards.
- **`--stop-lower`.** A trick boundary where the leader holds 20 cards is itself
  a deal with the same fate. Stopping a game when it reaches a deal that sorts
  lower saves ~35% of the work and still finds every cycle, because the lowest
  deal of each basin is always played to the end. Stopped games get no length
  statistics.
- **Backward search** (`cycles.py`). One trick can be undone in a few
  ways: try every pile size and who led, and check each candidate by playing
  it forward. Basins are tiny (a few hundred to ~1200 states), so one deal per
  cycle is enough to list every deal that ends in that cycle.

All of this is cross-checked: the engine matches `reference.py` exactly
(winner, steps, captures) on 250,006 deals under both rules. The enumerator
matches an independent Python enumeration, and its results don't change with
batch size. On an RTX 5090, `cvc_gpu` gives byte-identical output files to
`cvc_enum` on all 12 checks of `gpu_check.sh validate`.

## Benchmarks

| implementation | games/s | whole deal space |
|---|---|---|
| `cvc_engine.h`, 1 core of an M1 Max | 1.9·10⁶ | — |
| `cvc_enum`, 10 threads, M1 Max | 2.0·10⁷ (2.7·10⁷ with `--stop-lower`) | ~113 days (~83) |
| 2025 random sampler (`cavacamisa_cuda.cu`), RTX 5090 | 1.0·10⁸ | ~22 days |
| `cvc_gpu`, RTX 5090 | 2.3·10⁹ (3.1·10⁹ with `--stop-lower`) | ~23 h (~17 h) |

The GPU numbers come from 32 random blocks of 16,384 masks: 1.8·10¹⁰ games,
averaging 186 steps per game against 189 for random deals.

The rates are end-to-end (wall clock over all games). They include generating
the deals, which costs ~0.3 ns per deal against ~360 ns to play it.

## Known issues in older code

- `legacy/mano.h`: the ring buffer has 40 slots and uses `start == end` for
  "empty", so a hand holding all 40 cards also looks empty, and `len()` returns
  40 for an empty pile. Harmless where it is used (long-game detection), but
  don't reuse it for winner statistics: B wins collecting the last pile would
  be counted wrong. The true split on random deals is about 49.7% A / 50.3% B.
- `legacy/` uses "more than 100 captures" (C++) or "more than 100 tricks"
  (Python) as a sign of an infinite game. Finite games reach 744 captures.

## History

- 2017: Python scripts based on https://github.com/matthewmayer/beggarmypython,
  adapted to the Italian deck. Then C++ versions, and the first infinite game.
- 2026: new engine, exact exhaustive enumerator, cycle and basin analysis.
