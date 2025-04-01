#include "../../include/scheduler.h"
#include "../../include/utils.h"
#include "../../include/db.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>

#define INITIAL_CAPACITY 10
#define DB_FILENAME "tasks.db"

// Thread function declaration
static void* scheduler_thread_func(void *arg);

// Helper functions
static bool scheduler_resize(Scheduler *scheduler, int new_capacity);
static int find_task_index(Scheduler *scheduler, int task_id);
static bool check_dependencies_satisfied(Scheduler *scheduler, const Task *task);
static bool execute_task_with_script(Scheduler *scheduler, Task *task);

bool scheduler_init(Scheduler *scheduler, const char *data_dir) {
    if (!scheduler || !data_dir) {
        return false;
    }
    
    // Initialize scheduler structure
    memset(scheduler, 0, sizeof(Scheduler));
    
    // Copy data directory path
    safe_strcpy(scheduler->data_dir, data_dir, sizeof(scheduler->data_dir));
    
    // Create data directory if it doesn't exist
    if (!ensure_directory_exists(data_dir)) {
        log_message(LOG_ERROR, "Failed to create data directory: %s", data_dir);
        return false;
    }
    
    // Initialize mutex
    if (pthread_mutex_init(&scheduler->lock, NULL) != 0) {
        log_message(LOG_ERROR, "Failed to initialize mutex");
        return false;
    }
    
    // Set database path
    snprintf(scheduler->db_path, sizeof(scheduler->db_path), "%s/%s", data_dir, DB_FILENAME);
    
    // Initialize database
    if (!db_init(scheduler->db_path)) {
        log_message(LOG_ERROR, "Failed to initialize database: %s", scheduler->db_path);
        pthread_mutex_destroy(&scheduler->lock);
        return false;
    }
    
    // Allocate initial task array
    scheduler->capacity = INITIAL_CAPACITY;
    scheduler->tasks = (Task*)malloc(sizeof(Task) * scheduler->capacity);
    if (!scheduler->tasks) {
        log_message(LOG_ERROR, "Failed to allocate memory for tasks");
        pthread_mutex_destroy(&scheduler->lock);
        return false;
    }
    scheduler->task_count = 0;
    
    // Set default check interval (1 second)
    scheduler->check_interval = 1;
    
    // We're not running yet
    scheduler->running = false;
    
    // Load tasks from database
    Task *tasks = NULL;
    int count = 0;
    
    if (db_load_tasks(&tasks, &count)) {
        if (count > 0) {
            // Make sure we have enough capacity
            if (count > scheduler->capacity) {
                if (!scheduler_resize(scheduler, count)) {
                    log_message(LOG_ERROR, "Failed to resize task array");
                    free(tasks);
                    pthread_mutex_destroy(&scheduler->lock);
                    return false;
                }
            }
            
            // Copy tasks to scheduler
            memcpy(scheduler->tasks, tasks, sizeof(Task) * count);
            scheduler->task_count = count;
            
            // Free temporary array
            free(tasks);
            
            log_message(LOG_INFO, "Loaded %d tasks from database", count);
        }
    } else {
        log_message(LOG_ERROR, "Failed to load tasks from database");
    }
    
    return true;
}

void scheduler_cleanup(Scheduler *scheduler) {
    if (!scheduler) {
        return;
    }
    
    // Stop the scheduler if it's running
    if (scheduler->running) {
        scheduler_stop(scheduler);
    }
    
    // Free resources
    pthread_mutex_destroy(&scheduler->lock);
    if (scheduler->tasks) {
        free(scheduler->tasks);
        scheduler->tasks = NULL;
    }
    
    scheduler->task_count = 0;
    scheduler->capacity = 0;
    
    // Clean up database
    db_cleanup();
}

bool scheduler_start(Scheduler *scheduler) {
    if (!scheduler) {
        return false;
    }
    
    // Do nothing if already running
    if (scheduler->running) {
        return true;
    }
    
    // Set running flag and create thread
    scheduler->running = true;
    if (pthread_create(&scheduler->scheduler_thread, NULL, scheduler_thread_func, scheduler) != 0) {
        log_message(LOG_ERROR, "Failed to create scheduler thread");
        scheduler->running = false;
        return false;
    }
    
    log_message(LOG_INFO, "Scheduler started");
    return true;
}

bool scheduler_stop(Scheduler *scheduler) {
    if (!scheduler || !scheduler->running) {
        return false;
    }
    
    // Set running flag to false
    scheduler->running = false;
    
    // Wait for the thread to finish
    pthread_join(scheduler->scheduler_thread, NULL);
    
    log_message(LOG_INFO, "Scheduler stopped");
    return true;
}

int scheduler_add_task(Scheduler *scheduler, Task task) {
    if (!scheduler) {
        return -1;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Resize if necessary
    if (scheduler->task_count >= scheduler->capacity) {
        if (!scheduler_resize(scheduler, scheduler->capacity * 2)) {
            pthread_mutex_unlock(&scheduler->lock);
            return -1;
        }
    }
    
    // Generate a new ID
    task.id = db_get_next_id();
    
    // Calculate next run time if not set
    if (task.next_run_time == 0) {
        task_calculate_next_run(&task);
    }
    
    // Add task to array
    scheduler->tasks[scheduler->task_count] = task;
    scheduler->task_count++;
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Save task to database
    if (!db_save_task(&task)) {
        log_message(LOG_ERROR, "Failed to save task to database");
        // Remove the task from memory since we couldn't save it
        pthread_mutex_lock(&scheduler->lock);
        int index = find_task_index(scheduler, task.id);
        if (index >= 0) {
            if (index < scheduler->task_count - 1) {
                scheduler->tasks[index] = scheduler->tasks[scheduler->task_count - 1];
            }
            scheduler->task_count--;
        }
        pthread_mutex_unlock(&scheduler->lock);
        return -1;
    }
    
    log_message(LOG_INFO, "Task added: ID=%d, Name=%s", task.id, task.name);
    return task.id;
}

bool scheduler_remove_task(Scheduler *scheduler, int task_id) {
    if (!scheduler) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task index
    int index = find_task_index(scheduler, task_id);
    if (index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        return false;
    }
    
    // Move the last task to this position (if it's not the last one)
    if (index < scheduler->task_count - 1) {
        scheduler->tasks[index] = scheduler->tasks[scheduler->task_count - 1];
    }
    
    // Decrease task count
    scheduler->task_count--;
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Delete from database
    if (!db_delete_task(task_id)) {
        log_message(LOG_ERROR, "Failed to delete task from database");
        return false;
    }
    
    log_message(LOG_INFO, "Task removed: ID=%d", task_id);
    return true;
}

bool scheduler_update_task(Scheduler *scheduler, Task task) {
    if (!scheduler) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task
    int index = find_task_index(scheduler, task.id);
    if (index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        return false;
    }
    
    // Update the task
    scheduler->tasks[index] = task;
    
    // Make sure next run time is calculated
    task_calculate_next_run(&scheduler->tasks[index]);
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update in database
    if (!db_update_task(&task)) {
        log_message(LOG_ERROR, "Failed to update task in database");
        return false;
    }
    
    log_message(LOG_INFO, "Task updated: ID=%d, Name=%s", task.id, task.name);
    return true;
}

Task* scheduler_get_task(Scheduler *scheduler, int task_id) {
    if (!scheduler) {
        return NULL;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task
    int index = find_task_index(scheduler, task_id);
    if (index < 0) {
        // Not found in memory, try the database
        pthread_mutex_unlock(&scheduler->lock);
        
        Task *task = malloc(sizeof(Task));
        if (!task) {
            return NULL;
        }
        
        if (!db_get_task(task_id, task)) {
            free(task);
            return NULL;
        }
        
        return task;
    }
    
    // Make a copy of the task
    Task *task = malloc(sizeof(Task));
    if (!task) {
        pthread_mutex_unlock(&scheduler->lock);
        return NULL;
    }
    
    *task = scheduler->tasks[index];
    
    pthread_mutex_unlock(&scheduler->lock);
    return task;
}

Task* scheduler_get_all_tasks(Scheduler *scheduler, int *count) {
    if (!scheduler || !count) {
        return NULL;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Set count
    *count = scheduler->task_count;
    
    if (scheduler->task_count == 0) {
        pthread_mutex_unlock(&scheduler->lock);
        
        // If no tasks in memory, try loading from database
        Task *tasks = NULL;
        int db_count = 0;
        
        if (db_load_tasks(&tasks, &db_count) && db_count > 0) {
            *count = db_count;
            return tasks;
        }
        
        return NULL;
    }
    
    // Allocate memory for task array
    Task *tasks = malloc(sizeof(Task) * scheduler->task_count);
    if (!tasks) {
        pthread_mutex_unlock(&scheduler->lock);
        *count = 0;
        return NULL;
    }
    
    // Copy all tasks
    memcpy(tasks, scheduler->tasks, sizeof(Task) * scheduler->task_count);
    
    pthread_mutex_unlock(&scheduler->lock);
    return tasks;
}

bool scheduler_execute_task(Scheduler *scheduler, int task_id) {
    if (!scheduler) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task
    int index = find_task_index(scheduler, task_id);
    if (index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        return false;
    }
    
    Task *task = &scheduler->tasks[index];
    
    // Check if the task is enabled
    if (!task->enabled) {
        pthread_mutex_unlock(&scheduler->lock);
        return false;
    }
    
    // Check if dependencies are satisfied for manual execution
    if (task->dependency_count > 0) {
        if (!check_dependencies_satisfied(scheduler, task)) {
            log_message(LOG_WARNING, "Cannot execute task %d: dependencies not satisfied", task_id);
            pthread_mutex_unlock(&scheduler->lock);
            return false;
        }
    }
    
    bool result = false;
    int exit_code = 0;
    
    // Execute based on mode
    if (task->exec_mode == EXEC_SCRIPT) {
        // Release the lock during script execution
        pthread_mutex_unlock(&scheduler->lock);
        
        result = execute_task_with_script(scheduler, task);
        
        // Reacquire the lock
        pthread_mutex_lock(&scheduler->lock);
    } else {
        // Execute the command
        result = run_command_with_timeout(
            task->command,
            task->working_dir[0] ? task->working_dir : NULL,
            task->max_runtime,
            &exit_code
        );
        
        // Update task execution status
        task_mark_executed(task, exit_code);
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update in database
    if (!db_update_task(task)) {
        log_message(LOG_ERROR, "Failed to update task in database after execution");
    }
    
    log_message(LOG_INFO, "Task executed: ID=%d, Name=%s, Exit code=%d", 
               task->id, task->name, task->exit_code);
    
    return result;
}

bool scheduler_sync(Scheduler *scheduler) {
    if (!scheduler) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    bool success = true;
    
    // Sync each task with the database
    for (int i = 0; i < scheduler->task_count; i++) {
        Task *task = &scheduler->tasks[i];
        
        // Check if the task exists in the database
        Task db_task;
        bool exists = db_get_task(task->id, &db_task);
        
        if (exists) {
            // Update if it exists
            if (!db_update_task(task)) {
                log_message(LOG_ERROR, "Failed to update task in database during sync: ID=%d", task->id);
                success = false;
            }
        } else {
            // Save if it doesn't exist
            if (!db_save_task(task)) {
                log_message(LOG_ERROR, "Failed to save task to database during sync: ID=%d", task->id);
                success = false;
            }
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    
    if (success) {
        log_message(LOG_INFO, "Scheduler synced with database");
    }
    
    return success;
}

// Thread function for the scheduler
static void* scheduler_thread_func(void *arg) {
    Scheduler *scheduler = (Scheduler*)arg;
    if (!scheduler) {
        return NULL;
    }
    
    log_message(LOG_INFO, "Scheduler thread started");
    
    while (scheduler->running) {
        time_t now = time(NULL);
        
        pthread_mutex_lock(&scheduler->lock);
        
        // Check for tasks that need to be executed
        for (int i = 0; i < scheduler->task_count; i++) {
            Task *task = &scheduler->tasks[i];
            
            if (task->enabled && task->next_run_time > 0 && task->next_run_time <= now) {
                // Check if dependencies are satisfied for scheduled execution
                if (task->dependency_count > 0) {
                    if (!check_dependencies_satisfied(scheduler, task)) {
                        log_message(LOG_INFO, "Skipping task %d: dependencies not satisfied", task->id);
                        
                        // Reschedule for later (add 5 minutes and try again)
                        task->next_run_time = now + 300;
                        continue;
                    }
                }
                
                // Release lock during execution to avoid blocking
                pthread_mutex_unlock(&scheduler->lock);
                
                log_message(LOG_INFO, "Running task: ID=%d, Name=%s", task->id, task->name);
                
                // Execute based on mode
                if (task->exec_mode == EXEC_SCRIPT) {
                    execute_task_with_script(scheduler, task);
                } else {
                    // Execute the command
                    int exit_code = 0;
                    run_command_with_timeout(
                        task->command,
                        task->working_dir[0] ? task->working_dir : NULL,
                        task->max_runtime,
                        &exit_code
                    );
                    
                    // Reacquire lock
                    pthread_mutex_lock(&scheduler->lock);
                    
                    // Check if the task still exists
                    if (i < scheduler->task_count && scheduler->tasks[i].id == task->id) {
                        // Update task execution status
                        task_mark_executed(&scheduler->tasks[i], exit_code);
                        
                        // Update in database
                        db_update_task(&scheduler->tasks[i]);
                        
                        log_message(LOG_INFO, "Task completed: ID=%d, Name=%s, Exit code=%d", 
                                  task->id, task->name, exit_code);
                    }
                    
                    // Continue to next iteration without unlocking (we already have the lock)
                    continue;
                }
                
                // Reacquire lock after script execution
                pthread_mutex_lock(&scheduler->lock);
            }
        }
        
        pthread_mutex_unlock(&scheduler->lock);
        
        // Sleep for check interval
        sleep(scheduler->check_interval);
    }
    
    log_message(LOG_INFO, "Scheduler thread stopped");
    return NULL;
}

// Helper function to resize the tasks array
static bool scheduler_resize(Scheduler *scheduler, int new_capacity) {
    if (!scheduler || new_capacity <= 0) {
        return false;
    }
    
    // Allocate new array
    Task *new_tasks = (Task*)realloc(scheduler->tasks, sizeof(Task) * new_capacity);
    if (!new_tasks) {
        log_message(LOG_ERROR, "Failed to resize tasks array");
        return false;
    }
    
    // Update scheduler
    scheduler->tasks = new_tasks;
    scheduler->capacity = new_capacity;
    
    return true;
}

// Helper function to find a task by ID
static int find_task_index(Scheduler *scheduler, int task_id) {
    if (!scheduler) {
        return -1;
    }
    
    for (int i = 0; i < scheduler->task_count; i++) {
        if (scheduler->tasks[i].id == task_id) {
            return i;
        }
    }
    
    return -1;
}

// Helper function to check if dependencies are satisfied
static bool check_dependencies_satisfied(Scheduler *scheduler, const Task *task) {
    if (!scheduler || !task) {
        return false;
    }
    
    // No dependencies? Always satisfied
    if (task->dependency_count == 0) {
        return true;
    }
    
    // Track success and completion counts
    int success_count = 0;
    int completion_count = 0;
    
    // Check each dependency
    for (int i = 0; i < task->dependency_count; i++) {
        int dep_id = task->dependencies[i];
        
        // Find the dependency
        int dep_idx = find_task_index(scheduler, dep_id);
        if (dep_idx < 0) {
            log_message(LOG_WARNING, "Dependency %d not found for task %d", dep_id, task->id);
            continue;
        }
        
        Task *dep_task = &scheduler->tasks[dep_idx];
        
        // Check if it has ever run
        if (dep_task->last_run_time > 0) {
            completion_count++;
            
            // Check success (exit code 0)
            if (dep_task->exit_code == 0) {
                success_count++;
            }
        }
    }
    
    // Check based on dependency behavior
    switch (task->dep_behavior) {
        case DEP_ANY_SUCCESS:
            return success_count > 0;
            
        case DEP_ALL_SUCCESS:
            return success_count == task->dependency_count;
            
        case DEP_ANY_COMPLETION:
            return completion_count > 0;
            
        case DEP_ALL_COMPLETION:
            return completion_count == task->dependency_count;
            
        default:
            return false;
    }
}

// Helper function to execute a task with script mode
static bool execute_task_with_script(Scheduler *scheduler, Task *task) {
    if (!scheduler || !task || task->exec_mode != EXEC_SCRIPT) {
        return false;
    }
    
    // Create a temporary script file
    char temp_script_path[512];
    if (!task_prepare_script(task, temp_script_path, sizeof(temp_script_path))) {
        log_message(LOG_ERROR, "Failed to prepare script for task %d", task->id);
        return false;
    }
    
    // Execute the script
    int exit_code = 0;
    bool result = run_command_with_timeout(
        temp_script_path,
        task->working_dir[0] ? task->working_dir : NULL,
        task->max_runtime,
        &exit_code
    );
    
    // Delete the temporary script file
    if (unlink(temp_script_path) != 0) {
        log_message(LOG_WARNING, "Failed to delete temporary script file: %s", temp_script_path);
    }
    
    // Reacquire the lock to update task status
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task again (it might have been removed)
    int idx = find_task_index(scheduler, task->id);
    if (idx >= 0) {
        // Update task execution status
        task_mark_executed(&scheduler->tasks[idx], exit_code);
        
        // Update in database
        db_update_task(&scheduler->tasks[idx]);
        
        log_message(LOG_INFO, "Script task completed: ID=%d, Name=%s, Exit code=%d", 
                  task->id, task->name, exit_code);
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    
    return result;
}

// Add a dependency between tasks
bool scheduler_add_dependency(Scheduler *scheduler, int task_id, int dependency_id) {
    if (!scheduler || task_id < 0 || dependency_id < 0) {
        return false;
    }
    
    // Avoid circular dependency
    if (task_id == dependency_id) {
        log_message(LOG_ERROR, "Cannot add dependency: Task cannot depend on itself");
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find both tasks
    int task_index = find_task_index(scheduler, task_id);
    int dep_index = find_task_index(scheduler, dependency_id);
    
    if (task_index < 0 || dep_index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        log_message(LOG_ERROR, "Cannot add dependency: One or both tasks not found");
        return false;
    }
    
    // Add the dependency
    Task *task = &scheduler->tasks[task_index];
    bool result = task_add_dependency(task, dependency_id);
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update in database if successful
    if (result) {
        if (!db_update_task(task)) {
            log_message(LOG_ERROR, "Failed to update task dependencies in database");
            return false;
        }
    }
    
    return result;
}

// Remove a dependency between tasks
bool scheduler_remove_dependency(Scheduler *scheduler, int task_id, int dependency_id) {
    if (!scheduler || task_id < 0 || dependency_id < 0) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task
    int task_index = find_task_index(scheduler, task_id);
    
    if (task_index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        log_message(LOG_ERROR, "Cannot remove dependency: Task not found");
        return false;
    }
    
    // Remove the dependency
    Task *task = &scheduler->tasks[task_index];
    bool result = task_remove_dependency(task, dependency_id);
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update in database if successful
    if (result) {
        if (!db_update_task(task)) {
            log_message(LOG_ERROR, "Failed to update task dependencies in database");
            return false;
        }
    }
    
    return result;
}

// Set the task execution mode
bool scheduler_set_exec_mode(Scheduler *scheduler, int task_id, TaskExecMode mode, const char *script_content) {
    if (!scheduler || task_id < 0) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    // Find the task
    int task_index = find_task_index(scheduler, task_id);
    
    if (task_index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        log_message(LOG_ERROR, "Cannot set execution mode: Task not found");
        return false;
    }
    
    // Update the task
    Task *task = &scheduler->tasks[task_index];
    task->exec_mode = mode;
    
    // If script mode, update script content
    if (mode == EXEC_SCRIPT && script_content) {
        safe_strcpy(task->script_content, script_content, sizeof(task->script_content));
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update in database
    if (!db_update_task(task)) {
        log_message(LOG_ERROR, "Failed to update task execution mode in database");
        return false;
    }
    
    log_message(LOG_INFO, "Set task %d execution mode to %s", 
               task_id, mode == EXEC_COMMAND ? "Command" : "Script");
    
    return true;
} 
