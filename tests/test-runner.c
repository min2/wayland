/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#define _GNU_SOURCE

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include "test-runner.h"

static int num_alloc;
static void* (*sys_malloc)(size_t);
static void (*sys_free)(void*);

extern const struct test __start_test_section, __stop_test_section;

__attribute__ ((visibility("default"))) void *
malloc(size_t size)
{
	num_alloc++;
	return sys_malloc(size);
}

__attribute__ ((visibility("default"))) void
free(void* mem)
{
	if (mem != NULL)
		num_alloc--;
	sys_free(mem);
}

static const struct test *
find_test(const char *name)
{
	const struct test *t;

	for (t = &__start_test_section; t < &__stop_test_section; t++)
		if (strcmp(t->name, name) == 0)
			return t;

	return NULL;
}

static void
run_test(const struct test *t)
{
	int cur_alloc = num_alloc;
	int cur_fds;

	cur_fds = count_open_fds();
	t->run();
	assert(cur_alloc == num_alloc && "memory leak detected in test.");
	assert(cur_fds == count_open_fds() && "fd leak detected");
	exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[])
{
	const struct test *t;
	pid_t pid;
	int total, pass;
	siginfo_t info;

	/* Load system malloc and free */
	sys_malloc = dlsym(RTLD_NEXT, "malloc");
	sys_free = dlsym(RTLD_NEXT, "free");

	if (argc == 2) {
		t = find_test(argv[1]);
		if (t == NULL) {
			fprintf(stderr, "unknown test: \"%s\"\n", argv[1]);
			exit(EXIT_FAILURE);
		}

		run_test(t);
	}

	pass = 0;
	for (t = &__start_test_section; t < &__stop_test_section; t++) {
		int success = 0;

		pid = fork();
		assert(pid >= 0);

		if (pid == 0)
			run_test(t); /* never returns */

		if (waitid(P_ALL, 0, &info, WEXITED)) {
			fprintf(stderr, "waitid failed: %m\n");
			abort();
		}

		fprintf(stderr, "test \"%s\":\t", t->name);
		switch (info.si_code) {
		case CLD_EXITED:
			fprintf(stderr, "exit status %d", info.si_status);
			if (info.si_status == EXIT_SUCCESS)
				success = 1;
			break;
		case CLD_KILLED:
		case CLD_DUMPED:
			fprintf(stderr, "signal %d", info.si_status);
			break;
		}

		if (t->must_fail)
			success = !success;

		if (success) {
			pass++;
			fprintf(stderr, ", pass.\n");
		} else
			fprintf(stderr, ", fail.\n");
	}

	total = &__stop_test_section - &__start_test_section;
	fprintf(stderr, "%d tests, %d pass, %d fail\n",
		total, pass, total - pass);

	return pass == total ? EXIT_SUCCESS : EXIT_FAILURE;
}
