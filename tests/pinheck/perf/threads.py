#!/usr/bin/env python3
"""bench.sh worker: cycles and instructions per emulated second of the PIC32 thread (the one running pic32cpu_execute)
and of every other thread together (the Propeller worker, started again by the governor at times), from
`perf report --stdio -n --sort pid,sym` of a per-thread recording with a fixed period. Arguments: workload, emulated
seconds, period."""
import collections
import re
import sys

w, emu, period = sys.argv[1], float(sys.argv[2]), int(sys.argv[3])
ev = None
count = collections.defaultdict(collections.Counter)
pic = collections.Counter()
for line in sys.stdin:
    m = re.match(r"# Samples: .* of event '([^':]+)", line)
    if m:
        ev = m.group(1)
        continue
    m = re.match(r"\s+[\d.]+%\s+(\d+)\s+(\d+):\S+\s+\[.\]\s+(.*)$", line)
    if m and ev:
        n, pid, sym = int(m.group(1)), m.group(2), m.group(3).strip()
        count[ev][pid] += n
        if ev == 'cycles' and sym == 'pic32cpu_execute':
            pic[pid] += n
if not pic:
    sys.exit('threads: no PIC32 thread in the recording')
main = pic.most_common(1)[0][0]
per = lambda ev, other: sum(n for p, n in count[ev].items() if (p != main) == other) * period / 1e9 / emu
print('bench worker %s: the Propeller worker %.3f G cycles and %.2f G instructions per emulated s, the PIC32 thread '
      '%.3f G cycles (whole run, %.1f s emulated)' % (w, per('cycles', True), per('instructions', True), per('cycles', False), emu))
