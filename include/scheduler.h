#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "task.h"
#include <pthread.h>
#include <stdbool.h>

#define MAX_PATH 256

/**
 * Structure to hold the task list and scheduler state
 */
typedef struct {
    Task *tasks;                // Array of tasks
    int task_count;             // Number of tasks
    int capacity;               // Capacity of tasks array
    char data_dir[MAX_PATH];    // Data directory
    char db_path[MAX_PATH];     // Database path
    pthread_t scheduler_thread; // Scheduler thread
    pthread_mutex_t lock;       // Mutex for thread safety
    int check_interval;         // Check interval in seconds
    bool running;               // Is the scheduler running
} Scheduler;

/**
 * Initialize the scheduler
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param data_dir Directory to store task data
 * @return true on success, false on failure
 */
bool scheduler_init(Scheduler *scheduler, const char *data_dir);

/**
 * Clean up and free resources used by the scheduler
 * 
 * @param scheduler Pointer to the scheduler structure
 */
void scheduler_cleanup(Scheduler *scheduler);

/**
 * Start the scheduler thread
 * 
 * @param scheduler Pointer to the scheduler structure
 * @return true on success, false on failure
 */
bool scheduler_start(Scheduler *scheduler);

/**
 * Stop the scheduler thread
 * 
 * @param scheduler Pointer to the scheduler structure
 * @return true on success, false on failure
 */
bool scheduler_stop(Scheduler *scheduler);

/**
 * Add a new task to the scheduler
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task Task to add
 * @return Task ID on success, -1 on failure
 */
int scheduler_add_task(Scheduler *scheduler, Task task);

/**
 * Remove a task from the scheduler
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task_id ID of the task to remove
 * @return true on success, false on failure
 */
bool scheduler_remove_task(Scheduler *scheduler, int task_id);

/**
 * Update an existing task
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task Updated task information
 * @return true on success, false on failure
 */
bool scheduler_update_task(Scheduler *scheduler, Task task);

/**
 * Find a task by ID
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task_id ID of the task to find
 * @return Pointer to the task if found, NULL otherwise
 */
Task* scheduler_get_task(Scheduler *scheduler, int task_id);

/**
 * Get a list of all tasks
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param count Pointer to store the number of tasks
 * @return Array of tasks
 */
Task* scheduler_get_all_tasks(Scheduler *scheduler, int *count);

/**
 * Execute a specific task immediately
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task_id ID of the task to execute
 * @return true on success, false on failure
 */
bool scheduler_execute_task(Scheduler *scheduler, int task_id);

/**
 * Sync tasks with database
 * 
 * @param scheduler Pointer to the scheduler structure
 * @return true on success, false on failure
 */
bool scheduler_sync(Scheduler *scheduler);

/**
 * Add a dependency between tasks
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task_id ID of the task to add dependency
 * @param dependency_id ID of the dependency task
 * @return true on success, false on failure
 */
bool scheduler_add_dependency(Scheduler *scheduler, int task_id, int dependency_id);

/**
 * Remove a dependency between tasks
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task_id ID of the task to remove dependency
 * @param dependency_id ID of the dependency task
 * @return true on success, false on failure
 */
bool scheduler_remove_dependency(Scheduler *scheduler, int task_id, int dependency_id);

/**
 * Set the task execution mode
 * 
 * @param scheduler Pointer to the scheduler structure
 * @param task_id ID of the task to set execution mode
 * @param mode Execution mode for the task
 * @param script_content Content of the script for EXEC_SCRIPT mode
 * @param ai_prompt AI prompt for EXEC_AI_DYNAMIC mode
 * @param system_metrics System metrics to monitor for EXEC_AI_DYNAMIC mode
 * @return true on success, false on failure
 */
bool scheduler_set_exec_mode(Scheduler *scheduler, int task_id, TaskExecMode mode, 
                           const char *script_content, const char *ai_prompt, const char *system_metrics);

#endif /* SCHEDULER_H */ 