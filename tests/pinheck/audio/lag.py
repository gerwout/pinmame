#!/usr/bin/env python3
import sys

CLOCK = 80000000


def main():
    if len(sys.argv) != 3:
        sys.exit('usage: lag.py lag.log sample_rate')
    rate = int(sys.argv[2])
    lags = [int(l.split()[1]) for l in open(sys.argv[1]) if l.strip()]
    if not lags:
        sys.exit('lag: no stream updates logged')
    bound = 2 * CLOCK // rate
    worst = max(abs(x) for x in lags)
    n = len(lags) // 4 or 1
    print('lag: %d updates, worst %d cycles (%.2f samples), first quarter max %d, last quarter max %d, bound %d' % (
        len(lags), worst, worst * rate / CLOCK, max(abs(x) for x in lags[:n]), max(abs(x) for x in lags[-n:]), bound))
    sys.exit(0 if worst <= bound else 1)


if __name__ == '__main__':
    main()
