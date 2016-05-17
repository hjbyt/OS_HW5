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
#include <pthread.h>

//
//TODO: - Debug crashes when running with multiple threads
//      - check on nova
//      - improve performance (?)
//

//
// Macros
//

// Exit with an error message
#define ERROR(...) error(EXIT_FAILURE, errno, __VA_ARGS__)
// Verify that a condition holds, else exit with an error.
#define VERIFY(condition, ...) if (!(condition)) ERROR(__VA_ARGS__)
// Verify that a pthread operation succeeds, else exit with an error.
#define PCHECK(pthread_operation, ...)            \
	do {                                          \
		int _r = (pthread_operation);             \
		if (_r != 0) {                            \
			error(EXIT_FAILURE, _r, __VA_ARGS__); \
		}                                         \
	} while(0)

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

typedef struct Task_t
{
	int x;
	int y;
	int dx;
	int dy;
	struct Task_t* next;
} Task;

typedef struct TaskQueue_t
{
	Task* first;
	Task* last;
	bool is_empty;
	pthread_mutex_t mutex;
	pthread_cond_t not_empty_cond;
} TaskQueue;

//
// Globals
//


Matrix _matrix1;
Matrix _matrix2;
Matrix* game_matrix = &_matrix1;
Matrix* helper_matrix = &_matrix2;
TaskQueue tasks;
bool is_simulation_step_complete = FALSE;
bool should_worker_continue = TRUE;
pthread_cond_t simulation_step_complete_cond;
pthread_mutex_t simulation_step_mutex;
int completed_tasks_count = 0;
int matrix_size = 0;

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

void init_queue();
void uninit_queue();
void lock_queue();
void unlock_queue();
Task* create_task(int x, int y, int dx, int dy);
void destroy_task(Task* task);
void enqueue_task(Task* task);
Task* dequeue_task();
void* execute_tasks(void* arg);
bool execute_task(Task* task);

//
// Implementation
//

int main(int argc, char** argv)
{
	if (argc != 4) {
		printf("Usage: ./pgol <file> <steps> <threads>\n");
		return EXIT_FAILURE;
	}

	char* file_path = argv[1];
	errno = 0;
	int steps = strtol(argv[2], NULL, 0);
	VERIFY(errno == 0 && steps >= 0, "Invallid argument given as <steps>");
	int thread_count = strtol(argv[3], NULL, 0);
	VERIFY(errno == 0 && thread_count >= 1, "Invallid argument given as <threads>");

	load_matrix(game_matrix, file_path);
	create_matrix(helper_matrix, game_matrix->n);
	matrix_size = game_matrix->n * game_matrix->n;

	PCHECK(pthread_mutex_init(&simulation_step_mutex, NULL), "init mutex failed");
	PCHECK(pthread_cond_init(&simulation_step_complete_cond, NULL), "init condition variable failed");
	init_queue();

	pthread_t threads[thread_count];
	for (int i = 0; i < thread_count; ++i)
	{
		PCHECK(pthread_create(&threads[i], NULL, execute_tasks, NULL), "create thread failed");
	}

	unsigned long time_milliseconds = simulate(steps);
	//TODO: comment out ?
	printf("Simulated %d steps in %lu milliseconds using %d threads\n",
			steps, time_milliseconds, thread_count);


	// Signal the workers to finish
	// (if we wouldn't do this then we'd be unable to uninit_queue)
	should_worker_continue = FALSE;
	//TODO: can we use broadcast?
	PCHECK(pthread_cond_broadcast(&tasks.not_empty_cond), "condition broadcast failed");
	//TODO: XXX
//	for (int i = 0; i < thread_count; ++i)
//	{
//		PCHECK(pthread_cond_signal(&tasks.not_empty_cond), "condition signal failed");
//	}
	// Wait for them to actually finish
	for (int i = 0; i < thread_count; ++i)
	{
		PCHECK(pthread_join(threads[i], NULL), "thread join failed");
	}

	PCHECK(pthread_cond_destroy(&simulation_step_complete_cond), "destroy condition variable failed");
	PCHECK(pthread_mutex_destroy(&simulation_step_mutex), "destroy mutex failed");
	uninit_queue();

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
	is_simulation_step_complete = FALSE;
	completed_tasks_count = 0;
	Task* task = create_task(0, 0, game_matrix->n, game_matrix->n);
	lock_queue(); //TODO: remove ???
	enqueue_task(task);
	unlock_queue();

	// Wait for task simulation step complete signal
	PCHECK(pthread_mutex_lock(&simulation_step_mutex), "lock mutex failed");
	while (!is_simulation_step_complete)
	{
		PCHECK(pthread_cond_wait(&simulation_step_complete_cond, &simulation_step_mutex), "wait on condition variable failed");
	}
	PCHECK(pthread_mutex_unlock(&simulation_step_mutex), "unlock mutex failed");

	// Swap game and helper matrices
	Matrix* temp = game_matrix;
	game_matrix = helper_matrix;
	helper_matrix = temp;
}

void simulate_step_on_cell(Matrix* source, Matrix* dest, int x, int y)
{
	//TODO: synchronize writes to dest ??
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

void init_queue()
{
	tasks.first = NULL;
	tasks.last = NULL;
	tasks.is_empty = TRUE;
	PCHECK(pthread_mutex_init(&tasks.mutex, NULL), "init mutex failed");
	PCHECK(pthread_cond_init(&tasks.not_empty_cond, NULL), "init condition variable failed");
}

void uninit_queue()
{
	PCHECK(pthread_cond_destroy(&tasks.not_empty_cond), "destroy condition variable failed");
	PCHECK(pthread_mutex_destroy(&tasks.mutex), "destroy mutex failed");
}

void lock_queue()
{
	PCHECK(pthread_mutex_lock(&tasks.mutex), "lock mutex failed");
}

void unlock_queue()
{
	PCHECK(pthread_mutex_unlock(&tasks.mutex), "unlock mutex failed");
}

Task* create_task(int x, int y, int dx, int dy)
{
	Task* task = (Task*)malloc(sizeof(*task));
	VERIFY(task != NULL, "malloc task failed");
	task->x = x;
	task->y = y;
	task->dx = dx;
	task->dy = dy;
	task->next = NULL;
	return task;
}

void destroy_task(Task* task)
{
	free(task);
}

void enqueue_task(Task* task)
{
	task->next = NULL;
	if (tasks.last != NULL) {
		tasks.last->next = task;
	}
	tasks.last = task;
	if (tasks.first == NULL) {
		tasks.first = task;
		tasks.is_empty = FALSE;
		PCHECK(pthread_cond_signal(&tasks.not_empty_cond), "condition signal failed");
	}
}

Task* dequeue_task()
{
	if (tasks.is_empty) {
		fprintf(stderr, "Error, tried to dequeue from empty queue\n");
		exit(EXIT_FAILURE);
	}

	Task* task = tasks.first;
	tasks.first = tasks.first->next;
	if (task == tasks.last) {
		tasks.last = NULL;
		tasks.is_empty = TRUE;
	}
	task->next = NULL;
	return task;
}

void* execute_tasks(void* arg)
{
	while (TRUE)
	{
		lock_queue();
		while (tasks.is_empty && should_worker_continue)
		{
			PCHECK(pthread_cond_wait(&tasks.not_empty_cond, &tasks.mutex), "wait on condition variable failed");
		}
		if (!should_worker_continue) {
			unlock_queue();
			return NULL;
		}
		Task* task = dequeue_task();
		//TODO: should we use really unlock here,
		//      or should we unlock after creating sub-tasks?
		unlock_queue();

		bool simulated_cell = execute_task(task);
		destroy_task(task);
		if (simulated_cell) {
			int completed_tasks = __sync_add_and_fetch(&completed_tasks_count, 1);
			if (completed_tasks == matrix_size) {
				is_simulation_step_complete = TRUE;
				PCHECK(pthread_cond_signal(&simulation_step_complete_cond), "condition signal failed");
			}
		}
	}

	return NULL;
}

bool execute_task(Task* task)
{
	if (task->dx == 1 && task->dy == 1) {
		simulate_step_on_cell(game_matrix, helper_matrix, task->x, task->y);
		return TRUE;
	} else {
		int half_dx = task->dx / 2;
		int half_dy = task->dy / 2;
		assert(half_dx * 2 == task->dx);
		assert(half_dy * 2 == task->dy);
		Task* task1 = create_task(task->x          , task->y          , half_dx, half_dy);
		Task* task2 = create_task(task->x + half_dx, task->y          , half_dx, half_dy);
		Task* task3 = create_task(task->x          , task->y + half_dy, half_dx, half_dy);
		Task* task4 = create_task(task->x + half_dx, task->y + half_dy, half_dx, half_dy);
		lock_queue();
		enqueue_task(task1);
		enqueue_task(task2);
		enqueue_task(task3);
		enqueue_task(task4);
		unlock_queue();
		return FALSE;
	}
}
