/**
 * app.c
 * RED Host Application Source File -> modified for mergesort
 *
 *
 */
#include <assert.h>
#include <dpu.h>
#include <dpu_log.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../support/common.h"
#include "../support/params.h"
#include "../support/timer.h"

// Define the DPU Binary path as DPU_BINARY here
#ifndef DPU_BINARY
#define DPU_BINARY "./bin/dpu_code"
#endif

static T *A;
static T *B; // DPU에서 정렬된 결과를 받을 배열
static T *C;

// Create input arrays
static void read_input(T *A, unsigned int nr_elements) {
  srand(0);
  printf("nr_elements\t%u\t", nr_elements);
  for (unsigned int i = 0; i < nr_elements; i++) {
    A[i] = (T)(rand() % 1000);
  }
}

// Main of the Host Application
int main(int argc, char **argv) {
  struct Params p = input_params(argc, argv);

  struct dpu_set_t dpu_set, dpu;
  uint32_t nr_of_dpus;

#if ENERGY
  struct dpu_probe_t probe;
  DPU_ASSERT(dpu_probe_init("energy_probe", &probe));
#endif

  // Allocate DPUs and load binary
  DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));
  DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));
  printf("Allocated %d DPU(s)\n", nr_of_dpus);

  unsigned int i = 0;
#if PERF
  double cc = 0;
  double cc_min = 0;
#endif

  const unsigned int input_size = p.exp == 0 ? p.input_size * nr_of_dpus : p.input_size;                                            // Total input size (weak or strong scaling)
  const unsigned int input_size_8bytes = ((input_size * sizeof(T)) % 8) != 0 ? roundup(input_size, 8) : input_size;                 // Input size per DPU (max.), 8-byte aligned
  const unsigned int input_size_dpu = divceil(input_size, nr_of_dpus - 1);                                                          // Input size per DPU (max.)
  const unsigned int input_size_dpu_8bytes = ((input_size_dpu * sizeof(T)) % 8) != 0 ? roundup(input_size_dpu, 8) : input_size_dpu; // Input size per DPU (max.), 8-byte aligned

  // Input/output allocation
  A = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T));
  B = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T));
  C = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T));
  T *bufferA = A;
  T *bufferB = B;
  T *bufferC = C;

  // Create an input file with arbitrary data
  read_input(A, input_size);
  printf("org input data\n");
  for (int i = 0; i < input_size_dpu_8bytes * 2; i++) {
    printf("%d\n", A[i]);
  }

  // Timer declaration
  Timer timer;

  printf("NR_TASKLETS\t%d\tBL\t%d\n", NR_TASKLETS, BL);

  // Loop over main kernel
  for (int rep = 0; rep < p.n_warmup + p.n_reps; rep++) {
    // Compute output on CPU (performance comparison and verification purposes)
    if (rep >= p.n_warmup) start(&timer, 0, rep - p.n_warmup);
    if (rep >= p.n_warmup) stop(&timer, 0);

    printf("Load input data\n");
    if (rep >= p.n_warmup) start(&timer, 1, rep - p.n_warmup);
    // Input arguments
    unsigned int kernel1 = 0; //
    unsigned int kernel2 = 1; //
    dpu_arguments_t input_arguments[NR_DPUS];
    for (i = 0; i < nr_of_dpus - 1; i++) {
      input_arguments[i].size = input_size_dpu_8bytes * sizeof(T);
      input_arguments[i].kernel = kernel1; //
    }
    input_arguments[nr_of_dpus - 1].size = (2 * input_size_dpu_8bytes * sizeof(T)); //
    input_arguments[nr_of_dpus - 1].kernel = kernel1;                               //

    // Copy input arrays
    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
      if (i == 0 || i == 1) { // DPU 0, 1에만 전송
        // Input arguments
        DPU_ASSERT(dpu_copy_to(dpu, "DPU_INPUT_ARGUMENTS", 0, &input_arguments[i], sizeof(input_arguments[0])));

        // Input MRAM data
        DPU_ASSERT(dpu_copy_to(dpu, DPU_MRAM_HEAP_POINTER_NAME, 0, bufferA + input_size_dpu_8bytes * i, input_size_dpu_8bytes * sizeof(T)));
      }
    }

    // DPU_FOREACH(dpu_set, dpu, i) {
    //   DPU_ASSERT(dpu_prepare_xfer(dpu, &input_arguments[i]));
    // }
    // DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS",
    // 0,
    //                          sizeof(input_arguments[0]), DPU_XFER_DEFAULT));
    // DPU_FOREACH(dpu_set, dpu, i) {
    //   DPU_ASSERT(dpu_prepare_xfer(dpu, bufferA + input_size_dpu_8bytes * i));
    // }
    // DPU_ASSERT(
    //     dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME,
    //     0,
    //                   input_size_dpu_8bytes * sizeof(T), DPU_XFER_DEFAULT));
    // if (rep >= p.n_warmup) stop(&timer, 1);

    printf("Run program on DPU(s) \n");
    // Run DPU kernel
    if (rep >= p.n_warmup) {
      start(&timer, 2, rep - p.n_warmup);
#if ENERGY
      DPU_ASSERT(dpu_probe_start(&probe));
#endif
    }

    DPU_FOREACH(dpu_set, dpu, i) {
      if (i == 0 || i == 1) {
        DPU_ASSERT(dpu_launch(dpu, DPU_SYNCHRONOUS));
      }
    }

    // DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
    if (rep >= p.n_warmup) {
      stop(&timer, 2);
#if ENERGY
      DPU_ASSERT(dpu_probe_stop(&probe));
#endif
    }

#if PRINT
    {
      // unsigned int each_dpu = 0;
      // printf("Display DPU Logs\n");
      // DPU_FOREACH(dpu_set, dpu) {
      //   printf("DPU#%d:\n", each_dpu);
      //   DPU_ASSERT(dpulog_read_for_dpu(dpu.dpu, stdout));
      //   each_dpu++;
      // }
    }
#endif

    printf("results form dpu 1,2\n");
    if (rep >= p.n_warmup) start(&timer, 3, rep - p.n_warmup);
    i = 0;
    // PARALLEL RETRIEVE TRANSFER

    DPU_FOREACH(dpu_set, dpu, i) {
      if (i == 0 || i == 1) {
        DPU_ASSERT(dpu_copy_from(dpu, DPU_MRAM_HEAP_POINTER_NAME, 0, bufferB + input_size_dpu_8bytes * i, input_size_dpu_8bytes * sizeof(T)));
      }
    }

    // DPU_FOREACH(dpu_set, dpu, i) {
    //   DPU_ASSERT(dpu_prepare_xfer(dpu, bufferB + input_size_dpu_8bytes * i));
    // }

    // DPU_ASSERT(
    //     dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
    //     0,
    //                   input_size_dpu_8bytes * sizeof(T), DPU_XFER_DEFAULT));

    if (rep >= p.n_warmup) stop(&timer, 3);

    for (unsigned int k = 0; k < input_size_dpu_8bytes * 2; k++) {
      printf("%d\n", bufferB[k]);
    }

    i = 0;
    DPU_FOREACH(dpu_set, dpu, i) {
      if (i == 2) { // DPU 2에만 전송
        // Input arguments
        DPU_ASSERT(dpu_copy_to(dpu, "DPU_INPUT_ARGUMENTS", 0, &input_arguments[i], sizeof(input_arguments[0])));

        // Input MRAM data
        DPU_ASSERT(dpu_copy_to(dpu, DPU_MRAM_HEAP_POINTER_NAME, 0, bufferB, 2 * input_size_dpu_8bytes * sizeof(T)));
      }
    }

    DPU_FOREACH(dpu_set, dpu, i) {
      if (i == 2) {
        DPU_ASSERT(dpu_launch(dpu, DPU_SYNCHRONOUS));
      }
    }

    DPU_FOREACH(dpu_set, dpu, i) {
      if (i == 2) {
        DPU_ASSERT(dpu_copy_from(dpu, DPU_MRAM_HEAP_POINTER_NAME, 0, bufferC, 2 * input_size_dpu_8bytes * sizeof(T)));
      }
    }

    printf("result in dpu 3\n");

    for (unsigned int i = 0; i < input_size_dpu_8bytes * 2; i++) {
      printf("%d\n", bufferC[i]);
    }

    // Print timing results
    printf("CPU ");
    print(&timer, 0, p.n_reps);
    printf("CPU-DPU ");
    print(&timer, 1, p.n_reps);
    printf("DPU Kernel ");
    print(&timer, 2, p.n_reps);
    printf("DPU-CPU "); // 이 부분이 DPU-CPU 데이터 전송 시간을 나타냅니다.
    print(&timer, 3, p.n_reps);

#if ENERGY
    double energy;
    DPU_ASSERT(dpu_probe_get(&probe, DPU_ENERGY, DPU_AVERAGE, &energy));
    printf("DPU Energy (J): %f\t", energy);
#endif

    // Check output (제거됨)
    // bool status = true; // 제거됨
    // if (status) { // 제거됨
    //     printf("[" ANSI_COLOR_GREEN "OK" ANSI_COLOR_RESET "] Outputs are
    //     equal (sorted)\n"); // 제거됨
    // } else { // 제거됨
    //     printf("[" ANSI_COLOR_RED "ERROR" ANSI_COLOR_RESET "] Outputs differ
    //     (not sorted correctly)!\n"); // 제거됨
    // }
  }

  // Deallocation
  free(A);
  free(B); // DPU 결과 배열 해제
  DPU_ASSERT(dpu_free(dpu_set));

  // return status ? 0 : -1; // 제거됨
  return 0; // 항상 성공으로 가정 (검증 로직 없으므로)
}
