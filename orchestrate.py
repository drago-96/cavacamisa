#!/usr/bin/env python3
"""Runs the exhaustive enumeration in resumable shards and merges the results.

  ./orchestrate.py plan   --dir runs/full --shards 4096        # whole deal space
  ./orchestrate.py plan   --dir runs/sample --sample 20        # 20 random blocks of 160 masks (unbiased stats)
  ./orchestrate.py run    --dir runs/full [--threads 10] [--hours 8]
  ./orchestrate.py status --dir runs/full
  ./orchestrate.py merge  --dir runs/full                      # totals + infinite deals by cycle

The space is the 5,586,853,480 court-card masks (x 34,650 value patterns each).
A shard is a range of masks run by ./cvc_enum in its own folder, so shards can
be run in any order, on different machines, and interrupted at any time.
"""
import argparse
import json
import os
import random
import subprocess
import sys
import time
from collections import Counter

HERE = os.path.dirname(os.path.abspath(__file__))
NMASKS = 5_586_853_480
NPAT = 34_650


def read_summary(path):
    d = {}
    try:
        for line in open(path):
            k, v = line.split()
            d[k] = v if k == "rule" else int(v)
    except FileNotFoundError:
        return None
    return d


def load_plan(run_dir):
    return json.load(open(os.path.join(run_dir, "plan.json")))


def shard_dir(run_dir, i):
    return os.path.join(run_dir, f"shard_{i:05d}")


def shard_state(run_dir, i, shard):
    s = read_summary(os.path.join(shard_dir(run_dir, i), "summary.txt"))
    done = 0 if s is None else s["next_rank"] - s["first_rank"]
    return s, done, done >= shard["count"]


def cmd_plan(a):
    if os.path.exists(os.path.join(a.dir, "plan.json")):
        sys.exit(f"{a.dir} already has a plan")
    os.makedirs(a.dir, exist_ok=True)
    if a.sample:
        rng = random.Random(a.seed)
        blocks = rng.sample(range(NMASKS // a.block), a.sample)
        shards = [{"start": b * a.block, "count": a.block} for b in sorted(blocks)]
    else:
        step = -(-NMASKS // a.shards)
        shards = [{"start": s, "count": min(step, NMASKS - s)} for s in range(0, NMASKS, step)]
    plan = {"rule": a.rule, "stop_lower": a.stop_lower, "shards": shards}
    json.dump(plan, open(os.path.join(a.dir, "plan.json"), "w"), indent=1)
    games = sum(s["count"] for s in shards) * NPAT
    print(f"{len(shards)} shards, {games:,} games")


def cmd_run(a):
    plan = load_plan(a.dir)
    exe = os.path.join(HERE, "cvc_enum")
    if not os.path.exists(exe):
        subprocess.run(["make", "-C", HERE, "cvc_enum"], check=True)
    deadline = time.time() + a.hours * 3600 if a.hours else None
    for i, sh in enumerate(plan["shards"]):
        if deadline and time.time() > deadline:
            print("time budget used up; run again to continue")
            break
        s, done, complete = shard_state(a.dir, i, sh)
        if complete:
            continue
        cmd = [exe, f"--out={shard_dir(a.dir, i)}", f"--threads={a.threads}"]
        if s is not None:
            cmd.append("--resume")
        else:
            cmd += [f"--start={sh['start']}", f"--count={sh['count']}", f"--rule={plan['rule']}"]
            if plan["stop_lower"]:
                cmd.append("--stop-lower")
        if sh["count"] < 4096:
            cmd.append(f"--batch={max(1, sh['count'])}")
        print(f"shard {i}/{len(plan['shards'])}", flush=True)
        r = subprocess.run(cmd)
        if r.returncode != 0:
            sys.exit(f"cvc_enum failed on shard {i}")


def cmd_status(a):
    plan = load_plan(a.dir)
    total = sum(s["count"] for s in plan["shards"])
    done = 0
    for i, sh in enumerate(plan["shards"]):
        done += shard_state(a.dir, i, sh)[1]
    print(f"{done:,}/{total:,} masks ({100 * done / total:.4f}%), {done * NPAT:,} games")


def cmd_merge(a):
    sys.path.insert(0, HERE)
    import cycles
    import reference as R

    plan = load_plan(a.dir)
    counts, steps, caps = Counter(), Counter(), Counter()
    loops, longest, masks = [], [], 0
    for i, sh in enumerate(plan["shards"]):
        s, done, _ = shard_state(a.dir, i, sh)
        if s is None:
            continue
        masks += done
        for k in ("A", "B", "loop", "stopped"):
            counts[k] += s[k]
        d = shard_dir(a.dir, i)
        for name, hist in (("steps_hist.csv", steps), ("captures_hist.csv", caps)):
            for line in open(os.path.join(d, name)).read().split()[1:]:
                x, c = line.split(",")
                hist[int(x)] += int(c)
        for line in open(os.path.join(d, "records.txt")):
            idx, deal, outcome, st, cp = line.split()
            if outcome == "loop":
                loops.append(deal)
            else:
                longest.append((int(st), deal, outcome, int(cp)))

    games = sum(counts.values())
    if not games:
        sys.exit("nothing to merge yet")
    out = os.path.join(a.dir, "merged")
    os.makedirs(out, exist_ok=True)
    for name, hist in (("steps_hist.csv", steps), ("captures_hist.csv", caps)):
        with open(os.path.join(out, name), "w") as f:
            f.write(name.split("_")[0] + ",count\n")
            for x in sorted(hist):
                f.write(f"{x},{hist[x]}\n")

    by_cycle = {}
    for deal in loops:
        states, r = cycles.cycle_states(R.parse_deal(deal), plan["rule"])
        by_cycle.setdefault(cycles.cycle_id(states), (r, []))[1].append(deal)
    with open(os.path.join(out, "infinite.txt"), "w") as f:
        for n, (cid, (r, deals)) in enumerate(sorted(by_cycle.items()), 1):
            f.write(f"# cycle {n}: {cid} period {r['period_steps']} steps / {r['period_tricks']} tricks\n")
            for deal in sorted(deals):
                f.write(f"{deal} {n}\n")

    finished = counts["A"] + counts["B"]
    mean = sum(k * v for k, v in steps.items()) / max(1, sum(steps.values()))
    print(f"masks {masks:,} ({100 * masks / NMASKS:.4f}% of the space), games {games:,}")
    print(f"A wins {counts['A'] / games:.6f}  B wins {counts['B'] / games:.6f}  "
          f"infinite {counts['loop']}  stopped early {counts['stopped']}")
    if finished:
        print(f"mean steps {mean:.3f}, max steps {max(steps)}")
    if longest:
        print("longest finite games:")
        for st, deal, outcome, cp in sorted(longest, reverse=True)[:5]:
            print(f"  {deal} {outcome} steps={st} captures={cp}")
    print(f"infinite deals: {len(loops)} in {len(by_cycle)} cycles -> {out}/infinite.txt")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("plan")
    p.add_argument("--dir", required=True)
    p.add_argument("--shards", type=int, default=4096)
    p.add_argument("--sample", type=int, help="this many random blocks of masks instead of the full space")
    p.add_argument("--block", type=int, default=160, help="masks per sample block")
    p.add_argument("--seed", type=int, default=1)
    p.add_argument("--rule", choices=("empty", "must"), default="empty")
    p.add_argument("--stop-lower", action="store_true")
    p = sub.add_parser("run")
    p.add_argument("--dir", required=True)
    p.add_argument("--threads", type=int, default=os.cpu_count())
    p.add_argument("--hours", type=float)
    for name in ("status", "merge"):
        sub.add_parser(name).add_argument("--dir", required=True)
    a = ap.parse_args()
    {"plan": cmd_plan, "run": cmd_run, "status": cmd_status, "merge": cmd_merge}[a.cmd](a)


if __name__ == "__main__":
    main()
