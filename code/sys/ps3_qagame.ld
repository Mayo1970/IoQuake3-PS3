/* Partial-link (ld -r) script for the linked-in qagame.
   Brackets its writable data so Sys_LoadGameDll can reset it on every reload. */
SECTIONS
{
  .data : {
    __qagame_data_start = .;
    *(.data .data.*)
    __qagame_data_end = .;
  }
  .bss : {
    __qagame_bss_start = .;
    *(.bss .bss.*)
    __qagame_bss_end = .;
  }
}
