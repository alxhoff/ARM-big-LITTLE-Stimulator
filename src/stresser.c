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
	struct stresser_core *cores;
	/** struct stresser_core *cores[system_cpu_count]; */
	unsigned num_threads;
	unsigned duration;
	unsigned slot_duration;
	unsigned verbose;
	unsigned slots_per_second;
	unsigned *sqrts_per_second;
	/** unsigned *sqrts_per_second[system_cpu_count]; */
};

struct sqrt_thread_args {
	unsigned *result;
	unsigned core;
};

static pthread_mutex_t stresser_lock = PTHREAD_MUTEX_INITIALIZER;

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

static void *stresserGetSqrtsPerSecond(void *args)
{
	struct sqrt_thread_args *st_args = (struct sqrt_thread_args *)args;

	if (stresserSetCoreAffinity(st_args->core))
		goto err_return;

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

err_return:
	return (void *)NULL;
}

stresser_handle_t stresserCreate(unsigned *core_workloads,
				 unsigned thread_count, unsigned run_duration,
				 unsigned slot_duration, unsigned verbose)
{
	pthread_t threads[system_cpu_count];
	struct stresser *stresser =
		(struct stresser *)malloc(sizeof(struct stresser));

	if (!stresser)
		goto err_stresser;

	stresser->cores =
		calloc(system_cpu_count, sizeof(struct stresser_core));
	if (stresser->cores == NULL)
		goto err_cores;

	stresser->sqrts_per_second = calloc(system_cpu_count, sizeof(unsigned));
	if (stresser->sqrts_per_second == NULL)
		goto err_sqrts;

	for (int i = 0; i < system_cpu_count; i++)
		stresser->cores[i].workload = core_workloads[i];

	stresser->duration = run_duration;
	stresser->slot_duration = slot_duration;
	stresser->num_threads = thread_count;
	stresser->verbose = verbose;

	stresser->slots_per_second = run_duration / slot_duration;

	struct sqrt_thread_args *thread_args =
		calloc(system_cpu_count, sizeof(struct sqrt_thread_args));
	if (thread_args == NULL)
		goto err_thread_args;

	pthread_mutex_lock(&stresser_lock);

	for (int i = 0; i < system_cpu_count; i++) {
		thread_args[i].result = &stresser->cores[i].sqrts_required;
		thread_args[i].core = i;

		if (pthread_create(&threads[i], NULL, stresserGetSqrtsPerSecond,
				   (void *)&thread_args[i]))
			PRINT_ERROR("Creating sqrt calib thread #%d", i);
	}

	// Wait for all cores to finish
	for (int i = 0; i < system_cpu_count; i++)
		pthread_join(threads[i], NULL);

	pthread_mutex_unlock(&stresser_lock);

	free(thread_args);

	printf("Calibrating cores done, sqrts/sec:\n");

	for (int i = 0; i < system_cpu_count; i++)
		printf("Core #%d: %u\n", i, stresser->cores[i].sqrts_required);

	return (stresser_handle_t)stresser;

err_thread_args:
	free(stresser->sqrts_per_second);
err_sqrts:
	free(stresser->cores);
err_cores:
	free(stresser);
err_stresser:
	return (stresser_handle_t)NULL;
}

struct run_thread_args {
	struct stresser *stresser;
	unsigned *workload;
	unsigned *slots_per_second;
	unsigned core;
	unsigned work_sqrts;
	unsigned sleep_milliseconds;
};

static void *stresserStressCore(void *args)
{
	struct run_thread_args *st_args = (struct run_thread_args *)args;

	if (stresserSetCoreAffinity(st_args->core))
		goto err_return;

	clock_t start_time = clock();

	for (int i = 0; i < (*st_args->slots_per_second *
			     (st_args->stresser->duration / 1000.0));
	     i++) {
		for (int j = 0; j < st_args->work_sqrts; j++)
			sqrt(rand());
		usleep(st_args->sleep_milliseconds * 1000);
	}

	printf("Test on core #%d ran for %ld milliseconds\n", st_args->core,
	       clock() - start_time);

err_return:
	return (void *)NULL;
}

int stresserRun(stresser_handle_t stresser)
{
	pthread_t threads[system_cpu_count];

	if (stresser == NULL)
		return -1;

	struct stresser *st = (struct stresser *)stresser;

	struct run_thread_args *thread_args =
		calloc(system_cpu_count, sizeof(struct run_thread_args));
	if (thread_args == NULL)
		return -1;

	for (int i = 0; i < system_cpu_count; i++) {
		thread_args[i].stresser = st;
		thread_args[i].workload = &st->cores[i].workload;
		thread_args[i].slots_per_second = &st->slots_per_second;
		thread_args[i].core = i;
		thread_args[i].work_sqrts = *thread_args[i].workload / 100.0 *
					    st->cores[i].sqrts_required /
					    st->slots_per_second;
		thread_args[i].sleep_milliseconds =
			(100 - *thread_args[i].workload) * 10 /
			st->slots_per_second;

		printf("Core %u: \n"
		       "    Creating workload of %u using %u slots\n"
		       "    Calculating %u sqrts per slots, sleeping %u milliseconds "
		       "between slots\n"
		       "    Max sqrts/sec for core is %u\n",
		       i, *thread_args[i].workload,
		       *thread_args[i].slots_per_second,
		       thread_args[i].work_sqrts,
		       thread_args[i].sleep_milliseconds,
		       st->cores[i].sqrts_required);
	}

	pthread_mutex_lock(&stresser_lock);

	for (int i = 0; i < system_cpu_count; i++)
		if (pthread_create(&threads[i], NULL, stresserStressCore,
				   (void *)&thread_args[i]))
			PRINT_ERROR("Creating stresser thread #%d failed", i);

	for (int i = 0; i < system_cpu_count; i++)
		pthread_join(threads[i], NULL);

	pthread_mutex_unlock(&stresser_lock);

	free(thread_args);

	return 0;
}
