#define MAX_CSV_FNAME 32

#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "papi_tracer.h"



int main(int argc, char *argv[])
{
    char csv_fname[MAX_CSV_FNAME];
    int fd=-1;
    uint64_t number_of_events = 0;
    FILE *csvf;
    char columns[MAX_EVENTS_IN_SET][PMU_EVENT_NAME_MAX_SZ];
    uint64_t max_samples = 1000; //FIXME!!!!!

    pmu_tracepoint *tracepoint;
    pmu_event *events;
    pmu_event_set *event_set;
    pmu_sample *samples;

    if(2 != argc)
    {
        printf("%s takes exactly one argument, which is the name of the raw trace\n", argv[0]);
	exit(-1);
    }
    sprintf(csv_fname, "%s.csv", argv[1]);
    fd = open(argv[1], O_RDONLY);
    tracepoint = (pmu_tracepoint *)mmap(NULL,
		                        sizeof(pmu_tracepoint) + sizeof(pmu_sample) * max_samples,
		                        PROT_READ, MAP_PRIVATE, fd, 0);
    if((void *)-1 == tracepoint)
    {
        printf("failed to mmap raw tracepoint file\n");
	exit(-1);
    }
    event_set = &tracepoint->event_set;
    //number_of_events = &tracepoint->event_set->events_in_set;
    events = event_set->events;
    samples = &tracepoint->samples;
    number_of_events = event_set->events_in_set; 
    csvf = fopen(csv_fname, "w");
    fprintf(csvf,"iteration,");
    for(int i=0; i<number_of_events; ++i)
    {
        strcpy(columns[i], events[i].event_name);
        fprintf(csvf, "%s,", columns[i]);
    }
    fprintf(csvf, "elapsed_min,elapsed_max\n");
    for(int i=0; i<max_samples; ++i)
    {
        fprintf(csvf,"%d,",i);
        for(int j=0; j<number_of_events; ++j)
	{
            fprintf(csvf,"%ld,", samples->counters[j]);
	}
        fprintf(csvf,"%ld,%ld\n",samples->min_interval_ns, samples->max_interval_ns);
	samples++;
    }
    close(fd);
    munmap(tracepoint, sizeof(pmu_tracepoint) + sizeof(pmu_sample) * number_of_events);
    fclose(csvf);
    return 0;
}


