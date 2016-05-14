#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
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
	unsigned int n;
	int** rows;
} Matrix;

//
// Globals
//


Matrix matrix1;
Matrix matrix2;
Matrix* game_matrix = &matrix1;
Matrix* helper_matrix = &matrix2;

//
// Function Declarations
//

void load_matrix(Matrix* matrix, char* file_path);
void save_matrix(Matrix* matrix, char* file_path);
void create_matrix(Matrix* matrix, unsigned int n);
void destroy_matrix(Matrix* matrix);
unsigned int sqrt_(unsigned int n);
int is_power_of_2 (unsigned int x);

//
// Implementation
//

int main(int argc, char** argv)
{
	if (argc != 3) {
		printf("Usage: ./gol <file> <number of steps>\n");
		return EXIT_FAILURE;
	}

	char* file_path = argv[1];
	errno = 0;
	int steps = strtol(argv[2], NULL, 0);
	VERIFY(errno == 0 || steps < 0, "Invallid argument given as <steps>");

	load_matrix(game_matrix, file_path);

	//TODO XXX
	//save_matrix(game_matrix, "test_out.bin");

	destroy_matrix(game_matrix);

	return EXIT_SUCCESS;
}

void load_matrix(Matrix* matrix, char* file_path)
{
	int fd = open(file_path, O_RDONLY);
	VERIFY(fd != -1, "open input file failed");

	struct stat file_stat;
	VERIFY(fstat(fd, &file_stat) == 0, "fstat on input file failed");

	unsigned int n = sqrt_(file_stat.st_size);
	VERIFY(n * n == file_stat.st_size || !is_power_of_2(n), "input file length is not a power of 4");

	create_matrix(matrix, n);

	//TODO: optimize to read more than 1 byte at a time.
	for (unsigned x = 0; x < n; ++x)
	{
		for (unsigned y = 0; y < n; ++y)
		{
			char c;
			VERIFY(read(fd, &c, 1) == 1, "read from input failed");
			matrix->rows[x][y] = c == '\0' ? 0 : 1;
		}
	}
}

void save_matrix(Matrix* matrix, char* file_path)
{
	int fd = creat(file_path, 0666);
	VERIFY(fd != -1, "open output file failed");

	//TODO: optimize (?)
	for (unsigned x = 0; x < matrix->n; ++x)
	{
		for (unsigned y = 0; y < matrix->n; ++y)
		{
			char c = (char)matrix->rows[x][y];
			VERIFY(write(fd, &c, 1) == 1, "write to output failed");
		}
	}
}

void create_matrix(Matrix* matrix, unsigned int n)
{
	matrix->n = n;
	matrix->rows = (int**)malloc(sizeof(int*) * n);
	VERIFY(matrix->rows != NULL, "malloc failed");
	for (unsigned int i = 0; i < n; ++i)
	{
		matrix->rows[i] = (int*)malloc(sizeof(int) * n);
		VERIFY(matrix->rows[i] != NULL, "malloc failed");
	}
}

void destroy_matrix(Matrix* matrix)
{
	for (unsigned int i = 0; i < matrix->n; ++i)
	{
		free(matrix->rows[i]);
	}
	free(matrix->rows);
}

int is_power_of_2 (unsigned int x)
{
	// Note: taken from www.exploringbinary.com/ten-ways-to-check-if-an-integer-is-a-power-of-two-in-c
	return ((x != 0) && ((x & (~x + 1)) == x));
}

// Note: taken from http:unsigned int/stackoverflow.com/a/1101217
unsigned int sqrt_(unsigned int n)
{
	unsigned int op  = n;
	unsigned int res = 0;
	unsigned int one = 1uL << 30; // The second-to-top bit is set: use 1u << 14 for uint16_t type; use 1uL<<30 for uint32_t type


    // "one" starts at the highest power of four <= than the argument.
    while (one > op)
    {
        one >>= 2;
    }

    while (one != 0)
    {
        if (op >= res + one)
        {
            op = op - (res + one);
            res = res +  2 * one;
        }
        res >>= 1;
        one >>= 2;
    }
    return res;
}
