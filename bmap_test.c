/*
 * Copyright (c) 2015 Artur Grabowski <art@blahonga.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <fcntl.h>
#include <err.h>
#include <limits.h>
#include <assert.h>

#include <stopwatch.h>

#include "bmap.h"

struct {
	struct bmap_interface *bi;
	const char *n;
} tests[] = {
	{ &bmap_dumb, "dumb" },
};

static void
smoke_test(struct bmap_interface *bi, const char *name)
{
	void *b = bi->alloc(100);
	bi->set(b, 1);
	bi->set(b, 9);
	bi->set(b, 62);
	bi->set(b, 88);
	unsigned int r;
	if ((r = bi->first_set(b, 0)) != 1)
		errx(1, "first_set(0) != 1 (%d)", r);
	if ((r = bi->first_set(b, 2)) != 9)
		errx(1, "first_set(2) != 9 (%d)", r);
	if ((r = bi->first_set(b, 10)) != 62)
		errx(1, "first_set(2) != 88 (%d)", r);
	if ((r = bi->first_set(b, 63)) != 88)
		errx(1, "first_set(2) != 88 (%d)", r);
	bi->free(b);
	printf("smoke test of %s worked\n", name);
}

struct test_set {
	unsigned int nelems;		/* number of elements in this set. */
	unsigned int bmapsz;		/* size of bmap we want to test with. */
	const char *set_name;
	unsigned int *arr;		/* pregenerated array of elements we expect to find in array. */
} test_sets[] = {
	{ 	10,		1000,		"small-sparse" },
	{ 	100,		1000000,	"mid-sparse" },
	{ 	500000,		1000000,	"mid-dense" },
};

#define howmany(a) (sizeof(a) / sizeof(a[0]))

static int
uintcmp(const void *av, const void *bv)
{
	const unsigned int *a = av, *b = bv;

	if (*a < *b)
		return -1;
	else if (*b < *a)
		return 1;
	return 0;
}

static void
generate_set(struct test_set *ts)
{
	struct bmap_interface *bi = &bmap_dumb;		/* good enough for our needs. */
	void *b = bi->alloc(ts->bmapsz);
	int i;

	ts->arr = malloc(sizeof(*ts->arr) * ts->nelems);

	for (i = 0; i < ts->nelems; i++) {
		unsigned int x;

		do {
			x = random() % ts->bmapsz;
		} while (bi->isset(b, x));
		bi->set(b, x);
		ts->arr[i] = x;	
	}
	bi->free(b);

	qsort(ts->arr, ts->nelems, sizeof(*ts->arr), uintcmp);
}

static void
populate(struct bmap_interface *bi, struct test_set *ts, void *v)
{
	int i;

	for (i = 0; i < ts->nelems; i++)
		bi->set(v, ts->arr[i]);
}

static void
check(struct bmap_interface *bi, struct test_set *ts, void *v)
{
	unsigned int last = 0;
	int i;

	for (i = 0; i < ts->nelems; i++) {
		unsigned int n = bi->first_set(v, last);
		if (n != ts->arr[i])
			printf("bad elem %u != %u\n", n, ts->arr[i]);
		last = n + 1;
	}
}

static void
run_and_measure(void (*fn)(struct bmap_interface *bi, struct test_set *ts, void *v), struct bmap_interface *bi, struct test_set *ts, void *bmap, const char *statdir, const char *name)
{
	struct stopwatch sw;
	FILE *statfile;
	int rep, toprep;
	unsigned int nrep = 1000000000 / ts->bmapsz;

	if (statdir) {
		char fname[PATH_MAX];
		snprintf(fname, sizeof(fname), "%s/%s", statdir, name);
		if ((statfile = fopen(fname, "w+")) == NULL)
			err(1, "fopen(%s)", fname);
	}

	for (toprep = 0; toprep < (statdir ? 100 : 1); toprep++) {
		stopwatch_reset(&sw);
		stopwatch_start(&sw);
		for (rep = 0; rep < nrep; rep++) {
			(*fn)(bi, ts, bmap);
		}
		stopwatch_stop(&sw);
		printf("%s: %f\n", name, stopwatch_to_ns(&sw) / 1000000000.0);
		if (statdir)
			fprintf(statfile, "%f\n", stopwatch_to_ns(&sw) / 1000000000.0);
	}

	if (statdir)
		fclose(statfile);
}

static void
test_one(struct bmap_interface *bi, const char *test_name, struct test_set *ts, const char *statdir)
{
	char name[PATH_MAX];
	void *bmap;

	bmap = bi->alloc(ts->bmapsz);
	
	snprintf(name, sizeof(name), "%s-%s-populate", test_name, ts->set_name);
	run_and_measure(populate, bi, ts, bmap, statdir, name);

	snprintf(name, sizeof(name), "%s-%s-check", test_name, ts->set_name);
	run_and_measure(check, bi, ts, bmap, statdir, name);

	bi->free(bmap);
}

int
main(int argc, char **argv)
{
	const char *statdir = NULL;
	int t;

	srandom(4711);

	for (t = 0; t < howmany(test_sets); t++) {
		generate_set(&test_sets[t]);
	}

	/* If called with an argument we'll try to generate a set of stats data we can use with ministat. */
	if (argc > 1) {
		statdir = argv[1];
	}

	for (t = 0; t < howmany(tests); t++) {
		int s;
		smoke_test(tests[t].bi, tests[t].n);

		for (s = 0; s < howmany(test_sets); s++)
			test_one(tests[t].bi, tests[t].n, &test_sets[s], statdir);
	}

	return 0;
}