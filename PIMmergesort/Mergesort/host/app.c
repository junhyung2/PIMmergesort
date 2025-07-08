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

#ifndef DPU_BINARY
#define DPU_BINARY "./bin/dpu_code"
#endif

#if ENERGY
#include <dpu_probe.h>
#endif

static T* A;
static T* B;

static void read_input(T* A, unsigned int nr_elements) {
  srand(0);
  printf("nr_elements\t%u\t", nr_elements);
  for (unsigned int i = 0; i < nr_elements; i++) {
    A[i] = (T)(rand() % 10000);
  }
  printf("\n[Original Input Data]\n");
  for (unsigned int i = 0; i < Min(nr_elements, 100); i++) {
    printf("%d\n", A[i]);
  }
}

int main(int argc, char** argv) {
  struct Params p = input_params(argc, argv);

  struct dpu_set_t dpu_set, dpu;
  uint32_t nr_of_dpus;

#if ENERGY
  struct dpu_probe_t probe;
  DPU_ASSERT(dpu_probe_init("energy_probe", &probe));
#endif

  DPU_ASSERT(dpu_alloc(NR_DPUS, NULL, &dpu_set));
  DPU_ASSERT(dpu_load(dpu_set, DPU_BINARY, NULL));
  DPU_ASSERT(dpu_get_nr_dpus(dpu_set, &nr_of_dpus));
  printf("Allocated %d DPU(s)\n", nr_of_dpus);

  printf("NR_TASKLETS\t%d\tBL\t%d\n", NR_TASKLETS, BL);

  unsigned int i = 0;

  const unsigned int input_size =
      (p.exp == 0) ? p.input_size * nr_of_dpus : p.input_size;

//   const unsigned int input_size_8bytes =
//       ((input_size * sizeof(T)) % 8) != 0
//           ? roundup(input_size, 8)
//           : input_size;

  const unsigned int input_size_dpu =
      divceil(input_size, nr_of_dpus);

  const unsigned int input_size_dpu_8bytes =
      ((input_size_dpu * sizeof(T)) % 8) != 0
          ? roundup(input_size_dpu, 8)
          : input_size_dpu;

  A = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T));
  B = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T));

  T* bufferA = A;
  T* bufferB = B;

  read_input(A, input_size);

  Timer timer;
  printf("NR_TASKLETS\t%d\tBL\t%d\n", NR_TASKLETS, BL);

  // Set up input arguments
  unsigned int kernel = 0;
  dpu_arguments_t input_arguments[NR_DPUS];
  for (i = 0; i < nr_of_dpus; i++) {
    input_arguments[i].size = input_size_dpu_8bytes * sizeof(T);
    input_arguments[i].kernel = kernel;
  }

  printf("Load input data\n");
  start(&timer, 1, 0);
  DPU_FOREACH(dpu_set, dpu, i) {
    DPU_ASSERT(dpu_prepare_xfer(dpu, &input_arguments[i]));
  }
  DPU_ASSERT(dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, "DPU_INPUT_ARGUMENTS", 0,
                           sizeof(input_arguments[0]), DPU_XFER_DEFAULT));
  DPU_FOREACH(dpu_set, dpu, i) {
    DPU_ASSERT(dpu_prepare_xfer(dpu, bufferA + input_size_dpu_8bytes * i));
  }
  DPU_ASSERT(
      dpu_push_xfer(dpu_set, DPU_XFER_TO_DPU, DPU_MRAM_HEAP_POINTER_NAME, 0,
                    input_size_dpu_8bytes * sizeof(T), DPU_XFER_DEFAULT));
  stop(&timer, 1);

  printf("Run program on DPU(s)\n");
  start(&timer, 2, 0);
#if ENERGY
  DPU_ASSERT(dpu_probe_start(&probe));
#endif
  DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
  stop(&timer, 2);
#if ENERGY
  DPU_ASSERT(dpu_probe_stop(&probe));
#endif

#if PRINT
  {
    unsigned int each_dpu = 0;
    printf("Display DPU Logs\n");
    DPU_FOREACH(dpu_set, dpu) {
      printf("DPU#%d:\n", each_dpu);
      DPU_ASSERT(dpulog_read_for_dpu(dpu.dpu, stdout));
      each_dpu++;
    }
  }
#endif

  printf("sorted blocks from dpus\n");
  start(&timer, 3, 0);
  i = 0;
  DPU_FOREACH(dpu_set, dpu, i) {
    DPU_ASSERT(dpu_prepare_xfer(dpu, bufferB + input_size_dpu_8bytes * i));
  }
  DPU_ASSERT(
      dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                    0, input_size_dpu_8bytes * sizeof(T), DPU_XFER_DEFAULT));
  stop(&timer, 3);

  for (unsigned int i = 0; i < input_size; i++) {
    printf("%d\n", bufferB[i]);
  }

  printf("CPU-DPU ");
  print(&timer, 1, 1);
  printf("DPU Kernel ");
  print(&timer, 2, 1);
  printf("DPU-CPU ");
  print(&timer, 3, 1);

#if ENERGY
  double energy;
  DPU_ASSERT(dpu_probe_get(&probe, DPU_ENERGY, DPU_AVERAGE, &energy));
  printf("DPU Energy (J): %f\n", energy);
#endif

  free(A);
  free(B);
  DPU_ASSERT(dpu_free(dpu_set));

  return 0;
}
