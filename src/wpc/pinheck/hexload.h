#ifndef PINHECK_HEXLOAD_H
#define PINHECK_HEXLOAD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Intel HEX (records 00 data, 01 end, 04 upper address, 05 start address) into a flash image of size bytes at
   physical address base. Every record's checksum is checked, every byte must fall inside the image and the file must
   end with record 01. Returns the data bytes written, or -1 with the reason in err (at least 80 bytes). */
long pinheck_hex_flash(const uint8_t *hex, size_t n, uint8_t *flash, uint32_t size, uint32_t base, char *err);

#ifdef __cplusplus
}
#endif

#endif
