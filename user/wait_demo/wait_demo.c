#include <stdio.h>
#include <syscall.h>
#include <types.h>

#define EVENT_TIMER    1
#define EVENT_KEYBOARD 2

#define TIMER_MS       3000

static void
print_header(void)
{
	printf("\n");
	printf("================================================================\n");
	printf("  Multi-object wait demonstration (kernel wait object API)\n");
	printf("================================================================\n");
	printf("  This process registers a timer source (%d ms) and a keyboard\n",
	       TIMER_MS);
	printf("  source on one wait object, then repeatedly calls wait.\n");
	printf("  Keyboard input is not echoed; ensure the QEMU window has\n");
	printf("  focus. After a keyboard wakeup, the next timer deadline is\n");
	printf("  measured from that point (full %d ms before the next timer\n",
	       TIMER_MS);
	printf("  event).\n");
	printf("================================================================\n\n");
}

static void
print_separator(void)
{
	printf("----------------------------------------------------------------\n");
}

static void
print_registration(int wid, int timer_sid, int kbd_sid)
{
	printf("Registration\n");
	print_separator();
	printf("  (1) wait object created.  id = %d\n", wid);
	printf("  (2) timer source added.   id = %d  (interval %d ms)\n",
	       timer_sid, TIMER_MS);
	printf("  (3) keyboard source added. id = %d\n", kbd_sid);
	printf("  (4) Entering wait loop.\n");
	print_separator();
	printf("\n");
}

static void
print_iteration_header(int n)
{
	printf("\n");
	printf("Wait iteration %d\n", n);
	print_separator();
	printf("  Blocking until one source becomes ready.\n");
	printf("  - Timer:   approximately %d s of elapsed tick time without a\n",
	       TIMER_MS / 1000);
	printf("             keyboard-delivered wakeup (see kernel log line when\n");
	printf("             the timer fires).\n");
	printf("  - Keyboard: any key press (not echoed).\n");
	print_separator();
}

static void
print_guidance(int n)
{
	if (n == 1) {
		printf("Suggested test: do not press any key for at least %d s to\n",
		       (TIMER_MS / 1000) + 1);
		printf("observe the timer source.\n");
	} else if (n == 2) {
		printf("Suggested test: press a key shortly after blocking begins.\n");
	} else {
		printf("Either wait for the timer interval or use the keyboard.\n");
	}
	printf("\n");
}

static void
print_result(int iteration, int timer_sid, int kbd_sid, int ev,
	     int *n_timer, int *n_kbd)
{
	printf("\n");
	if (ev == timer_sid) {
		printf("Result: TIMER source (id %d) satisfied the wait.\n", ev);
		printf("        The periodic interval (%d ms) elapsed; the kernel\n",
		       TIMER_MS);
		printf("        should also print a timer notification line above.\n");
		(*n_timer)++;
	} else if (ev == kbd_sid) {
		printf("Result: KEYBOARD source (id %d) satisfied the wait.\n", ev);
		(*n_kbd)++;
	} else {
		printf("Result: unexpected source id %d.\n", ev);
	}
	printf("\n");
	printf("Cumulative wakeups:  timer: %d   keyboard: %d   (iteration %d)\n",
	       *n_timer, *n_kbd, iteration);
	print_separator();
}

int
main(int argc, char **argv)
{
	int wid;
	int timer_sid;
	int kbd_sid;
	int ev;
	int iteration;
	int n_timer;
	int n_kbd;

	(void)argc;
	(void)argv;

	n_timer = 0;
	n_kbd = 0;
	iteration = 0;

	print_header();

	wid = sys_waitobj_create();
	if (wid < 0) {
		printf("Error: waitobj_create failed.\n");
		return 1;
	}

	timer_sid = sys_waitobj_add(wid, EVENT_TIMER, TIMER_MS);
	if (timer_sid < 0) {
		printf("Error: waitobj_add (timer) failed.\n");
		return 1;
	}

	kbd_sid = sys_waitobj_add(wid, EVENT_KEYBOARD, 0);
	if (kbd_sid < 0) {
		printf("Error: waitobj_add (keyboard) failed.\n");
		return 1;
	}

	print_registration(wid, timer_sid, kbd_sid);

	while (1) {
		iteration++;
		print_iteration_header(iteration);
		print_guidance(iteration);

		ev = sys_waitobj_wait(wid, 0);
		if (ev < 0) {
			printf("Error: wait failed; retrying.\n");
			continue;
		}

		print_result(iteration, timer_sid, kbd_sid, ev, &n_timer,
			     &n_kbd);
	}

	return 0;
}
