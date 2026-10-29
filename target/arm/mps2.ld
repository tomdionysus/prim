/* MPS2 AN385 (Cortex-M3) and AN500 (Cortex-M7) under QEMU: 4 MB of SSRAM
   at 0 for code, 4 MB at 0x20000000 for data. */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 4M
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 4M
}
INCLUDE sections.ld
