/* the parts of PinMAME's driver.h that pic32mxcpu.c uses, for cpu_test.c */
#define REGION_CPU1 1
unsigned char *memory_region(int num);
unsigned memory_region_length(int num);
