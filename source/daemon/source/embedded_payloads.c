#ifndef ONIONHEN_SHELLUI_ELF
#define ONIONHEN_SHELLUI_ELF "../assets/shellui.elf"
#endif
#ifndef ONIONHEN_UTIL_ELF
#define ONIONHEN_UTIL_ELF "../../bin/util.elf"
#endif
#ifndef ONIONHEN_ELFLDR_ELF
#define ONIONHEN_ELFLDR_ELF "../../bin/onion_elfldr.elf"
#endif

 __asm__(


	".global shellui_elf_start\n"
	".type   shellui_elf_start, @object\n"
	".align  16\n"
	"shellui_elf_start:\n"
	    ".incbin \"" ONIONHEN_SHELLUI_ELF "\"\n"
	"shellui_elf_end:\n"
	    ".global shellui_elf_size\n"
	    ".type   shellui_elf_size, @object\n"
	    ".align  4\n"
	"shellui_prx_size:\n"
    	".int    shellui_elf_end - shellui_elf_start\n"

	/* Keep util in memory so the runtime supervisor can restart it without disk. */
	".global util_elf_start\n"
	".type   util_elf_start, @object\n"
	".align  16\n"
	"util_elf_start:\n"
		".incbin \"" ONIONHEN_UTIL_ELF "\"\n"
	"util_elf_end:\n"
		".global util_elf_size\n"
		".type   util_elf_size, @object\n"
		".align  4\n"
	"util_elf_size:\n"
		".int    util_elf_end - util_elf_start\n"

	/* Recovery root: daemon can relaunch private :9020 through external :9021. */
	".global onion_elfldr_elf_start\n"
	".type   onion_elfldr_elf_start, @object\n"
	".align  16\n"
	"onion_elfldr_elf_start:\n"
		".incbin \"" ONIONHEN_ELFLDR_ELF "\"\n"
	"onion_elfldr_elf_end:\n"
		".global onion_elfldr_elf_size\n"
		".type   onion_elfldr_elf_size, @object\n"
		".align  4\n"
	"onion_elfldr_elf_size:\n"
		".int    onion_elfldr_elf_end - onion_elfldr_elf_start\n"

);
