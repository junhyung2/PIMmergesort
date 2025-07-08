#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define BLOCK_SIZE 64
#define ELEMENTS_PER_BLOCK 16
#define TOTAL_BLOCKS 8
#define TOTAL_ELEMENTS (TOTAL_BLOCKS * ELEMENTS_PER_BLOCK)

typedef uint32_t T;

void array_copy(T *dst, T *src, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
    {
        dst[i] = src[i];
    }
}

void print_array(const char *label, T *arr, uint32_t size)
{
    printf("%s: ", label);
    for (uint32_t i = 0; i < size; i++)
    {
        printf("%d ", arr[i]);
    }
    printf("\n");
}

void print_block(const char *label, T *base, uint32_t start_block, uint32_t num_blocks)
{
    printf("%s\n", label);
    for (uint32_t b = 0; b < num_blocks; b++)
    {
        printf("Block %u: ", start_block + b);
        for (uint32_t i = 0; i < ELEMENTS_PER_BLOCK; i++)
        {
            printf("%d ", base[(start_block + b) * ELEMENTS_PER_BLOCK + i]);
        }
        printf("\n");
    }
    printf("\n");
}

void generate_disordered_sorted_blocks(T *mram_base)
{

    T block_starts[TOTAL_BLOCKS] = {100, 3, 395, 64, 283, 33, 5, 791};

    for (int i = 0; i < TOTAL_BLOCKS; i++)
    {
        int generator = (rand() % 1000);
        for (int j = 0; j < ELEMENTS_PER_BLOCK; j++)
        {
            mram_base[i * ELEMENTS_PER_BLOCK + j] = block_starts[i] + j * generator;
        }
    }
}

void window_merge_on_cpu(T *mram_base, uint32_t total_blocks, uint32_t input_size_dpu_bytes)
{
    T cache_A[ELEMENTS_PER_BLOCK];
    T cache_B[ELEMENTS_PER_BLOCK];
    T cache_C[2 * ELEMENTS_PER_BLOCK];

    for (uint32_t i = 1; i < total_blocks; i++)
    {
        int count = 0;
        printf("== Iteration i = %u: merging blocks [0...%u] and [%u] ==\n", i, i - 1, i);

        uint32_t z_total = 0;
        uint32_t A_block_idx = 0;
        uint32_t A_total_blocks = i;
        uint32_t B_block_idx = i;
        uint32_t x = 0, y = 0, z = 0;

        array_copy(cache_A, mram_base + A_block_idx * ELEMENTS_PER_BLOCK, ELEMENTS_PER_BLOCK);
        print_array("  >> Loaded cache_A (block 0)", cache_A, ELEMENTS_PER_BLOCK);

        array_copy(cache_B, mram_base + B_block_idx * ELEMENTS_PER_BLOCK, ELEMENTS_PER_BLOCK);
        print_array("  >> Loaded cache_B (block i)", cache_B, ELEMENTS_PER_BLOCK);

        while (z < 2 * ELEMENTS_PER_BLOCK && y < ELEMENTS_PER_BLOCK && x < ELEMENTS_PER_BLOCK)
        {

            if (i < 4)
            {
                printf("starting #%d while iteration, i is %u\n", count++, i);
            }

            if (cache_A[x] <= cache_B[y])
            {
                printf("cache_A[%d] (value %u) moved to cache_C[%d]\n", x, cache_A[x], z);
                cache_C[z++] = cache_A[x++];
            }
            else
            {
                printf("cache_B[%d] (value %u)moved to cache_C[%d]\n", y, cache_B[y], z);
                cache_C[z++] = cache_B[y++];
            }

            if (x == ELEMENTS_PER_BLOCK || y == ELEMENTS_PER_BLOCK)
            {
                if (x == ELEMENTS_PER_BLOCK && (A_block_idx + 1) == A_total_blocks)
                {
                    while (y < ELEMENTS_PER_BLOCK)
                    {
                        printf("cache_B[%d] (value %u)moved to cache_C[%d]\n", y, cache_B[y], z);
                        cache_C[z++] = cache_B[y++];
                    }
                }
                print_array("  >> cache_C before write", cache_C, z);

                for (uint32_t k = 0; k < z; k++)
                {
                    mram_base[128 + z_total + k] = cache_C[k];
                }

                printf("  >> Wrote %u elements to temp area (offset %u)\n", z, z_total);
                printf("z value %u add to %u, x is %u, y is %u\n", z, z_total, x, y);
                z_total += z;

                z = 0;

                if (x == ELEMENTS_PER_BLOCK && ++A_block_idx < A_total_blocks)
                {
                    array_copy(cache_A, mram_base + A_block_idx * ELEMENTS_PER_BLOCK, ELEMENTS_PER_BLOCK);
                    x = 0;
                    printf("  >> Loaded next cache_A block (%u)\n", A_block_idx);
                    print_array("     cache_A", cache_A, ELEMENTS_PER_BLOCK);
                }
                else if (y == ELEMENTS_PER_BLOCK && x != ELEMENTS_PER_BLOCK)
                {
                    printf("  >> B block finished, flushing rest of A. x is %u \n", x);
                    while (x < ELEMENTS_PER_BLOCK)
                    {
                        cache_C[z++] = cache_A[x++];
                        if (x == ELEMENTS_PER_BLOCK)
                        {
                            print_array("  >> cache_C before flush write", cache_C, z);
                            for (uint32_t k = 0; k < z; k++)
                            {
                                mram_base[input_size_dpu_bytes / sizeof(T) + z_total + k] = cache_C[k];
                            }
                            z_total += z;
                            z = 0;
                        }
                    }
                    for (A_block_idx += 1; A_block_idx < A_total_blocks; A_block_idx++)
                    {
                        array_copy(cache_A, mram_base + A_block_idx * ELEMENTS_PER_BLOCK, ELEMENTS_PER_BLOCK);
                        print_array("  >> Copied remaining A block", cache_A, ELEMENTS_PER_BLOCK);
                        for (uint32_t k = 0; k < ELEMENTS_PER_BLOCK; k++)
                        {
                            mram_base[input_size_dpu_bytes / sizeof(T) + (A_block_idx + 1) * ELEMENTS_PER_BLOCK + k] = cache_A[k];
                        }
                    }
                    break;
                }
            }
        }

        // 병합된 [0...i]를 다시 왼쪽으로 덮어쓰기
        for (uint32_t j = 0; j <= i; j++)
        {
            for (uint32_t k = 0; k < ELEMENTS_PER_BLOCK; k++)
            {
                cache_C[k] = mram_base[input_size_dpu_bytes / sizeof(T) + j * ELEMENTS_PER_BLOCK + k];
            }
            for (uint32_t k = 0; k < ELEMENTS_PER_BLOCK; k++)
            {
                mram_base[j * ELEMENTS_PER_BLOCK + k] = cache_C[k];
            }
        }

        print_block("  >> After iteration", mram_base, 0, i + 1);
    }
}

int main()
{
    T mram_base[10240] = {0};
    uint32_t input_size_dpu_bytes = TOTAL_BLOCKS * BLOCK_SIZE;

    generate_disordered_sorted_blocks(mram_base);

    print_block("Before merge:", mram_base, 0, TOTAL_BLOCKS);

    window_merge_on_cpu(mram_base, TOTAL_BLOCKS, input_size_dpu_bytes);

    print_block("Final After merge:", mram_base, 0, TOTAL_BLOCKS);

    return 0;
}
