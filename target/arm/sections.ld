/* The sections, for every machine: code and constants in FLASH, the rest in
   RAM, the heap from the end of .bss up to the stack at the top of RAM:
   newlib's _sbrk refuses to grow it past the stack pointer.
   The machine's script defines FLASH and RAM and includes this. */

ENTRY(Reset_Handler)

SECTIONS
{
  .text : {
    KEEP(*(.isr_vector))
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
    __preinit_array_start = .;
    KEEP(*(.preinit_array*))
    __preinit_array_end = .;
    __init_array_start = .;
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    __init_array_end = .;
    __fini_array_start = .;
    KEEP(*(.fini_array*))
    __fini_array_end = .;
  } > FLASH

  .ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } > FLASH
  .ARM.exidx : {
    __exidx_start = .;
    *(.ARM.exidx* .gnu.linkonce.armexidx.*)
    __exidx_end = .;
  } > FLASH

  .data : { . = ALIGN(8); *(.data*) . = ALIGN(8); } > RAM

  .bss (NOLOAD) : {
    . = ALIGN(8);
    __bss_start__ = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(8);
    __bss_end__ = .;
  } > RAM

  end = .;
  _end = .;
  __end__ = .;
  __stack_top = ORIGIN(RAM) + LENGTH(RAM);
}
