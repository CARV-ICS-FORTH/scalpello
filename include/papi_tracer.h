
//#ifndef GADGET_PAPI_TRACER_H
//#define GADGET_PAPI_TRACER_H

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#define PMU_EVENT_NAME_MAX_SZ 64
#define MAX_EVENTS_IN_SET 6
#define MAX_TRACE_POINTS 16
#define GLOBAL_STATE_MMAP_KEY "PMU_TRACING_GLOBAL_AREA"

// FIXME: this is horrible
#define TOP_DIR "./"
#define MAX_FNAME 512

typedef struct pmu_event 
{
    uint64_t papi_id; //Id by which event can be understood by PAPI
    char event_name[PMU_EVENT_NAME_MAX_SZ]; //Name which PAPI can parse and reply with a valid Id
}pmu_event;


//the order of events is how tracing and postprocessing code
//will interpret the ordering in the raw traces, so it is important
//to keep them properly ordered
typedef struct pmu_event_set
{
    uint32_t events_in_set;
    pmu_event events[MAX_EVENTS_IN_SET+1]; //after final event in the set, a 0,0 event is expected
}pmu_event_set;

//the diff between a start and a stop
typedef struct pmu_sample
{
    uint64_t max_interval_ns;
    uint64_t min_interval_ns;
    uint64_t counters[MAX_EVENTS_IN_SET];//this is perhaps wastefull (type and size), should fix
}pmu_sample;


typedef struct pmu_tracepoint
{
    uint32_t max_samples;
    //uint32_t start_iteration;
    pmu_event_set event_set;
    int tracepoint_id;
    struct pmu_sample samples;
}pmu_tracepoint;

typedef struct pmu_tracing_global_state
{
    uint64_t access_sem;
    uint64_t tracepoint_count;
    uint32_t max_samples;
    uint32_t initialized; // this is set only after everything is initialized
}pmu_tracing_global_state;

//typedef struct pmu_thread_status{
//    __thread int isitme;
//}pmu_thread_status;



extern __thread pmu_tracing_global_state *threadcopy_global_status;
extern __thread pmu_tracepoint *tracepoints[MAX_TRACE_POINTS];
extern __thread int tl_event_set;
extern __thread struct timespec tl_start, tl_end;
extern __thread uint32_t tl_iteration;                              
extern pmu_event_set eventset;

int pmu_init_global(pmu_event_set *event_set, uint32_t max_samples);
int pmu_tracepoint_start(int tracepoint_id);
int pmu_tracepoint_stop(int tracepoint_id);
int pmu_tracing_finalize();


//#endif //GADGET_PAPI_TRACER_H

