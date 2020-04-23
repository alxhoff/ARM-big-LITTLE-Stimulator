#ifndef _GNU_SOOURCE
#define _GNU_SOURCE 1
#endif
#include <stdlib.h>
#include <time.h>
#include <sched.h>
#include <math.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include "config.h"
#include "stresser.h"

#define PRINT_ERROR(msg, ...)                                                  \
	fprintf(stderr, "[ERROR] " msg "\n    @-> %s:%d, %s\n    ERRNO: %s\n", \
		##__VA_ARGS__, __FILE__, __LINE__, __func__, strerror(errno));

struct stresser_core {
	unsigned workload;
	unsigned sqrts_required;
};

struct stresser {
	struct stresser_core cores[SYSTEM_CORE_COUNT];
	unsigned num_threads;
	unsigned duration;
	unsigned slot_duration;
	unsigned verbose;
	unsigned slots_per_second;
	unsigned sqrts_per_second[SYSTEM_CORE_COUNT];
};

struct sqrt_thread_args {
	unsigned *result;
	unsigned core;
};

static int stresserSetCoreAffinity(unsigned core)
{
	int num_cores = sysconf(_SC_NPROCESSORS_ONLN);
	if (core < 0 || core >= num_cores) {
		PRINT_ERROR("Invalid CPU core given");
		return -1;
	}

	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(core, &mask);

	pthread_t current_thread = pthread_self();
	if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &mask) ==
	    -1) {
		PRINT_ERROR("Setting core affinity");
		return -1;
	}

	return 0;
}

static void stresserResetCoreAffinity(void)
{
	cpu_set_t mask;
	CPU_ZERO(&mask);

	pthread_t current_thread = pthread_self();
	if (pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &mask) ==
	    -1)
		PRINT_ERROR("Resetting core affinity");
}

void *stresserGetSqrtsPerSecond(void *args)
{
	struct sqrt_thread_args *st_args = (struct sqrt_thread_args *)args;

	if (stresserSetCoreAffinity(st_args->core))
		return (void *)NULL;

	clock_t test_duration = 0, tmp_clock;

	for (unsigned i = 0; i < DEFAULT_SQRT_TEST_COUNT; i++) {
		tmp_clock = clock();
		for (unsigned j = DEFAULT_SQRT_TEST_ITERATIONS; j > 0; j--)
			sqrt(rand());
		test_duration += clock() - tmp_clock;
	}

	double average_sqrt_duration =
		(double)(test_duration) / DEFAULT_SQRT_TEST_ITERATIONS;

	*(st_args->result) = (unsigned)(CLOCKS_PER_SEC / average_sqrt_duration);

	return (void *)NULL;
}

stresser_handle_t stresserCreate(unsigned *core_workloads,
				 unsigned thread_count, unsigned run_duration,
				 unsigned slot_duration, unsigned verbose)
{
	pthread_t threads[SYSTEM_CORE_COUNT];
	struct stresser *stresser =
		(struct stresser *)malloc(sizeof(struct stresser));

	if (!stresser)
		return (stresser_handle_t)NULL;

	for (int i = 0; i < SYSTEM_CORE_COUNT; i++)
		stresser->cores[i].workload = core_workloads[i];

	stresser->duration = run_duration;
	stresser->slot_duration = slot_duration;
	stresser->num_threads = thread_count;
	stresser->verbose = verbose;

	stresser->slots_per_second = run_duration / slot_duration;

	for (int i = 0; i < SYSTEM_CORE_COUNT; i++) {
		struct sqrt_thread_args thread_args = {
			.result = &stresser->cores[i].sqrts_required,
			.core = i,
		};

		if (pthread_create(&threads[i], NULL, stresserGetSqrtsPerSecond,
				   (void *)&thread_args))
			PRINT_ERROR("Creating sqrt calib thread #%d", i);
	}

	// Wait for all cores to finish
	for (int i = 0; i < SYSTEM_CORE_COUNT; i++)
		pthread_join(threads[i], NULL);

	printf("Calibrating cores done, sqrts/sec:");

	for (int i = 0; i < SYSTEM_CORE_COUNT; i++)
		printf("Core #%d: %u\n", i, stresser->cores[i].sqrts_required);

	return (stresser_handle_t)stresser;
}
