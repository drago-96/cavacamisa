#!/usr/bin/env python3
"""Reference Cavacamisa simulator: slow, step-by-step, meant to be obviously correct.

Cards: 0 = plain card, 1/2/3 = asso/due/tre (the opponent must pay 1/2/3 cards).
Deck: 28 zeros + 4 each of 1, 2, 3.  A deal is 40 values: deal[:20] is A's hand,
deal[20:] is B's hand, top card first.  A plays first.

The winner of a trick puts the pile under their hand and leads the next trick.

When does a player lose?  Two variants:
  'empty'  a player loses as soon as their hand becomes empty (behaviour of the
           legacy C++ code)
  'must'   a player loses only when they have to play and have no cards left
           (so playing your last card as a 1/2/3 can still win the pile)

Infinite games are detected exactly: at every trick boundary (empty pile, winner
to lead) the state is (leader's hand, follower's hand); a repeated state means a
cycle.  States are unlabeled (we don't care whether A or B is leading), so the
reported period can be half of the "labeled" period.
"""
from collections import deque
import random
import sys

FULL_DECK = [0] * 28 + [1] * 4 + [2] * 4 + [3] * 4
RULES = ("empty", "must")


def parse_deal(s):
    """Accepts '0030...' (40 digits) or 'A digits | B digits' or space separated."""
    digits = [int(ch) for ch in s if ch in "0123"]
    if len(digits) != 40 or sorted(digits) != sorted(FULL_DECK):
        raise ValueError(f"not a valid 40 card deal: {s!r}")
    return digits


def deal_str(deal):
    return "".join(map(str, deal))


def random_deal(rng=random):
    d = FULL_DECK[:]
    rng.shuffle(d)
    return d


def play(deal, rule="empty", trace=False, max_steps=None):
    """Plays a whole game.

    Returns a dict with:
      outcome   'A', 'B' (winner) or 'loop' (or 'limit' if max_steps hit)
      steps     cards played
      captures  tricks won (pile collections)
    and for loops also:
      mu_steps, mu_tricks          when the cycle is entered
      period_steps, period_tricks  length of the (unlabeled) cycle
      swapped                      True if the repeated state has the other player leading
      boundary                     list of trick-boundary states (leader, follower, leader_label)
    """
    assert rule in RULES
    hands = [deque(deal[:20]), deque(deal[20:])]
    pile = []
    cur = 0
    penalty = 0
    steps = 0
    captures = 0
    seen = {}
    boundary = []

    while True:
        if penalty == 0 and not pile:
            key = (tuple(hands[cur]), tuple(hands[1 - cur]))
            if key in seen:
                s0, c0, who0 = seen[key]
                return dict(outcome="loop", steps=steps, captures=captures,
                            mu_steps=s0, mu_tricks=c0,
                            period_steps=steps - s0, period_tricks=captures - c0,
                            swapped=(who0 != cur), boundary=boundary)
            seen[key] = (steps, captures, cur)
            boundary.append((key[0], key[1], cur))

        if max_steps is not None and steps >= max_steps:
            return dict(outcome="limit", steps=steps, captures=captures)

        hand = hands[cur]
        if not hand:  # only reachable with rule 'must'
            return dict(outcome="AB"[1 - cur], steps=steps, captures=captures)

        card = hand.popleft()
        pile.append(card)
        steps += 1
        if trace:
            print(f"{steps:6d} {'AB'[cur]} plays {card}  pile={deal_str(pile)}")

        if penalty and card == 0:
            penalty -= 1
            if penalty == 0:
                cur = 1 - cur
                hands[cur].extend(pile)
                pile.clear()
                captures += 1
                if trace:
                    print(f"       {'AB'[cur]} captures -> A={deal_str(hands[0])} B={deal_str(hands[1])}")
        else:
            penalty = card
            cur = 1 - cur

        if rule == "empty":
            if not hands[0]:
                return dict(outcome="B", steps=steps, captures=captures)
            if not hands[1]:
                return dict(outcome="A", steps=steps, captures=captures)


def play_trick(leader, follower, rule):
    """Plays one trick from a boundary state (leader leads, pile empty).

    Returns (winner_is_leader, new_leader_hand, new_follower_hand) or None if
    the game ends during the trick.
    """
    hands = [deque(leader), deque(follower)]
    pile = []
    cur = 0
    penalty = 0
    while True:
        hand = hands[cur]
        if not hand:
            return None
        card = hand.popleft()
        pile.append(card)
        if penalty and card == 0:
            penalty -= 1
            if penalty == 0:
                cur = 1 - cur
                hands[cur].extend(pile)
                if rule == "empty" and not hands[1 - cur]:
                    return None
                return (cur == 0, tuple(hands[cur]), tuple(hands[1 - cur]))
        else:
            penalty = card
            cur = 1 - cur
        if rule == "empty" and (not hands[0] or not hands[1]):
            return None


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("deal", nargs="?", help="40 digit deal (A first). Omit to read deals from stdin.")
    ap.add_argument("--rule", choices=RULES, default="empty")
    ap.add_argument("--trace", action="store_true", help="print every card played")
    ap.add_argument("--random", type=int, metavar="N", help="play N random deals and print them")
    ap.add_argument("--seed", type=int, default=1)
    args = ap.parse_args(argv)

    def show(deal):
        r = play(deal, args.rule, trace=args.trace)
        extra = ""
        if r["outcome"] == "loop":
            extra = (f" mu_steps={r['mu_steps']} mu_tricks={r['mu_tricks']}"
                     f" period_steps={r['period_steps']} period_tricks={r['period_tricks']}"
                     f" swapped={int(r['swapped'])}")
        print(f"{deal_str(deal)} {r['outcome']} steps={r['steps']} captures={r['captures']}{extra}")

    if args.random:
        rng = random.Random(args.seed)
        for _ in range(args.random):
            show(random_deal(rng))
    elif args.deal:
        show(parse_deal(args.deal))
    else:
        for line in sys.stdin:
            if line.strip():
                show(parse_deal(line.split()[0]))


if __name__ == "__main__":
    main(sys.argv[1:])
