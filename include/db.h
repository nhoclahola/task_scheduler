#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <stdbool.h>
#include "task.h"

/**
 * Initialize the database
 * 
 * @param db_path Path to the database file
 * @return true on success, false on failure
 */
bool db_init(const char *db_path);

/**
 * Close the database connection
 */
void db_cleanup(void);

/**
 * Save a task to the database
 * 
 * @param task Pointer to the task to save
 * @return true on success, false on failure
 */
bool db_save_task(const Task *task);

/**
 * Update a task in the database
 * 
 * @param task Pointer to the task to update
 * @return true on success, false on failure
 */
bool db_update_task(const Task *task);

/**
 * Delete a task from the database
 * 
 * @param task_id ID of the task to delete
 * @return true on success, false on failure
 */
bool db_delete_task(int task_id);

/**
 * Load all tasks from the database
 * 
 * @param tasks Pointer to store the array of tasks
 * @param count Pointer to store the number of tasks
 * @return true on success, false on failure
 */
bool db_load_tasks(Task **tasks, int *count);

/**
 * Get a single task from the database
 * 
 * @param task_id ID of the task to retrieve
 * @param task Pointer to store the task
 * @return true if task found, false otherwise
 */
bool db_get_task(int task_id, Task *task);

/**
 * Get the next available task ID
 * 
 * @return Next available ID
 */
int db_get_next_id(void);

#endif /* DB_H */ 