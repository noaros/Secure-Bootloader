/* Slot B = 0x08120000 (bank 2); the first 0x200 bytes hold the signed image header. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08120200, LENGTH = 384K - 0x200
    RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 192K
}
INCLUDE sections.ld
