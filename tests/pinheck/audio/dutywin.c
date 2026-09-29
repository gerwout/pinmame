#include "audio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dutywin RTLTRACE OURTRACE CTRLOG WINDOW: per-window high cycles on P15/P14, RTL vs audio.c */
typedef struct ev { unsigned long long t; int kind; unsigned a, b, c, d; } ev;

static ev *load(const char *path, char tag, int *n)
{
	FILE *f = fopen(path, "r");
	char line[256];
	ev *v = NULL;
	int cap = 0;
	*n = 0;
	if (!f) { perror(path); exit(2); }
	while (fgets(line, sizeof(line), f)) {
		ev e;
		memset(&e, 0, sizeof(e));
		if (line[0] != tag) continue;
		if (tag == 'P' && sscanf(line + 2, "%llu %x %x", &e.t, &e.a, &e.b) != 3) continue;
		if (tag == 'C' && sscanf(line + 2, "%llu %u %u %x %x", &e.t, &e.a, &e.b, &e.c, &e.d) != 5) continue;
		if (tag == 'E' && sscanf(line + 2, "%llu", &e.t) != 1) continue;
		e.kind = tag;
		if (*n == cap) { cap = cap ? 2 * cap : 1024; v = realloc(v, cap * sizeof(ev)); }
		v[(*n)++] = e;
	}
	fclose(f);
	return v;
}

int main(int argc, char **argv)
{
	int nr, no, nc, ne, i, j, k, ch, bad = 0, worst = 0;
	ev *rtl, *our, *ctr, *end;
	unsigned long long w, tend, t;
	audio a;
	if (argc != 5) { fprintf(stderr, "usage: dutywin rtl our ctrlog window\n"); return 2; }
	rtl = load(argv[1], 'P', &nr);
	our = load(argv[2], 'P', &no);
	ctr = load(argv[3], 'C', &nc);
	end = load(argv[1], 'E', &ne);
	w = strtoull(argv[4], NULL, 0);
	tend = ne ? end[0].t : rtl[nr - 1].t;
	audio_init(&a, 48000.0, NULL, NULL);
	for (i = j = 0; i < no || j < nc;) {
		if (j >= nc || (i < no && our[i].t <= ctr[j].t)) { audio_pins(&a, our[i].t, our[i].a, our[i].b); i++; }
		else { audio_ctr(&a, ctr[j].t, (int)ctr[j].a, (int)ctr[j].b, ctr[j].c, ctr[j].d); j++; }
	}
	for (ch = 0; ch < 2; ch++) {
		int pin = ch ? AUDIO_PIN_R : AUDIO_PIN_L;
		unsigned long long w0;
		k = 0;
		for (w0 = 0; w0 + w <= tend; w0 += w) {
			long long high = 0, diff;
			unsigned long long lvl_t = w0, cur;
			int level = 0;
			for (k = 0; k < nr && rtl[k].t <= w0; k++) level = (rtl[k].a >> pin) & (rtl[k].b >> pin) & 1;
			cur = w0;
			for (; k < nr && rtl[k].t < w0 + w; k++) {
				if (level) high += (long long)(rtl[k].t - cur);
				cur = rtl[k].t;
				level = (rtl[k].a >> pin) & (rtl[k].b >> pin) & 1;
			}
			if (level) high += (long long)(w0 + w - cur);
			(void)lvl_t;
			diff = high - (long long)(audio_level(&a, ch, w0, w0 + w) * (double)w + 0.5);
			if (diff < 0) diff = -diff;
			if (diff > worst) worst = (int)diff;
			if (diff > 2) { if (bad < 8) printf("P%d window %llu: rtl %lld audio %.1f\n", pin, w0, high, (double)high - (double)diff); bad++; }
		}
	}
	t = tend;
	printf("dutywin: %d windows off by more than 2 cycles, worst %d (window %llu, %llu cycles)\n", bad, worst, w, t);
	return bad != 0;
}
