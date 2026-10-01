/* the parts of PinMAME's cpuintrf.h that pic32mxcpu.c uses, for cpu_test.c */
#define MAX_REGS 256
enum { REG_PREVIOUSPC = -1, REG_PC = -2, REG_SP = -3 };
enum { CPU_INFO_REG, CPU_INFO_FLAGS = MAX_REGS, CPU_INFO_NAME, CPU_INFO_FAMILY, CPU_INFO_VERSION, CPU_INFO_FILE, CPU_INFO_CREDITS };
