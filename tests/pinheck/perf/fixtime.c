/* LD_PRELOAD shim for benchmark runs: time() returns PINHECK_FIXTIME, so the DS1340 starts at the same second in every run */
#include <stdlib.h>
#include <time.h>

time_t time(time_t *t)
{
	const char *v = getenv("PINHECK_FIXTIME");
	time_t r = v ? (time_t)strtoll(v, NULL, 10) : 0;
	if (t) *t = r;
	return r;
}
