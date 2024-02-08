#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <omp.h>
#include "papi_tracer.h"

#define N 1000
#define M 1000
#define K 1000

static double A[M][N];
static double B[M][N];
static double C[M][N];




// Matrix multiplication
void matrix_multiply(double A[N][M], double B[M][K], double C[N][K]) {
    #pragma omp parallel for
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < K; j++) {
            for (int k = 0; k < M; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
}

#define NN 5

int main() {
    // Initialize PAPI
//    PAPI_library_init(PAPI_VER_CURRENT);
    
    pmu_event_set eventset;
    eventset.events_in_set=4;
    strcpy(eventset.events[0].event_name, "INST_SPEC");
    strcpy(eventset.events[1].event_name, "SVE_INST_RETIRED");
    strcpy(eventset.events[2].event_name, "FP_SPEC");
    strcpy(eventset.events[3].event_name, "LDST_SPEC");
//    eventset.events[0].event_name="INST_SPEC";
//    eventset.events[1].event_name="SVE_INST_RETIRED";
//    eventset.events[2].event_name="FP_SPEC";
//    eventset.events[3].event_name="LDST_SPEC";
    


    // Create matrices

    // Initialize matrices
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < M; j++) {
            A[i][j] = (double)rand() / RAND_MAX;
        }
    } 

    for (int i = 0; i < M; i++) {
        for (int j = 0; j < K; j++) {
            B[i][j] = (double)rand() / RAND_MAX;
        }
    }


pmu_init_global(&eventset, 100);
for(int n=0; n<NN; ++n){    
    
    pmu_tracepoint_start(0);
    // Matrix multiplication
    #pragma omp parallel for
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < K; j++) {
            for (int k = 0; k < M; k++) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }
    pmu_tracepoint_stop(0);
}

    return 0;
}
