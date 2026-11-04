/* Partial-link (ld -r) script for the linked-in cgame.
   Brackets its writable data so Sys_LoadGameDll can reset it on every reload. */
SECTIONS
{
  .data : {
    __cgame_data_start = .;
    *(.data .data.*)
    __cgame_data_end = .;
  }
  .bss : {
    __cgame_bss_start = .;
    *(.bss .bss.*)
    __cgame_bss_end = .;
  }
}
