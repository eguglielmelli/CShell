#include "utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

int array_length(char** array)
{
	if(array == NULL)
	{
		perror("Cannot find length of NULL array");
		return -1;
	}

	int length = 0;

	while(array[length] != NULL)
	{
		length++;
	}

	return length;

}
