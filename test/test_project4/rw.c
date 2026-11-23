#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>

int main(int argc, char* argv[])
{
	assert(argc > 1);
	srand(clock());
	sys_move_cursor(2, 2);
	for (int i = 1; i < argc; i++)
	{
		uintptr_t addr = atol(argv[i]);
		volatile long *page = (volatile long *)addr;
		long expected = rand();
		*page = expected;
		long actual = *page;
		printf("0x%lx, %ld\n", addr, expected);
		if (actual != expected) {
			printf("Error! Read %ld\n", actual);
			return 1;
		}
	}
	//Only input address.
	//Achieving input r/w command is recommended but not required.
	printf("Success!\n");
	return 0;
}
