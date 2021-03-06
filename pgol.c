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

#define KILO 1024
#define MEGA (KILO*KILO)

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
} Task;

#define TASKS_PER_BLOCK (MEGA/sizeof(Task))

typedef struct TaskQueue_t {
	Task** task_blocks;
	int blocks_count;
	int capacity;
	int first_task_index;
	int task_count;
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
int thread_count = 0;

//
// Function Declarations
//

unsigned long simulate(int steps);
void simulate_step();
void simulate_step_on_cell(Matrix* source, Matrix* dest, int x, int y);
int count_alive_neighbors(const Matrix* matrix, int x, int y);
bool is_alive(const Matrix* matrix, int x, int y);
void load_matrix(Matrix* matrix, char* file_path);
void print_matrix(const Matrix* matrix);
void save_matrix(const Matrix* matrix, char* file_path);
void create_matrix(Matrix* matrix, int n);
void destroy_matrix(Matrix* matrix);
unsigned int sqrt_(unsigned int n);
int is_power_of_2 (unsigned int x);

void init_queue(int max_size);
void uninit_queue();
void lock_queue();
void unlock_queue();
bool is_queue_empty();
Task* get_task(int index);
Task* first_task();
void enqueue_task(const Task* task);
void dequeue_task(Task* task);
void* execute_tasks(void* arg);
bool execute_task(const Task* task);

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
	thread_count = strtol(argv[3], NULL, 0);
	VERIFY(errno == 0 && thread_count >= 1, "Invallid argument given as <threads>");

	load_matrix(game_matrix, file_path);
	if (game_matrix->n == 0) {
		fprintf(stderr, "Error, input file is empty\n");
		exit(EXIT_FAILURE);
	}
	create_matrix(helper_matrix, game_matrix->n);
	matrix_size = game_matrix->n * game_matrix->n;

	PCHECK(pthread_mutex_init(&simulation_step_mutex, NULL), "init mutex failed");
	PCHECK(pthread_cond_init(&simulation_step_complete_cond, NULL), "init condition variable failed");
	init_queue(matrix_size);

	pthread_t threads[thread_count];
	int i;
	for (i = 0; i < thread_count; ++i)
	{
		PCHECK(pthread_create(&threads[i], NULL, execute_tasks, NULL), "create thread failed");
	}

	unsigned long time_milliseconds = simulate(steps);
	printf("Simulated %d steps in %lu milliseconds using %d threads\n",
			steps, time_milliseconds, thread_count);

	//print_matrix(game_matrix);
	//save_matrix(game_matrix, "result.bin");

	// Signal the workers to finish
	// (if we wouldn't do this then we'd be unable to uninit_queue)
	should_worker_continue = FALSE;
	//Note: pthread_cond_broadcast wasn't mentioned in the recitation,
	// so this code is disabled, and pthread_cond_signal is iterated instead.
	//
	//PCHECK(pthread_cond_broadcast(&tasks.not_empty_cond), "condition broadcast failed");
	for (i = 0; i < thread_count; ++i)
	{
		PCHECK(pthread_cond_signal(&tasks.not_empty_cond), "condition signal failed");
	}
	// Wait for them to actually finish
	for (i = 0; i < thread_count; ++i)
	{
		PCHECK(pthread_join(threads[i], NULL), "thread join failed");
	}

	PCHECK(pthread_cond_destroy(&simulation_step_complete_cond), "destroy condition variable failed");
	PCHECK(pthread_mutex_destroy(&simulation_step_mutex), "destroy mutex failed");
	uninit_queue();

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

	int i;
	for (i = 0; i < steps; ++i)
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
	Task task = {0, 0, game_matrix->n, game_matrix->n};
	if (thread_count == 1) {
		// Note: locking is necessary here in order to prevent a race such as this:
		// http://stackoverflow.com/questions/4544234/calling-pthread-cond-signal-without-locking-mutex
		lock_queue();
		enqueue_task(&task);
		unlock_queue();
	} else {
		// Since at most 1 thread can miss the signal, there is no need to lock
		// (see: http://moodle.tau.ac.il/mod/forum/discuss.php?d=65528)
		enqueue_task(&task);
	}


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

int count_alive_neighbors(const Matrix* matrix, int x, int y)
{
	int alive_neighbors = 0;
	int i, j;
	for (i = x - 1; i <= x + 1; ++i)
	{
		for (j = y - 1; j <= y + 1; ++j)
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

bool is_alive(const Matrix* matrix, int x, int y)
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

	char* buffer = (char*)malloc(MEGA);
	VERIFY(buffer != NULL, "malloc buffer failed");
	int x, y;
	int i = 0;
	int bytes_read = 0;
	for (x = 0; x < n; ++x)
	{
		for (y = 0; y < n; ++y)
		{
			if (i == bytes_read) {
				bytes_read = read(fd, buffer, MEGA);
				VERIFY(bytes_read != -1, "read from input failed");
				if (bytes_read == 0) {
					// Note: this shouldn't happen because we verified the file size.
					fprintf(stderr, "Error, input ended prematurely.\n");
					exit(EXIT_FAILURE);
				}
				i = 0;
			}
			matrix->cols[x][y] = buffer[i] == '\0' ? 0 : 1;
			i += 1;
		}
	}

	free(buffer);
	close(fd);
}

//Note: this is for debugging purposes only
void print_matrix(const Matrix* matrix)
{
	char* buffer = (char*)malloc(matrix->n + 2);
	VERIFY(buffer != NULL, "malloc buffer failed");
	int x, y;
	for (x = 0; x < matrix->n; ++x)
	{
		for (y = 0; y < matrix->n; ++y)
		{
			buffer[y] = matrix->cols[x][y] ? 'O' : '.';
		}
		buffer[matrix->n] = '\n';
		buffer[matrix->n + 1] = '\0';
		VERIFY(write(STDOUT_FILENO, buffer, matrix->n + 2), "print matrix failed");
	}
	free(buffer);
}

//Note: this is for debugging purposes only
void save_matrix(const Matrix* matrix, char* file_path)
{
	int fd = creat(file_path, 0666);
	VERIFY(fd != -1, "open output file failed");

	char* buffer = (char*)malloc(matrix->n);
	VERIFY(buffer != NULL, "malloc buffer failed");
	int x, y;
	for (x = 0; x < matrix->n; ++x)
	{
		for (y = 0; y < matrix->n; ++y)
		{
			buffer[y] = matrix->cols[x][y];
		}
		VERIFY(write(fd, buffer, matrix->n), "write to output failed");
	}
	free(buffer);

	close(fd);
}

void create_matrix(Matrix* matrix, int n)
{
	matrix->n = n;
	matrix->cols = (int**)malloc(sizeof(int*) * n);
	VERIFY(matrix->cols != NULL, "malloc failed");
	int i;
	for (i = 0; i < n; ++i)
	{
		matrix->cols[i] = (int*)malloc(sizeof(int) * n);
		VERIFY(matrix->cols[i] != NULL, "malloc failed");
	}
}

void destroy_matrix(Matrix* matrix)
{
	int i;
	for (i = 0; i < matrix->n; ++i)
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

void init_queue(int max_size)
{
	tasks.first_task_index = 0;
	tasks.blocks_count = (max_size + TASKS_PER_BLOCK - 1) / TASKS_PER_BLOCK;
	tasks.capacity = TASKS_PER_BLOCK * tasks.blocks_count;
	tasks.task_blocks = (Task**)malloc(sizeof(*tasks.task_blocks) * tasks.blocks_count);
	VERIFY(tasks.task_blocks != NULL, "malloc task block pointers failed");
	int i;
	for (i = 0; i < tasks.blocks_count; ++i) {
		tasks.task_blocks[i] = (Task*)malloc(sizeof(Task) * TASKS_PER_BLOCK);
		VERIFY(tasks.task_blocks[i] != NULL, "malloc task block failed");
	}
	PCHECK(pthread_mutex_init(&tasks.mutex, NULL), "init mutex failed");
	PCHECK(pthread_cond_init(&tasks.not_empty_cond, NULL), "init condition variable failed");
}

void uninit_queue()
{
	int i;
	for (i = 0; i < tasks.blocks_count; ++i) {
		free(tasks.task_blocks[i]);
	}
	free(tasks.task_blocks);
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

bool is_queue_empty()
{
	return tasks.task_count == 0;
}

Task* get_task(int index)
{
	int task_index = (tasks.first_task_index + index) % tasks.capacity;
	div_t q = div(task_index, TASKS_PER_BLOCK);
	return &tasks.task_blocks[q.quot][q.rem];
}

Task* first_task()
{
	return get_task(0);
}

void enqueue_task(const Task* task)
{
	if (tasks.task_count == tasks.capacity) {
		fprintf(stderr, "Error, tried to enqueue when the queue is full\n");
		exit(EXIT_FAILURE);
	}
	Task* new_task = get_task(tasks.task_count);
	*new_task = *task;

	tasks.task_count += 1;
	if (tasks.task_count == 1) {
		PCHECK(pthread_cond_signal(&tasks.not_empty_cond), "condition signal failed");
	}
}

void dequeue_task(Task* task)
{
	if (is_queue_empty()) {
		fprintf(stderr, "Error, tried to dequeue from empty queue\n");
		exit(EXIT_FAILURE);
	}

	*task = *first_task();
	tasks.first_task_index = (tasks.first_task_index + 1) % tasks.capacity;
	tasks.task_count -= 1;
}

void* execute_tasks(void* arg)
{
	while (TRUE)
	{
		lock_queue();
		while (is_queue_empty() && should_worker_continue)
		{
			PCHECK(pthread_cond_wait(&tasks.not_empty_cond, &tasks.mutex), "wait on condition variable failed");
		}
		if (!should_worker_continue) {
			unlock_queue();
			return NULL;
		}
		Task task;
		dequeue_task(&task);
		unlock_queue();

		bool simulated_cell = execute_task(&task);
		if (simulated_cell) {
			int completed_tasks = __sync_add_and_fetch(&completed_tasks_count, 1);
			if (completed_tasks == matrix_size) {
				is_simulation_step_complete = TRUE;
				// Note: locking is necessary here in order to prevent a race such as this:
				// http://stackoverflow.com/questions/4544234/calling-pthread-cond-signal-without-locking-mutex
				PCHECK(pthread_mutex_lock(&simulation_step_mutex), "lock mutex failed");
				PCHECK(pthread_cond_signal(&simulation_step_complete_cond), "condition signal failed");
				PCHECK(pthread_mutex_unlock(&simulation_step_mutex), "unlock mutex failed");
			}
		}
	}

	return NULL;
}

bool execute_task(const Task* task)
{
	if (task->dx == 1 && task->dy == 1) {
		simulate_step_on_cell(game_matrix, helper_matrix, task->x, task->y);
		return TRUE;
	} else {
		int half_dx = task->dx / 2;
		int half_dy = task->dy / 2;
		assert(half_dx * 2 == task->dx);
		assert(half_dy * 2 == task->dy);
		Task task1 = {task->x          , task->y          , half_dx, half_dy};
		Task task2 = {task->x + half_dx, task->y          , half_dx, half_dy};
		Task task3 = {task->x          , task->y + half_dy, half_dx, half_dy};
		Task task4 = {task->x + half_dx, task->y + half_dy, half_dx, half_dy};
		lock_queue();
		enqueue_task(&task1);
		enqueue_task(&task2);
		enqueue_task(&task3);
		enqueue_task(&task4);
		unlock_queue();
		return FALSE;
	}
}
