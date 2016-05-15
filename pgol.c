#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
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
	int** cols;
} Matrix;

//
// Globals
//


Matrix _matrix1;
Matrix _matrix2;
Matrix* game_matrix = &_matrix1;
Matrix* helper_matrix = &_matrix2;

//
// Function Declarations
//

unsigned long simulate(int steps);
void simulate_step();
void simulate_step_on_cell(Matrix* source, Matrix* dest, int x, int y);
int count_alive_neighbors(Matrix* matrix, int x, int y);
bool is_alive(Matrix* matrix, int x, int y);
void load_matrix(Matrix* matrix, char* file_path);
void print_matrix(Matrix* matrix);
void save_matrix(Matrix* matrix, char* file_path);
void create_matrix(Matrix* matrix, int n);
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
	create_matrix(helper_matrix, game_matrix->n);

	unsigned long time_milliseconds = simulate(steps);
	//TODO: comment out ?
	printf("Simulated %d steps in %lu milliseconds\n", steps, time_milliseconds);

	//TODO: comment out
	print_matrix(game_matrix);
	//save_matrix(game_matrix, "result.bin");

	destroy_matrix(helper_matrix);
	destroy_matrix(game_matrix);

	return EXIT_SUCCESS;
}

unsigned long simulate(int steps)
{
	assert(game_matrix != NULL);
	assert(helper_matrix != NULL);
	assert(game_matrix != helper_matrix);
	assert(game_matrix->n == helper_matrix->n);

	// Start time measurement
	struct timeval start, end, diff;
	VERIFY(gettimeofday(&start, NULL) == 0, "Error getting time");

	for (int i = 0; i < steps; ++i)
	{
		simulate_step();
	}

	// End time measurement
	VERIFY(gettimeofday(&end, NULL) == 0, "Error getting time");

	// Return measurement
	timersub(&end, &start, &diff);
	unsigned long diff_useconds = 1000000 * diff.tv_sec + diff.tv_usec;
	unsigned long diff_milliseconds = diff_useconds / 1000;
	return diff_milliseconds;
}

void simulate_step()
{
	for (int x = 0; x < game_matrix->n; ++x)
	{
		for (int y = 0; y < game_matrix->n; ++y)
		{
			simulate_step_on_cell(game_matrix, helper_matrix, x, y);
		}
	}

	// Swap game and helper matrices
	Matrix* temp = game_matrix;
	game_matrix = helper_matrix;
	helper_matrix = temp;
}

void simulate_step_on_cell(Matrix* source, Matrix* dest, int x, int y)
{
	int alive_neighbors = count_alive_neighbors(source, x, y);
	if (is_alive(source, x, y)) {
		if (alive_neighbors < 2 || alive_neighbors > 3) {
			// Kill cell
			dest->cols[x][y] = 0;
		} else {
			// Keep alive
			dest->cols[x][y] = 1;
		}
	} else /* Cell is dead */ {
		if (alive_neighbors == 3) {
			// Revive cell
			dest->cols[x][y] = 1;
		} else {
			// Keep dead
			dest->cols[x][y] = 0;
		}
	}
}

int count_alive_neighbors(Matrix* matrix, int x, int y)
{
	int alive_neighbors = 0;
	for (int i = x - 1; i <= x + 1; ++i)
	{
		for (int j = y - 1; j <= y + 1; ++j)
		{
			if (i < 0 || i >= matrix->n
					|| j < 0 || j >= matrix->n
					|| (i == x && j == y)) {
				continue;
			}
			if (is_alive(matrix, i, j)) {
				alive_neighbors += 1;
			}
		}
	}
	return alive_neighbors;
}

bool is_alive(Matrix* matrix, int x, int y)
{
	return matrix->cols[x][y] == 1;
}

void load_matrix(Matrix* matrix, char* file_path)
{
	int fd = open(file_path, O_RDONLY);
	VERIFY(fd != -1, "open input file failed");

	struct stat file_stat;
	VERIFY(fstat(fd, &file_stat) == 0, "fstat on input file failed");

	int n = sqrt_(file_stat.st_size);
	VERIFY(n * n == file_stat.st_size || !is_power_of_2(n), "input file length is not a power of 4");

	create_matrix(matrix, n);

	//TODO: optimize to read more than 1 byte at a time.
	for (int x = 0; x < n; ++x)
	{
		for (int y = 0; y < n; ++y)
		{
			char c;
			VERIFY(read(fd, &c, 1) == 1, "read from input failed");
			matrix->cols[x][y] = c == '\0' ? 0 : 1;
		}
	}
}

void print_matrix(Matrix* matrix)
{
	//TODO: optimize (?)
	for (int x = 0; x < matrix->n; ++x)
	{
		for (int y = 0; y < matrix->n; ++y)
		{
			char c = (char)matrix->cols[x][y];
			c = c == 1 ? 'O' : '.';
			printf("%c", c);
		}
		printf("\n");
	}
}

void save_matrix(Matrix* matrix, char* file_path)
{
	int fd = creat(file_path, 0666);
	VERIFY(fd != -1, "open output file failed");

	//TODO: optimize (?)
	for (int x = 0; x < matrix->n; ++x)
	{
		for (int y = 0; y < matrix->n; ++y)
		{
			char c = (char)matrix->cols[x][y];
			VERIFY(write(fd, &c, 1) == 1, "write to output failed");
		}
	}
}

void create_matrix(Matrix* matrix, int n)
{
	matrix->n = n;
	matrix->cols = (int**)malloc(sizeof(int*) * n);
	VERIFY(matrix->cols != NULL, "malloc failed");
	for (int i = 0; i < n; ++i)
	{
		matrix->cols[i] = (int*)malloc(sizeof(int) * n);
		VERIFY(matrix->cols[i] != NULL, "malloc failed");
	}
}

void destroy_matrix(Matrix* matrix)
{
	for (int i = 0; i < matrix->n; ++i)
	{
		free(matrix->cols[i]);
	}
	free(matrix->cols);
}

int is_power_of_2 (unsigned int x)
{
	// Note: taken from www.exploringbinary.com/ten-ways-to-check-if-an-integer-is-a-power-of-two-in-c
	return ((x != 0) && ((x & (~x + 1)) == x));
}

// Note: taken from http:unsigned int/stackoverflow.com/a/1101217
// This is used instead of the standard sqrt(),
// because the standard math sqrt requires linking with libmath.
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