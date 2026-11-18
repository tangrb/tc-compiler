/* TC 0.0.46 RISC-V 32 image layout for the QEMU virt machine.
 *
 * RAM starts at 0x80000000.  Code and read-only data occupy the first
 * profile code window, the language static region has a fixed 1 MiB window at
 * 0x81000000, and the runtime-private .bss follows.  The stack occupies the
 * top of the 8 MiB frame window.  No dynamic loader, libc or host runtime
 * participates.
 */
ENTRY(_start)

__tc_stack_top = 0x88000000;
__tc_stack_bottom = 0x87800000;

SECTIONS
{
  . = 0x80000000;
  .text : {
    __tc_code_start = .;
    *(.text._start)
    *(.text*)
    __tc_code_end = .;
  }
  .rodata : { *(.rodata*) }

  /* The emitter writes the static language region first in .data, so this
   * address is also __tc_static_region.  Code and rodata may grow to 16 MiB
   * below it. */
  . = 0x81000000;
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
  . = ALIGN(16);
  __global_pointer$ = . + 0x800;

  ASSERT(__tc_code_start == 0x80000000,
         "TC code must start at the RAM base")
  ASSERT(__tc_code_end <= 0x81000000,
         "TC code and rodata overflow the profile code window")
  ASSERT(__tc_static_region == 0x81000000,
         "TC static region must start at its profile base")
  ASSERT(__tc_static_region_end <= 0x81100000,
         "TC static data exceeds the profile static window")
  ASSERT(__tc_image_end <= __tc_stack_bottom,
         "TC image overlaps the stack region")
  ASSERT(__tc_stack_bottom < __tc_stack_top,
         "TC stack region is empty")
  /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
