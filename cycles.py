#!/usr/bin/env python3
"""Cycles and basins of infinite Cavacamisa games.

At every trick boundary the game state is just (leader's hand, follower's hand)
with an empty pile.  "Play one trick" is a deterministic map T on these states,
and an initial deal is simply a boundary state where the leader has 20 cards.

  * an infinite deal is one whose T-orbit ends in a cycle;
  * T can be inverted: given a state, every possible last pile (k cards at the
    bottom of the leader's hand) and every choice of who led the trick gives at
    most one candidate predecessor, which we confirm by playing it forward.

So from one deal on a cycle we can walk backwards and list *every* boundary
state that ends in that cycle, and in particular every deal (leader has 20
cards) that does.  This is exact and takes seconds, because basins are tiny.

  ./cycles.py DEAL [DEAL...]        cycle + full basin of each deal's cycle
  ./cycles.py --file known.txt      same, deals read from a file
"""
import argparse
import sys
import time

import reference as R


def cycle_states(deal, rule):
    """Returns the list of boundary states on the cycle reached by `deal`."""
    r = R.play(deal, rule)
    if r["outcome"] != "loop":
        return None, r
    states = [(L, F) for (L, F, _) in r["boundary"][r["mu_tricks"]:]]
    return states, r


def cycle_id(states):
    """Canonical name of a cycle: its lexicographically smallest state."""
    L, F = min(states)
    return R.deal_str(L) + "|" + R.deal_str(F)


def predecessors(H, G, rule):
    """All boundary states (L, F) with T(L, F) == (H, G)."""
    out = []
    n = len(H)
    for k in range(2, n + 1):
        pile, rest = H[n - k:], H[:n - k]
        for winner_led in (True, False):
            # Replay the pile to find out who played each card. 0 = winner, 1 = loser.
            cur = 0 if winner_led else 1
            penalty = 0
            played = ([], [])
            ok = False
            for t, c in enumerate(pile):
                played[cur].append(c)
                if penalty and c == 0:
                    penalty -= 1
                    if penalty == 0:
                        cur = 1 - cur
                        ok = (t == k - 1 and cur == 0)
                        break
                else:
                    penalty = c
                    cur = 1 - cur
            if not ok:
                continue
            W = tuple(played[0]) + rest
            Lo = tuple(played[1]) + G
            L, F = (W, Lo) if winner_led else (Lo, W)
            fwd = R.play_trick(L, F, rule)
            if fwd is not None and (fwd[1], fwd[2]) == (H, G):
                out.append((L, F))
    return out


def basin(states, rule, limit=10_000_000):
    """Every boundary state whose orbit reaches the cycle `states`."""
    seen = set(states)
    stack = list(states)
    while stack:
        s = stack.pop()
        for p in predecessors(s[0], s[1], rule):
            if p not in seen:
                seen.add(p)
                stack.append(p)
                if len(seen) > limit:
                    raise RuntimeError("basin larger than limit")
    return seen


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("deals", nargs="*")
    ap.add_argument("--file")
    ap.add_argument("--rule", choices=R.RULES, default="empty")
    ap.add_argument("--out", help="write all infinite deals found (one per line, with cycle id)")
    args = ap.parse_args(argv)

    deals = [R.parse_deal(d) for d in args.deals]
    if args.file:
        deals += [R.parse_deal(l.split()[0]) for l in open(args.file) if l.strip()]

    cycles = {}  # cycle id -> (states, basin)
    for d in deals:
        states, r = cycle_states(d, args.rule)
        if states is None:
            print(f"{R.deal_str(d)} is not infinite ({r['outcome']})")
            continue
        cid = cycle_id(states)
        if cid not in cycles:
            t = time.time()
            b = basin(states, args.rule)
            cycles[cid] = (states, b, r)
            print(f"cycle {len(cycles)}: {cid}\n"
                  f"  period {r['period_steps']} steps / {r['period_tricks']} tricks"
                  f" ({'A and B swap roles each lap' if r['swapped'] else 'same leader each lap'});"
                  f" basin: {len(b)} boundary states, "
                  f"{sum(len(L) == 20 for L, F in b)} deals, "
                  f"{sum(len(L) == 20 for L, F in states)} deals on the cycle itself"
                  f"  [{time.time() - t:.1f}s]")

    all_deals = []
    for i, (cid, (states, b, r)) in enumerate(cycles.items(), 1):
        for L, F in b:
            if len(L) == 20:
                all_deals.append((R.deal_str(L + F), i))
    all_deals.sort()
    print(f"total infinite deals in these {len(cycles)} cycles: {len(all_deals)}")

    # Paranoia: every deal must really be infinite and end in the right cycle.
    ids = list(cycles)
    for s, i in all_deals:
        states, r = cycle_states(R.parse_deal(s), args.rule)
        assert states is not None and cycle_id(states) == ids[i - 1], s
    print("verified: all of them loop forward into the expected cycle")

    if args.out:
        with open(args.out, "w") as f:
            for i, cid in enumerate(ids, 1):
                states, b, r = cycles[cid]
                f.write(f"# cycle {i}: {cid} period_steps={r['period_steps']} period_tricks={r['period_tricks']}\n")
            for s, i in all_deals:
                f.write(f"{s} {i}\n")


if __name__ == "__main__":
    main(sys.argv[1:])
