/* MPS2 AN505 (Cortex-M33) under QEMU. The core starts secure, with its
   vector table at 0x10000000, the secure alias of the 4 MB SSRAM at 0; data
   goes in the two 2 MB SSRAMs at 0x38000000, their secure alias. */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 4M
  RAM   (rwx) : ORIGIN = 0x38000000, LENGTH = 4M
}
INCLUDE sections.ld
