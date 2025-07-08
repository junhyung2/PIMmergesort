# PIMmergesort (Processing-In-Memory Mergesort)

## Explanation

This repository contains three different implementations of mergesort designed for the UPMEM PIM (Processing-In-Memory) architecture.

* mergesort: A single-DPU mergesort that uses WRAM and a master tasklet for sorting.

* hostMergesort: A multi-DPU mergesort where the host performs the final merge after DPUs finish their tasks.

* windowMergesort: A multi-DPU mergesort that performs all merging inside the DPUs without involving the host. In this implementation, the number of DPUs is fixed to 3: two for the initial merge, and one for the final merge. For example, if the input size is 4MB, DPUs 1 and 2 process 2MB each, and DPU 3 receives 4MB as the final merged input.(See app.c of windowMerge, lines 100–105 for hardcoded behavior.)

## Prerequisites

To run this project, you must first install the [UPMEM SDK](https://sdk.upmem.com). 

## Running Mergesort

### Execution

To compile and run the code with default parameters:
```
cd Mergesort
make test
```
if you changed some parameters : 

```
make clean
make test
```
### Handling Parameters

You can customize parameters such as block size, number of DPUs, number of tasklets, and input size via the Makefile and command-line options.

Example:
```
./${HOST_TARGET} -w 0 -e 1 -i 512 -x 1
```

This command line corresponds to the following options:

```
while ((opt = getopt(argc, argv, "hi:w:e:x:")) >= 0) {
    switch (opt) {
    case 'h': usage(); exit(0); break;
    case 'i': p.input_size = atoi(optarg); break;
    case 'w': p.n_warmup = atoi(optarg); break;
    case 'e': p.n_reps = atoi(optarg); break;
    case 'x': p.exp = atoi(optarg); break;
    default:
        fprintf(stderr, "\nUnrecognized option!\n");
        usage();
        exit(0);
    }
}
```


## Bottlenecks

### Current Issues

The current windowMerge implementation for UPMEM does not function correctly, while the CPU version using the same logic works as expected. This issue is likely caused by problems related to address allocation or limited WRAM cache capacity during MRAM-to-WRAM data transfers.

### Goal

Our short-term goals for improving windowMergesort include:

* To enable scalability with more DPUs, we need a mechanism that allows a DPU to send signals to the host when certain tasks are completed — even before the DPU launch finishes.

* To eliminate the need for a master tasklet, we are exploring lightweight synchronization methods between tasklets that can minimize overhead.

## Contact
If you have any suggestions or ideas for improvement, feel free to contact me. (peter04771@knu.ac.kr)


