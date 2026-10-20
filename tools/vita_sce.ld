/* Reserve room for the import metadata appended by vita-elf-create. */
SECTIONS { . = ALIGN(0x1000) + 0x4000; } INSERT BEFORE .init_array;
