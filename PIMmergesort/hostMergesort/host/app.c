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

#define Min(a, b) ((a) < (b) ? (a) : (b))

// Pointer declaration
static T* A;
static T* B; 
static T* C; 

// Create input arrays
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

void MergeRecursive(T* src, T* dest, uint32_t left, uint32_t mid, uint32_t right) {
  uint32_t i = left;
  uint32_t j = mid;
  for (uint32_t k = left; k < right; k++) {
    if (i < mid && (j >= right || src[i] <= src[j])) {
      dest[k] = src[i];
      i += 1;
    } else {
      dest[k] = src[j];
      j += 1;
    }
  }
}

void MergeSortRecursive(T* arr, T* temp_buffer, uint32_t left, uint32_t right) {
  if (right - left <= 1) { 
    return;
  }

  uint32_t mid = left + (right - left) / 2;

  MergeSortRecursive(arr, temp_buffer, left, mid);
  MergeSortRecursive(arr, temp_buffer, mid, right);
  MergeRecursive(arr, temp_buffer, left, mid, right);

  for (uint32_t i = left; i < right; i++) {
    arr[i] = temp_buffer[i];
  }
}

void MergeSort(T* arr, unsigned int size) {
  T* temp_buffer = (T*)malloc(size * sizeof(T));
  if (temp_buffer == NULL) {
    fprintf(stderr, "Failed to allocate temporary buffer for MergeSort\n");
    exit(EXIT_FAILURE);
  }

  MergeSortRecursive(arr, temp_buffer, 0, size);
  free(temp_buffer);
}


// Main of the Host Application
int main(int argc, char** argv) {
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
  printf("NR_TASKLETS\t%d\tBL\t%d\n", NR_TASKLETS, BL);

  unsigned int i = 0;
#if PERF
  double cc = 0;
  double cc_min = 0;
#endif

  const unsigned int input_size =
      p.exp == 0 ? p.input_size * nr_of_dpus
                 : p.input_size;  // Total input size (weak or strong scaling)
  const unsigned int input_size_8bytes =
      ((input_size * sizeof(T)) % 8) != 0
          ? roundup(input_size, 8)
          : input_size;  // Input size per DPU (max.), 8-byte aligned
  const unsigned int input_size_dpu =
      divceil(input_size, nr_of_dpus);  // Input size per DPU (max.)
  const unsigned int input_size_dpu_8bytes =
      ((input_size_dpu * sizeof(T)) % 8) != 0
          ? roundup(input_size_dpu, 8)
          : input_size_dpu;  // Input size per DPU (max.), 8-byte aligned

  // Input/output allocation
  A = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T));
  B = malloc(input_size_dpu_8bytes * nr_of_dpus * sizeof(T)); 

  T* bufferA = A;
  T* bufferB = B;

  // Create an input file with arbitrary data
  read_input(A, input_size);

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
    unsigned int kernel = 0;
    dpu_arguments_t input_arguments[NR_DPUS];
    for (i = 0; i < nr_of_dpus - 1; i++) {
      input_arguments[i].size = input_size_dpu_8bytes * sizeof(T);
      input_arguments[i].kernel = kernel;
    }
    input_arguments[nr_of_dpus - 1].size =
        (input_size_8bytes - input_size_dpu_8bytes * (NR_DPUS - 1)) * sizeof(T);
    input_arguments[nr_of_dpus - 1].kernel = kernel;
    // Copy input arrays
    i = 0;
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
    if (rep >= p.n_warmup) stop(&timer, 1);

    printf("Run program on DPU(s) \n");
    // Run DPU kernel
    if (rep >= p.n_warmup) {
      start(&timer, 2, rep - p.n_warmup);
#if ENERGY
      DPU_ASSERT(dpu_probe_start(&probe));
#endif
    }

    DPU_ASSERT(dpu_launch(dpu_set, DPU_SYNCHRONOUS));
    if (rep >= p.n_warmup) {
      stop(&timer, 2);
#if ENERGY
      DPU_ASSERT(dpu_probe_stop(&probe));
#endif
    }

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
    if (rep >= p.n_warmup) start(&timer, 3, rep - p.n_warmup);
    i = 0;
    // PARALLEL RETRIEVE TRANSFER
    DPU_FOREACH(dpu_set, dpu, i) {
      DPU_ASSERT(dpu_prepare_xfer(dpu, bufferB + input_size_dpu_8bytes * i));
    }

    DPU_ASSERT(
        dpu_push_xfer(dpu_set, DPU_XFER_FROM_DPU, DPU_MRAM_HEAP_POINTER_NAME,
                      0,
                      input_size_dpu_8bytes * sizeof(T), DPU_XFER_DEFAULT));
  
    for (int i=0; i< input_size; i++)
    {
      printf("%d\n",bufferB[i]);
    }

    if (rep >= p.n_warmup) stop(&timer, 3);

    // Print timing results
    printf("CPU ");
    print(&timer, 0, p.n_reps);
    printf("CPU-DPU ");
    print(&timer, 1, p.n_reps);
    printf("DPU Kernel ");
    print(&timer, 2, p.n_reps);
    printf("DPU-CPU "); 
    print(&timer, 3, p.n_reps);

#if ENERGY
    double energy;
    DPU_ASSERT(dpu_probe_get(&probe, DPU_ENERGY, DPU_AVERAGE, &energy));
    printf("DPU Energy (J): %f\t", energy);
#endif
  }

  printf("\nPerforming final merge sort on host...\n");
  MergeSort(B, input_size);

  printf("\n[Final Sorted Output]\n");
  for (int i=0; i< input_size; i++)
    {
      printf("%d\n",B[i]);
    }
  
  // Deallocation
  free(A);
  free(B);  

  DPU_ASSERT(dpu_free(dpu_set));

  return 0;
}
