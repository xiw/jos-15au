// Report system up time.

#include <inc/lib.h>

void
umain(int argc, char **argv)
{
	unsigned msec = sys_time_msec();

	printf("%u.%02u\n", msec / 1000, (msec % 1000) * 100 / 1000);
}
