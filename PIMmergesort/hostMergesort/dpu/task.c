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

// insertion sort
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

// merge 함수 -> host merge이므로 필요없음

//  void Merge(int* unsorted, uint32_t left, uint32_t mid, uint32_t right,
//             int* sorted) {
//    uint32_t i = left;
//    uint32_t j = mid;
//    for (int k = left; k < right; k++) {
//      if (i < mid && (j >= right || unsorted[i] <= unsorted[j])) {
//        sorted[k] = unsorted[i];
//        i += 1;
//      } else {
//        sorted[k] = unsorted[j];
//        j += 1;
//      }
//    }
//  }

//  void CopyBufferBfromA(int* bufferA, int* bufferB, uint32_t size) {
//    for (uint32_t idx = 0; idx < size; ++idx) {
//      bufferB[idx] = bufferA[idx];
//    }
//  }

//  void __attribute__((noinline)) iterativeMerge(int* bufferB, int* bufferA,
//                                                unsigned int l_size) {
//    bool isAtoB = true;
//    for (uint32_t width = BLOCK_SIZE / sizeof(T); width < l_size; width *= 2)
//    {
//      for (uint32_t idx = 0; idx < l_size; idx += 2 * width) {
//        if (isAtoB)
//          Merge(bufferA, idx, Min(idx + width, l_size),
//                Min(idx + 2 * width, l_size), bufferB);
//        else
//          Merge(bufferB, idx, Min(idx + width, l_size),
//                Min(idx + 2 * width, l_size), bufferA);
//      }
//      isAtoB = !isAtoB;
//    }
//    if (!isAtoB) {
//      CopyBufferBfromA(bufferB, bufferA, l_size);
//    }
//  }

// Barrier 초기화
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
  uint32_t total_written_bytes = 0;
  uint32_t input_size_dpu_bytes = DPU_INPUT_ARGUMENTS.size;
  uint32_t input_size_dpu_bytes_transfer = DPU_INPUT_ARGUMENTS.transfer_size; // Transfer input size per DPU in bytes
  uint32_t base_tasklet = tasklet_id << BLOCK_SIZE_LOG2;
  uint32_t mram_base_addr_A = (uint32_t)DPU_MRAM_HEAP_POINTER;
  T *cache_A = (T *)mem_alloc(BLOCK_SIZE);

#if !PERF_SYNC
  for (unsigned int byte_index = base_tasklet; byte_index < input_size_dpu_bytes; byte_index += BLOCK_SIZE * NR_TASKLETS) {
    uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= input_size_dpu_bytes) ? (input_size_dpu_bytes - byte_index) : BLOCK_SIZE;
    // 이 block만큼 write함
    total_written_bytes += l_size_bytes;
    mram_read((__mram_ptr void const *)(mram_base_addr_A + byte_index), cache_A, l_size_bytes);
    insertionsort(cache_A, l_size_bytes >> DIV);

    mram_write(cache_A, (__mram_ptr void *)(mram_base_addr_A + byte_index), l_size_bytes);
  }
#endif

  barrier_wait(&my_barrier);

#if PERF && PERF_SYNC
  result->cycles = timer_stop(&cycles);
#endif

  //  if (tasklet_id == 0) {
  //    T* cache_B = (T*)mem_alloc(total_written_bytes);
  //    T* cache_C = (T*)mem_alloc(total_written_bytes);

  //    mram_read((__mram_ptr void const*)(mram_base_addr_A), cache_B,
  //              total_written_bytes);

  //    uint32_t num_total_elements = total_written_bytes >> DIV;

  //    if (num_total_elements > 0) {
  //      iterativeMerge(cache_B, cache_C, num_total_elements);
  //    }

  //    mram_write(cache_B, (__mram_ptr void*)(mram_base_addr_A),
  //               total_written_bytes);
  //  }
#if PERF && !PERF_SYNC
  result->cycles = timer_stop(&cycles);
#endif

  return 0;
}
