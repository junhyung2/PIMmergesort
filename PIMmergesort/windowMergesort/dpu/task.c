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

__host dpu_arguments_t DPU_INPUT_ARGUMENTS;

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

BARRIER_INIT(my_barrier, NR_TASKLETS);

extern int main_kernel1(void);

int (*kernels[nr_kernels])(void) = {main_kernel1}; //

int main(void) { return kernels[DPU_INPUT_ARGUMENTS.kernel](); }

int main_kernel1() {
  unsigned int tasklet_id = me();
  if (tasklet_id == 0) {
    mem_reset();
  }
  barrier_wait(&my_barrier);

  uint32_t input_size_dpu_bytes = DPU_INPUT_ARGUMENTS.size;
  uint32_t input_size_dpu_bytes_transfer = DPU_INPUT_ARGUMENTS.transfer_size;
  uint32_t base_tasklet = tasklet_id << BLOCK_SIZE_LOG2;
  uint32_t mram_base_addr_A = (uint32_t)DPU_MRAM_HEAP_POINTER;
  uint32_t total_blocks = input_size_dpu_bytes / BLOCK_SIZE;
  T *cache_A = (T *)mem_alloc(BLOCK_SIZE);
  T *cache_B = (T *)mem_alloc(BLOCK_SIZE);
  T *cache_C = (T *)mem_alloc(BLOCK_SIZE * 2);

#if !PERF_SYNC
  for (unsigned int byte_index = base_tasklet; byte_index < input_size_dpu_bytes; byte_index += BLOCK_SIZE * NR_TASKLETS) {
    uint32_t l_size_bytes = (byte_index + BLOCK_SIZE >= input_size_dpu_bytes) ? (input_size_dpu_bytes - byte_index) : BLOCK_SIZE;
    mram_read((__mram_ptr void const *)(mram_base_addr_A + byte_index), cache_A, l_size_bytes);
    insertionsort(cache_A, l_size_bytes >> DIV);
    mram_write(cache_A, (__mram_ptr void *)(mram_base_addr_A + byte_index), l_size_bytes);
  }

  barrier_wait(&my_barrier);

  if (tasklet_id == 15) {

    for (uint32_t i = 1; i < total_blocks; i++) {
      uint32_t z_total = 0;
      uint32_t A_block_idx = 0;
      uint32_t A_total_blocks = i; // [0,...,i-1] 
      uint32_t B_block_idx = i;
      uint32_t x = 0, y = 0, z = 0;
      uint32_t A_size = BLOCK_SIZE / sizeof(T); // cache A element 
      uint32_t B_size = BLOCK_SIZE / sizeof(T); // cache B   `` 

      mram_read((__mram_ptr void const *)(mram_base_addr_A + A_block_idx * BLOCK_SIZE), cache_A, BLOCK_SIZE);
      mram_read((__mram_ptr void const *)(mram_base_addr_A + B_block_idx * BLOCK_SIZE), cache_B, BLOCK_SIZE);

      while (z < 2 * A_size && x < A_size && y < B_size) {
        if (cache_A[x] <= cache_B[y]) {
          cache_C[z++] = cache_A[x++];
        } else {
          cache_C[z++] = cache_B[y++];
        }

        if (x == A_size || y == B_size) {

          if (x == A_size && (A_block_idx + 1) == A_total_blocks) {
            while (y < B_size) {
              cache_C[z++] = cache_B[y++];
            }
          }
          mram_write(cache_C, (__mram_ptr void *)(mram_base_addr_A + input_size_dpu_bytes + z_total * sizeof(T)), z * sizeof(T));
          z_total += z;
          z = 0;

          if (x == A_size && ++A_block_idx < A_total_blocks) {
            mram_read((__mram_ptr void const *)(mram_base_addr_A + A_block_idx * BLOCK_SIZE), cache_A, BLOCK_SIZE);
            x = 0;
          } else if (y == B_size && x != A_size) {
            // flush 남은 A
            while (x < A_size) {
              cache_C[z++] = cache_A[x++];
              if (x == A_size) {
                mram_write(cache_C, (__mram_ptr void *)(mram_base_addr_A + input_size_dpu_bytes + z_total * sizeof(T)), z * sizeof(T));
                z_total += z;
                z = 0;
              }
            }
            for (A_block_idx += 1; A_block_idx < A_total_blocks; A_block_idx++) {
              mram_read((__mram_ptr void const *)(mram_base_addr_A + A_block_idx * BLOCK_SIZE), cache_A, BLOCK_SIZE);
              mram_write(cache_A, (__mram_ptr void *)(mram_base_addr_A + input_size_dpu_bytes + (A_block_idx + 1) * BLOCK_SIZE), BLOCK_SIZE);
            }

            break;
          }
        }
      }

      // overwrite merged [0...i] to left mram region
      for (uint32_t j = 0; j <= i; j++) {
        mram_read((__mram_ptr void const *)(mram_base_addr_A + input_size_dpu_bytes + j * BLOCK_SIZE), cache_C, BLOCK_SIZE);
        mram_write(cache_C, (__mram_ptr void *)(mram_base_addr_A + j * BLOCK_SIZE), BLOCK_SIZE);
      }
    }
  }
#endif

  return 0;
}
