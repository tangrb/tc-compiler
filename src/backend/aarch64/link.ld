/* TC 0.0.46 AArch64 subset image layout for QEMU virt (RAM at 0x40000000).
 * Everything the program needs is loaded as ELF PT_LOAD segments; no dynamic
 * loader, no libc and no host runtime participate. */
ENTRY(_start)

/* Platform-instance region addresses.  They are also used by the runtime and
 * entry code so a layout change cannot silently overlap the input image or the
 * stack. */
__tc_input_image = 0x41000000;
__tc_stack_top = 0x4f000000;
__tc_stack_bottom = __tc_stack_top - 0x01000000;

SECTIONS
{
  . = 0x40000000;
  .text : {
    __tc_code_start = .;
    *(.text._start)
    *(.text*)
    __tc_code_end = .;
  }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : {
    . = ALIGN(16);
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(16);
    __bss_end = .;
  }
  __tc_image_end = .;
  ASSERT(__tc_image_end <= __tc_input_image,
         "TC image overlaps the platform input image")
  ASSERT(__tc_input_image + 0x00100000 <= __tc_stack_bottom,
         "TC input image overlaps the stack region")
  ASSERT(__tc_stack_bottom <= __tc_stack_top,
         "TC stack region is empty")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
