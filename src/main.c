
#include <argp.h>
#include <stdlib.h>
#include <unistd.h>

#include "stresser.h"
#include "config.h"

const char *argp_program_version = "0.0";
const char *argp_program_bug_email = "alxhoff@gmail.com";
static char doc[] =
	"Workload generator to stress the big and LITTLE cores of an ARM bit.LITLE MPSoC as well as the GPU";
static char req_args[] =
	"Specify for each core (-c) a desired load (-l) and a duration (-d) for the test";

const struct argp_option argp_options[] = {
	{ 0, 0, 0, 0,
	  "To set a core's load, first specify the core (-c) and then the load (-l)\n Duration must be specified once" },
	{ "core", 'c', "uint", 0,
	  "Core to be given the load specified by a following '-l' option" },
	{ "load", 'l', "uint", 0,
	  "Target workload in % (0-100) to be generated on the aforementioned core" },
	{ "duration", 'd', "uint", 0,
	  "Duration for which load should be generated" },
	{ 0, 0, 0, 0, "Optional arguments" },
	{ "verbose", 'v', 0, OPTION_ARG_OPTIONAL, "Show verbose output" },
	{ "threads", 't', "uint", OPTION_ARG_OPTIONAL,
	  "The number of threads to be used when generating workload" },
	{ "slot", 's', "uint", OPTION_ARG_OPTIONAL,
	  "Duration of slots used to generated load" },
	{ 0 }
};

struct arguments {
	char *arg1;
	char **strings;
	unsigned verbose;
	unsigned threads;
	unsigned *core_loads;
	unsigned duration;
	unsigned slot;
};

static struct arguments prog_arguments = {
	.duration = DEFAULT_DURATION,
	.slot = DEFAULT_SLOT_DURATION,
	.threads = DEFAULT_THREAD_COUNT,
};

unsigned system_cpu_count = 0;

static error_t parse_opt(int key, char *arg, struct argp_state *state)
{
	static unsigned cur_core = 0;

	switch (key) {
	case 'v':
		prog_arguments.verbose = 1;
		break;
	case 't':
		if (state->arg_num != 1)
			argp_usage(state);
		prog_arguments.threads = (unsigned)strtol(arg, NULL, 10);
	case 'c': {
		unsigned core = (unsigned)atoi(arg);
		if (core < system_cpu_count)
			cur_core = core;
	} break;
	case 'l': {
		unsigned load = (unsigned)strtol(arg, NULL, 10);
		if (load <= 100)
			prog_arguments.core_loads[cur_core] = load;
	} break;
	case 'd':
		prog_arguments.duration = (unsigned)strtol(arg, NULL, 10);
		break;
	case 's':
		if (state->arg_num != 1)
			argp_usage(state);
		prog_arguments.duration = (unsigned)strtol(arg, NULL, 10);
		break;
	default:
		return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

static struct argp argp = { argp_options, parse_opt, req_args, doc };

int main(int argc, char **argv)
{
#ifdef SYSTEM_CORE_COUNT
	system_cpu_count = SYSTEM_CORE_COUNT;
#else
	system_cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
#endif //system_cpu_count

	prog_arguments.core_loads = calloc(system_cpu_count, sizeof(unsigned));
	if (prog_arguments.core_loads == NULL)
		return -1;

	argp_parse(&argp, argc, argv, 0, 0, &prog_arguments);

	for (int i = 0; i < system_cpu_count; i++) {
		printf("Generating %u load on core %u\n",
		       prog_arguments.core_loads[i], i);
	};

	printf("Running loads for %u milliseconds, in %u millisecond slots, using %u threads\n",
	       prog_arguments.duration, prog_arguments.slot,
	       prog_arguments.threads);

	stresser_handle_t stresser =
		stresserCreate(prog_arguments.core_loads,
			       prog_arguments.threads, prog_arguments.duration,
			       prog_arguments.slot, prog_arguments.verbose);

    stresserRun(stresser);

	return 0;
}
