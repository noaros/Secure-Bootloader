#ifndef CRC32_H
#define CRC32_H

#include <stddef.h>
#include <stdint.h>

/* Standard CRC-32 (IEEE 802.3, same as Python's zlib.crc32). */
uint32_t crc32(const void *data, size_t len);

#endif
