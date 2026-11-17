/* TC 0.0.46 ARM32 image layout for the QEMU MPS2 AN500 (Cortex-M7) board.
 *
 * The M-profile vector table must live at address 0 (SP, reset handler); the
 * language code and static region start above the vector page so address 0
 * stays reserved for `nullptr` (standard §13.2).  A single ELF PT_LOAD image
 * contains code, read-only data and the initialized static region; the
 * runtime arena and object tables are zero-initialized .bss.  No dynamic
 * loader, libc or host runtime participates.
 */
ENTRY(_start)

/* Platform-instance region addresses.  The stack grows down from the top and
 * the test harness injects the stdin image into the second SRAM bank. */
__tc_input_image = 0x20000000;
__tc_stack_top = 0x00400000;
__tc_stack_bottom = 0x00300000;

SECTIONS
{
  . = 0x00000000;
  .vectors : {
    KEEP(*(.vectors))
  }

  . = 0x00001000;
  .text : {
    __tc_code_start = .;
    *(.text._start)
    *(.text*)
    __tc_code_end = .;
  }
  .rodata : { *(.rodata*) }

  /* The emitter puts the static language region first in .data, so this
   * address is also __tc_static_region.  Code may grow to 1 MiB below it. */
  . = 0x00100000;
  .data : { *(.data*) }
  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    __bss_end = .;
  }
  __tc_image_end = .;

  ASSERT(__tc_code_start == 0x00001000,
         "TC code must start above the vector page")
  ASSERT(__tc_static_region == 0x00100000,
         "TC static region must start at its profile base")
  ASSERT(__tc_image_end <= __tc_stack_bottom,
         "TC image overlaps the stack region")
  ASSERT(__tc_stack_bottom < __tc_stack_top,
         "TC stack region is empty")
  ASSERT(__tc_input_image + 0x00100000 <= 0x20200000,
         "TC input image overlaps the platform heap")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
