#ifndef __STRESSER_H__
#define __STRESSER_H__ 

typedef void *stresser_handle_t;

stresser_handle_t stresserCreate(unsigned *core_workloads,
				 unsigned thread_count, unsigned run_duration,
				 unsigned slot_duration, unsigned verbose);

#endif // __STRESSER_H__
