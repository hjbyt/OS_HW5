#include <stdio.h>
#include <unistd.h>
#include <time.h> // for time measurement
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <error.h>

//
// Macros
//

// Return number of elements in static array
#define ARRAY_LENGTH(array) (sizeof(array)/sizeof(array[0]))
// Exit with an error message
#define ERROR(...) error(EXIT_FAILURE, errno, __VA_ARGS__)
// Verify that a condition holds, else exit with an error.
#define VERIFY(condition, ...) if (!(condition)) ERROR(__VA_ARGS__)

//
// Constants
//

typedef int bool;
#define FALSE 0
#define TRUE 1

//
// Structs
//

typedef struct Matrix_t
{
	int n;
	int** rows;
} Matrix;

//
// Globals
//


Matrix matrix1;
Matrix matrix2;
Matrix* game_matrix;
Matrix* helper_matrix;

//
// Function Declarations
//

void create_matrix(Matrix* matrix, int n);
void destroy_matrix(Matrix* matrix);

//
// Implementation
//

int main(int argc, char** argv)
{
	if (argc != 3) {
		printf("Usage: ./gol <file> <number of steps>");
		return EXIT_FAILURE;
	}

	char* file_path = argv[1];
	errno = 0;
	int steps = strtol(argv[2], NULL, 0);
	VERIFY(errno == 0, "Invallid argument given as <steps>");

	return EXIT_SUCCESS;
}

void create_matrix(Matrix* matrix, int n)
{
	matrix->n = n;
	matrix->rows = (int**)malloc(sizeof(int*) * n);
	VERIFY(matrix->rows != NULL, "malloc failed");
	for (int i = 0; i < n; ++i)
	{
		matrix->rows[i] = (int*)malloc(sizeof(int) * n);
		VERIFY(matrix->rows[i] != NULL, "malloc failed");
	}
}

void destroy_matrix(Matrix* matrix)
{
	for (int i = 0; i < matrix->n; ++i)
	{
		free(matrix->rows[i]);
	}
	free(matrix->rows);
}
