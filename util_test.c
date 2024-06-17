#include "util.h"
#ifdef TEST
#ifdef LIST_TEST
#include <stdio.h>

#define TEST_COUNT 128

void print_list(list_t list)
{
	for (int i = 0; i < list_count(list); i++)
		printf("%i%c", *(int*)list_get(list, i), i + 1 < list_count(list) ? ',' : '\n');
}

int main()
{
	list_t list = list_create(sizeof(int));
	printf("Stats before addition of %i elements: count: %llu, reserved: %llu\n", TEST_COUNT, list_count(list), list_reserved(list));
	for (int i = 0; i < TEST_COUNT; i++)
		list_add(list, &i);
	printf("Reserved after addition: count: %llu, reserved: %llu\n", list_count(list), list_reserved(list));
	print_list(list);
	int wrong_count = 0;
	for (int i = 0; i < TEST_COUNT; i++)
	{
		if (i != *(int*)list_get(list, i))
			wrong_count++;
	}
	printf("list_get test resulted in %i wrong matches.\n", wrong_count);
	
	list_splice(list, 0, 1);
	printf("Expect a removal of the first element (0-1):\n");
	print_list(list);

	list_splice(list, 0, 9);
	printf("Expect a removal of all one-digit numbers (1-10 -> 0-9):\n");
	print_list(list);

	list_splice(list, (int)list_search_primitive(list, 40), (int)list_search_primitive(list, 51));
	printf("Expect a removal of all numbers from 40-50:\n");
	print_list(list);

	list_splice(list, 0, list_count(list));
	printf("Expect a removal of all numbers:\n");
	print_list(list);

	return 0;
}
#endif
#endif