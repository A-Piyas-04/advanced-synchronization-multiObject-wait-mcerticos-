#include <proc.h>
#include <stdio.h>
#include <syscall.h>
#include <x86.h>

/*
 * Default boot goes straight to wait_demo (see kern/init/init.c).
 * This program is still built and linked for courses that spawn elf_id 0
 * or switch back to an idle-style loop; it no longer spawns wait_demo here
 * to avoid duplicate tutorials when both run.
 */
int
main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	printf("[idle] Running (not used as default boot; spawn me to test).\n");

	while (1)
		yield();

	return 0;
}
