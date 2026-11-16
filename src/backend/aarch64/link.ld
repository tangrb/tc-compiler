/* TC 0.0.46 AArch64 subset image layout for QEMU virt (RAM at 0x40000000).
 * Everything the program needs is loaded as ELF PT_LOAD segments; no dynamic
 * loader, no libc and no host runtime participate. */
ENTRY(_start)
SECTIONS
{
  . = 0x40000000;
  .text : {
    *(.text._start)
    *(.text*)
  }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
