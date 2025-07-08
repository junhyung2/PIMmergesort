/*
 * Mergesort Tasklet Code for uPIMulator
 */

#include <alloc.h>
#include <barrier.h>
#include <defs.h>
#include <handshake.h>
#include <mram.h>
#include <perfcounter.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/common.h"
#include "../support/cyclecount.h"

#define Min(a, b) ((a) < (b) ? (a) : (b))

__host dpu_arguments_t DPU_INPUT_ARGUMENTS;

//insertion sort
void __attribute__((noinline)) insertionsort(T *input, unsigned int l_size) {
  for (unsigned int i = 1; i < l_size; i++) {
    T key = input[i];
    int j = i - 1;
    while (j >= 0 && input[j] > key) {
      input[j + 1] = input[j];
      j--;
    }
    input[j + 1] = key;
  }
}

void merge_blocks(T *a, uint32_t a_size, T *b, uint32_t b_size, T *out) {
  uint32_t i = 0, j = 0, k = 0;

  while (i < a_size && j < b_size) {
    if (a[i] <= b[j]) {
      out[k++] = a[i++];
    } else {
      out[k++] = b[j++];
    }
  }
  while (i < a_size)
    out[k++] = a[i++];
  while (j < b_size)
    out[k++] = b[j++];
}

void merge_all_blocks(T *src, T *dst, uint32_t src_size, uint32_t block_size) {
  T *in = src;
  T *out = dst;
  T *tmp;

  uint32_t current_block_size = block_size;

  while (current_block_size < src_size) {
    uint32_t num_blocks = src_size / current_block_size;
    uint32_t out_offset = 0;

    for (uint32_t i = 0; i < num_blocks; i += 2) {
      uint32_t a_start = i * current_block_size;
      uint32_t b_start = (i + 1) * current_block_size;

      uint32_t a_size = current_block_size;
      uint32_t b_size = current_block_size;

      if (b_start >= src_size) {
        for (uint32_t j = 0; j < a_size; ++j)
          out[out_offset + j] = in[a_start + j];
        out_offset += a_size;
        break;
      }

      merge_blocks(in + a_start, a_size, in + b_start, b_size, out + out_offset);
      out_offset += a_size + b_size;
    }

    tmp = in;
    in = out;
    out = tmp;

    current_block_size *= 2;
  }

  if (in != dst) {
    for (uint32_t i = 0; i < src_size; ++i) {
      dst[i] = in[i];
    }
  }
}

// Barrier
BARRIER_INIT(my_barrier, NR_TASKLETS);

extern int main_kernel1(void);
int (*kernels[nr_kernels])(void) = {main_kernel1};

int main(void) { return kernels[DPU_INPUT_ARGUMENTS.kernel](); }

int main_kernel1() {
  unsigned int tasklet_id = me();
  if (tasklet_id == 0) {
    mem_reset();
#if PERF
    perfcounter_config(COUNT_CYCLES, true);
#endif
  }
  barrier_wait(&my_barrier);

#if PERF && !PERF_SYNC
  result->cycles = 0;
  perfcounter_cycles cycles;
  timer_start(&cycles);
#endif
  uint32_t input_size_dpu_bytes = DPU_INPUT_ARGUMENTS.size;
  // uint32_t input_size_dpu_bytes_transfer = DPU_INPUT_ARGUMENTS .transfer_size;  // Transfer input size per DPU in bytes
  uint32_t base_tasklet = tasklet_id << BLOCK_SIZE_LOG2;
  uint32_t mram_base_addr_A = (uint32_t)DPU_MRAM_HEAP_POINTER;
  T *cache_A = (T *)mem_alloc(BLOCK_SIZE);

#if !PERF_SYNC
  for (unsigned int byte_index = base_tasklet; byte_index < input_size_dpu_bytes; byte_index += BLOCK_SIZE * NR_TASKLETS) {
    uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= input_size_dpu_bytes) ? (input_size_dpu_bytes - byte_index) : BLOCK_SIZE;

    mram_read((__mram_ptr void const *)(mram_base_addr_A + byte_index), cache_A, l_size_bytes);
    insertionsort(cache_A, l_size_bytes >> DIV);
    mram_write(cache_A, (__mram_ptr void *)(mram_base_addr_A + byte_index), l_size_bytes);
  }
#endif

#if PERF && PERF_SYNC
  result->cycles = timer_stop(&cycles);
#endif

  barrier_wait(&my_barrier);

  if (tasklet_id == 15) {

    T *cache_B = (T *)mem_alloc(input_size_dpu_bytes);
    T *cache_C = (T *)mem_alloc(input_size_dpu_bytes);

    mram_read((__mram_ptr void const *)(mram_base_addr_A), cache_B, input_size_dpu_bytes);

    merge_all_blocks(cache_B, cache_C, (input_size_dpu_bytes / sizeof(T)), (BLOCK_SIZE / sizeof(T)));

    mram_write(cache_C, (__mram_ptr void *)(mram_base_addr_A), input_size_dpu_bytes);
  }

#if PERF && !PERF_SYNC
  result->cycles = timer_stop(&cycles);
#endif

  return 0;
}
