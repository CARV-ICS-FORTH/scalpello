
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include "papi.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
//#include <pthread.h>
#include <omp.h>

#include "papi_tracer.h"

__thread pmu_tracing_global_state *threadcopy_global_status; 
__thread pmu_tracepoint *tracepoints[MAX_TRACE_POINTS];      
__thread int tl_event_set;                                   
__thread struct timespec tl_start_array[MAX_TRACE_POINTS], tl_end_array[MAX_TRACE_POINTS];
__thread uint32_t tl_iteration_array[MAX_TRACE_POINTS];

pmu_event_set eventset;

unsigned long pmu_get_thread_id();
int internal_pmu_init_global(pmu_event_set *event_set, uint32_t max_samples);
int internal_pmu_tracepoint_start(int tracepoint_id);
int internal_pmu_tracepoint_stop(int tracepoint_id);


unsigned long pmu_get_thread_id() //signature as expected by PAPI_thread_init
{
    return omp_get_thread_num();
    //return pthread_self();
}


//we assume this code runs once per omp/MPI thread
int internal_pmu_init_global(pmu_event_set *event_set, uint32_t max_samples)
{
    uint64_t ticket = 0;
    if((pmu_tracing_global_state *)0 == threadcopy_global_status)
    {
        int global_area_fd;;
	//thread_id = omp_get_thread_num();
        //printf("DEBU: ENTRY THIS CODE SHOULD BE EXECUTED EXACTLY ONCE PER THREAD\n"); //delete this later
        //if (PAPI_thread_init(pthread_self()) != PAPI_OK)
        if (PAPI_thread_init(pmu_get_thread_id) != PAPI_OK) // move this at the end of the function
	{
	    printf("failed the per thread initialization of PAPI, aborting (sorry)\n");
	    exit(-1);
	}
	global_area_fd = shm_open(GLOBAL_STATE_MMAP_KEY, O_CREAT | O_RDWR, 0600);
        if(-1 == global_area_fd)
        {
            printf("pmu tracer failed to open shared area, terminating thread (sorry)\n");
	    exit(-1);
        }
	if(-1 == ftruncate(global_area_fd, sizeof(struct pmu_tracing_global_state)))
	{
            printf("pmu tracer failed to truncate shared area, terminating thread (sorry)\n");
	    exit(-1);

	}
        threadcopy_global_status = (pmu_tracing_global_state *)mmap(NULL, 
			                 sizeof(struct pmu_tracing_global_state),
                                         PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED,
                                         global_area_fd, 0);
	if((void *) -1 == threadcopy_global_status)
	{
            printf("pmu tracer failed to mmap shared area, terminating thread (sorry)\n");
	    exit(-1);
	}
        //printf("DEBUG: EXIT THIS CODE SHOULD BE EXECUTED EXACTLY ONCE PER THREAD\n"); //delete this later
    }
    else
    {
        printf("multiple global initializations of PAPI tracer, aborting!\n");
	exit(-1);
    }
    //now we know we have mapped things properly, if we get the ticket we initialize, otherwise exit
    ticket = __atomic_add_fetch(&threadcopy_global_status->access_sem, 1, __ATOMIC_SEQ_CST);
    if(1 != ticket)
    {
	do
	{
	}while(!__atomic_load_n(&threadcopy_global_status->initialized, __ATOMIC_SEQ_CST));
        for(int i = 0; i<MAX_TRACE_POINTS; ++i)
	{
            char tracepoint_fname[MAX_FNAME];
	    int tracepoint_fd = 0;

	    sprintf(tracepoint_fname, "%s%s_%d", TOP_DIR, "tracepoint", i);
	    tracepoint_fd = open(tracepoint_fname, O_RDWR, S_IRUSR|S_IWUSR);
	    if(tracepoint_fd <= 0)
	    {
		    printf("failed to open tracefile: %s  terminating, (sorry)\n", tracepoint_fname);
		    exit(-1);
	    }
	    
	    tracepoints[i] = (pmu_tracepoint *) mmap(NULL, 
			                 sizeof(pmu_tracepoint) + sizeof(pmu_sample) * max_samples,  
                                         PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED,                    
                                         tracepoint_fd, 0);
	    if((void *)-1 == tracepoints[i])
	    {
		    printf("failed to mmap file: %s  terminating, (sorry)\n", tracepoint_fname);
		    exit(-1);
	    }
	    close(tracepoint_fd);
	}
    }
    else // thread got the ticket
    {
        int i = 0;
	threadcopy_global_status->max_samples = max_samples;
        for(i = 0; i<MAX_TRACE_POINTS; ++i)
	{
            char tracepoint_fname[MAX_FNAME];
	    int tracepoint_fd = 0;
	    int result;

	    sprintf(tracepoint_fname, "%s%s_%d", TOP_DIR, "tracepoint", i);
	    tracepoint_fd = open(tracepoint_fname, O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
	    if(tracepoint_fd <= 0)
	    {
		    printf("failed to open tracefile: %s  terminating, (sorry)\n", tracepoint_fname);
		    exit(-1);
	    } 
	    result = ftruncate(tracepoint_fd, sizeof(pmu_tracepoint) + sizeof(pmu_sample) * max_samples);
	    if(-1 == result)
	    {
		    printf("failed to truncate file: %s  terminating, (sorry)\n", tracepoint_fname);
		    perror(":(");
		    exit(-1);
	    }
	    tracepoints[i] = (pmu_tracepoint *) mmap(NULL,
			                 sizeof(pmu_tracepoint) + sizeof(pmu_sample) * max_samples,  
                                         PROT_READ|PROT_WRITE, MAP_SHARED|MAP_LOCKED,                    
                                         tracepoint_fd, 0);
	    if((void *)-1 == tracepoints[i])
	    {
		    printf("failed to mmap file: %s  terminating, (sorry)\n", tracepoint_fname);
		    exit(-1);
	    }
	    close(tracepoint_fd);
	    tracepoints[i]->tracepoint_id = i;
	    tracepoints[i]->max_samples = max_samples;
	    memcpy(&tracepoints[i]->event_set, event_set, sizeof(pmu_event_set));
	}
	__atomic_store_n(&threadcopy_global_status->initialized,  1, __ATOMIC_SEQ_CST);
    }
    return 0;
}

int pmu_init_global(pmu_event_set *event_set, uint32_t max_samples)
{
    int status = 0;
    shm_unlink(GLOBAL_STATE_MMAP_KEY);
    PAPI_library_init(PAPI_VER_CURRENT);

    #pragma omp parallel
    {
        status |= internal_pmu_init_global(event_set, max_samples);
    }
    return status;
}


int internal_pmu_tracepoint_start(int tracepoint_id)
{
    int EventCode;
    uint32_t *tl_iteration;
    struct timespec *tl_start;

    tl_start = &tl_start_array[tracepoint_id];
    tl_iteration = &tl_iteration_array[tracepoint_id];
    if(*tl_iteration > threadcopy_global_status->max_samples)
    {
        return -1;
    }
    tl_event_set = PAPI_NULL;
    pmu_event_set *eventset = &tracepoints[tracepoint_id]->event_set;
    if(PAPI_OK != PAPI_create_eventset(&tl_event_set))
    {
        printf("failed to create event set at thread %d aborting!\n", pmu_get_thread_id());
	exit(-1);
    }
    else
    {
        //printf("success at creating event set [%d] at thread %d\n", tl_event_set, pmu_get_thread_id());
    }
    for(int i=0; i < eventset->events_in_set; ++i)
    {
        if(PAPI_OK != PAPI_event_name_to_code(eventset->events[i].event_name, &EventCode) )
	{
            printf("failed to get a code for event: %s, will now abort (sorry)\n",
			    eventset->events[i].event_name);
	    exit(-1);
	}
	if(PAPI_OK != PAPI_add_event(tl_event_set, EventCode))
	{
            printf("failed to add event code:%d for event:%s, will now abort (sorry)\n",
			    EventCode, eventset->events[i].event_name);
	    exit(-1);
	}
        
    }
    if(PAPI_OK != PAPI_start(tl_event_set))
    {
         printf("failed to start event set %d at thread %d aborting!\n", tl_event_set, pmu_get_thread_id());
	 exit(-1);
    }
    if(PAPI_OK != PAPI_reset(tl_event_set))
    {
         printf("failed to reset event set %d at thread %d aborting!\n", tl_event_set, pmu_get_thread_id());
	 exit(-1);
    }
    clock_gettime(CLOCK_MONOTONIC, tl_start);
    return 0;
}

int pmu_tracepoint_start(int tracepoint_id)
{
    int status = 0;
    #pragma omp parallel
    {
        status |= internal_pmu_tracepoint_start(tracepoint_id);
    }
    return status;
}

int internal_pmu_tracepoint_stop(int tracepoint_id)
{ 
    uint64_t elapsed_ns;
    uint64_t max_ns, min_ns;
    uint64_t flag;
    long long t_counters[MAX_EVENTS_IN_SET];
    pmu_tracepoint *tracepoint;
    pmu_sample *sample;
    //pmu_sample
    pmu_event_set *eventset;
    uint32_t *tl_iteration;
    struct timespec *tl_start, *tl_end;

    tl_start = &tl_start_array[tracepoint_id];
    tl_end = &tl_end_array[tracepoint_id];
    tl_iteration = &tl_iteration_array[tracepoint_id];
    
    if(*tl_iteration > threadcopy_global_status->max_samples)
    {
        return -1;
    }
    eventset = &tracepoints[tracepoint_id]->event_set;
    tracepoint = tracepoints[tracepoint_id];
    //sample = &(((pmu_sample *)(&(tracepoint->samples)))[tl_iteration++]);
    sample = &tracepoint->samples;
    sample += *tl_iteration;
    (*tl_iteration)++;
    if(PAPI_OK != PAPI_stop(tl_event_set, t_counters))
    {
        printf("failed PAPI_stop at thread %d, aborting! [%d]\n", pmu_get_thread_id(), tl_event_set);
	sleep(1);
	exit(-1);
    }
    clock_gettime(CLOCK_MONOTONIC, tl_end);
    //PAPI_read(tl_event_set, t_counters);
    PAPI_cleanup_eventset(tl_event_set);
    PAPI_destroy_eventset(&tl_event_set);
    for(int i=0; i < eventset->events_in_set; ++i)
    {
	//uint64_t counter;
	//printf("superdebug[%d][%d] -> %ld\n", pmu_get_thread_id(), i, t_counters[i]); 
        __atomic_add_fetch(&sample->counters[i], t_counters[i], __ATOMIC_SEQ_CST);
    }
    elapsed_ns = 1000000000LLU*(tl_end->tv_sec - tl_start->tv_sec) + (tl_end->tv_nsec - tl_start->tv_nsec);
    do
    {
        max_ns = __atomic_load_n (&sample->max_interval_ns, __ATOMIC_SEQ_CST);
	if(max_ns > elapsed_ns)
	    break;
        flag = __atomic_compare_exchange_n(&sample->max_interval_ns, &max_ns, elapsed_ns, 
                                    1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }while(!flag);//this will happen at worse case as many times as the number of threads (unless I am mistaken)
    do
    {
        min_ns = __atomic_load_n(&sample->min_interval_ns, __ATOMIC_SEQ_CST);
	if(min_ns < elapsed_ns)
	    break;
        flag = __atomic_compare_exchange_n(&sample->min_interval_ns, &min_ns, elapsed_ns, 
                                    1, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }while(!flag);
    return 0;
}

int pmu_tracepoint_stop(int tracepoint_id)
{
    int status = 0;
    #pragma omp parallel
    {
        status |= internal_pmu_tracepoint_stop(tracepoint_id);
    }
    return status;
}


int pmu_tracing_finalize()
{
    //munmap(threadcopy_global_status, sizeof(struct pmu_tracing_global_state));
    //shm_unlink(GLOBAL_STATE_MMAP_KEY); // this must be done once
    return 0;
}
