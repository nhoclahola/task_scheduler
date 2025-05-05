#include "../../include/scheduler.h"
#include "../../include/utils.h"
#include "../../include/db.h"
#include "../../include/ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>

#define INITIAL_CAPACITY 10
#define DB_FILENAME "tasks.db"
#define MAX_TASKS_TO_EXECUTE 100

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
    
    // Lưu trữ các giá trị quan trọng trước khi cập nhật
    time_t original_last_run_time = scheduler->tasks[index].last_run_time;
    int original_exit_code = scheduler->tasks[index].exit_code;
    
    // In thông tin debug nếu có last_run_time
    if (original_last_run_time > 0) {
        char time_str[64];
        struct tm *tm_info = localtime(&original_last_run_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
        log_message(LOG_INFO, "Preserving last_run_time during update: %s", time_str);
        log_message(LOG_INFO, "Preserving exit_code during update: %d", original_exit_code);
    }
    
    // Update the task
    scheduler->tasks[index] = task;
    
    // Đảm bảo giữ nguyên các giá trị lịch sử khi vô hiệu hóa task
    // Khi task bị vô hiệu hóa, giá trị last_run_time và exit_code có thể bị mất
    if (!task.enabled || (original_last_run_time > 0 && task.last_run_time == 0)) {
        scheduler->tasks[index].last_run_time = original_last_run_time;
        scheduler->tasks[index].exit_code = original_exit_code;
        log_message(LOG_INFO, "Restored historical data for task %d (last_run_time: %ld, exit_code: %d)", 
                   task.id, original_last_run_time, original_exit_code);
    }
    
    // Make sure next run time is calculated
    task_calculate_next_run(&scheduler->tasks[index]);
    
    // Tạo bản sao của task để gửi đến database
    Task db_task = scheduler->tasks[index];
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update in database
    if (!db_update_task(&db_task)) {
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
        log_message(LOG_ERROR, "Cannot execute task: Task ID %d not found", task_id);
        return false;
    }
    
    Task *task = &scheduler->tasks[index];
    
    // Double check task ID is correct
    if (task->id != task_id) {
        pthread_mutex_unlock(&scheduler->lock);
        log_message(LOG_ERROR, "Task ID mismatch in scheduler_execute_task: Expected %d, found %d", 
            task_id, task->id);
        return false;
    }
    
    // Check if the task is enabled
    if (!task->enabled) {
        pthread_mutex_unlock(&scheduler->lock);
        log_message(LOG_ERROR, "Cannot execute task %d: Task is disabled", task_id);
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
    } else if (task->exec_mode == EXEC_AI_DYNAMIC) {
        // Tải lại cấu hình mỗi lần để đảm bảo đọc API key mới nhất
        ai_load_config(NULL);
        
        // Lấy Deepseek API key từ cấu hình hoặc biến môi trường
        const char *api_key = ai_get_api_key();
        if (!api_key) {
            log_message(LOG_ERROR, "DeepSeek API key not found in config file or environment variable");
            log_message(LOG_INFO, "Please set your API key using 'taskscheduler set-api-key YOUR_API_KEY' command");
            exit_code = -1;
        } else {
            // Thu thập thông tin hệ thống
            SystemMetrics metrics;
            init_system_metrics(&metrics); // Khởi tạo metrics trước khi thu thập
            
            if (!collect_system_metrics(task->system_metrics, &metrics)) {
                log_message(LOG_ERROR, "Failed to collect system metrics for task %d", task_id);
                exit_code = -1;
            } else {
                // Sử dụng hàm AI mới để tạo lệnh từ AI
                char *command = ai_generate_command(&metrics, task->ai_prompt);
                
                if (!command || strlen(command) == 0) {
                    log_message(LOG_ERROR, "Failed to generate AI command for task %d", task_id);
                    if (command) free(command);
                    exit_code = -1;
                } else {
                    // Ghi log lệnh được tạo ra
                    log_message(LOG_INFO, "AI generated command for task %d: %s", task_id, command);
                    
                    // Kiểm tra và cải thiện lệnh để đảm bảo thông báo hoạt động
                    if (strstr(command, "notify-send") != NULL) {
                        // Lệnh này đã có notify-send từ AI - thêm wrapper đơn giản
                        // để tự động xử lý trường hợp không có notify-send
                        char *modified_command = malloc(strlen(command) * 2 + 256);
                        
                        if (modified_command) {
                            // Thêm kiểm tra notify-send với fallback echo đơn giản
                            sprintf(modified_command, 
                                "if command -v notify-send >/dev/null 2>&1; then %s; else echo \"NOTIFY: Task %d (%s) notification\"; fi",
                                command, task_id, task->name);
                            
                            log_message(LOG_INFO, "Added notify-send compatibility wrapper for task %d", task_id);
                            
                            // Thực thi lệnh đã sửa đổi trực tiếp
                            run_command_with_timeout(
                                modified_command,
                                task->working_dir[0] ? task->working_dir : NULL,
                                task->max_runtime,
                                &exit_code
                            );
                            
                            // Force exit code to 0 for notification commands
                            exit_code = 0;
                            free(modified_command);
                        } else {
                            // Nếu không thể cấp phát bộ nhớ, thực thi lệnh gốc
                            log_message(LOG_WARNING, "Could not create wrapper, executing original command");
                            run_command_with_timeout(
                                command,
                                task->working_dir[0] ? task->working_dir : NULL,
                                task->max_runtime,
                                &exit_code
                            );
                        }
                    } else {
                        // Thực thi lệnh gốc nếu không có notify-send
                        run_command_with_timeout(
                            command,
                            task->working_dir[0] ? task->working_dir : NULL,
                            task->max_runtime,
                            &exit_code
                        );
                    }
                    
                    // Cập nhật trạng thái thực thi vào task
                    task_mark_executed(task, exit_code);
                    
                    // Giải phóng lệnh
                    free(command);
                }
            }
        }
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
    
    // Log execution result
    if (exit_code == 0) {
        log_message(LOG_INFO, "Task executed: ID=%d, Name=%s, Exit code=%d", 
                 task->id, task->name, task->exit_code);
        result = true;
    } else {
        log_message(LOG_INFO, "Task executed with errors: ID=%d, Name=%s, Exit code=%d", 
                 task->id, task->name, task->exit_code);
        result = false;
    }
    
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
    Scheduler *scheduler = (Scheduler *)arg;
    time_t current_time;
    
    // Create structure to store task execution information
    typedef struct {
        Task task;           // Copy of the task
        int original_index;  // Original position in task list
        int task_id;         // Store task ID explicitly for validation
    } TaskExecutionInfo;
    
    TaskExecutionInfo tasks_to_execute[MAX_TASKS_TO_EXECUTE];
    int num_tasks_to_execute;
    
    log_message(LOG_INFO, "Scheduler thread started.");
    
    while (scheduler->running) {
        num_tasks_to_execute = 0;
        current_time = time(NULL);
        
        // Debug log current time
        char time_buffer[64];
        time_to_string(current_time, time_buffer, sizeof(time_buffer), NULL);
        log_message(LOG_DEBUG, "Scheduler checking tasks at %s", time_buffer);
        
        // Lock mutex before accessing task list
        pthread_mutex_lock(&scheduler->lock);
        
        // Check tasks for execution
        for (int i = 0; i < scheduler->task_count; i++) {
            Task *task = &scheduler->tasks[i];
            
            // Debug log task info
            if (task->enabled) {
                char next_run_buffer[64];
                time_to_string(task->next_run_time, next_run_buffer, sizeof(next_run_buffer), NULL);
                char last_run_buffer[64];
                if (task->last_run_time > 0) {
                    time_to_string(task->last_run_time, last_run_buffer, sizeof(last_run_buffer), NULL);
                } else {
                    strcpy(last_run_buffer, "Never");
                }
                
                log_message(LOG_DEBUG, "Task %d (%s): enabled=%d, schedule_type=%d, next_run=%s, last_run=%s", 
                    task->id, task->name, task->enabled, task->schedule_type, next_run_buffer, last_run_buffer);
            }
            
            // Skip tasks that are disabled or one-time tasks already executed or manual tasks
            if (!task->enabled || 
                (task->last_run_time > 0 && task->frequency == ONCE && task->schedule_type != SCHEDULE_INTERVAL && task->schedule_type != SCHEDULE_CRON) ||
                (task->schedule_type == SCHEDULE_MANUAL)) {
                
                // Log reason for skipping
                if (!task->enabled) {
                    log_message(LOG_DEBUG, "Task %d (%s): Skipped - disabled", task->id, task->name);
                } else if (task->last_run_time > 0 && task->frequency == ONCE && task->schedule_type != SCHEDULE_INTERVAL && task->schedule_type != SCHEDULE_CRON) {
                    log_message(LOG_DEBUG, "Task %d (%s): Skipped - one-time task already executed", task->id, task->name);
                } else if (task->schedule_type == SCHEDULE_MANUAL) {
                    log_message(LOG_DEBUG, "Task %d (%s): Skipped - manual schedule", task->id, task->name);
                }
                
                continue;
            }
            
            // Check if task is due to run
            if (task->next_run_time <= current_time) {
                log_message(LOG_DEBUG, "Task %d (%s): Due for execution (next_run=%ld, current=%ld)", 
                    task->id, task->name, task->next_run_time, current_time);
                
                // Check if dependencies are satisfied
                if (check_dependencies_satisfied(scheduler, task)) {
                    log_message(LOG_INFO, "Task ID %d (%s) is due and dependencies are satisfied.", task->id, task->name);
                    
                    // Copy task to temporary array for execution after unlocking mutex
                    if (num_tasks_to_execute < MAX_TASKS_TO_EXECUTE) {
                        memcpy(&(tasks_to_execute[num_tasks_to_execute].task), task, sizeof(Task));
                        tasks_to_execute[num_tasks_to_execute].original_index = i;
                        tasks_to_execute[num_tasks_to_execute].task_id = task->id; // Store task ID explicitly
                        num_tasks_to_execute++;
                    } else {
                        log_message(LOG_WARNING, "Reached maximum number of tasks to execute.");
                        break;
                    }
                    
                    // Next run time will be updated in task_mark_executed after execution
                    // Log for debugging purposes
                    if (task->frequency != ONCE) {
                        log_message(LOG_DEBUG, "Task ID %d (%s) is recurring. Will be rescheduled after execution.", 
                                task->id, task->name);
                    }
                } else {
                    log_message(LOG_DEBUG, "Task ID %d (%s) is due but dependencies are not satisfied.", task->id, task->name);
                }
            } else {
                log_message(LOG_DEBUG, "Task %d (%s): Not due yet (next_run=%ld, current=%ld, diff=%ld seconds)", 
                    task->id, task->name, task->next_run_time, current_time, task->next_run_time - current_time);
            }
        }
        
        // Unlock mutex after copying necessary tasks
        pthread_mutex_unlock(&scheduler->lock);
        
        // Execute copied tasks after unlocking mutex
        for (int i = 0; i < num_tasks_to_execute; i++) {
            Task *task = &(tasks_to_execute[i].task);
            int task_id = task->id;  // Store task ID for later lookup
            
            // Verify task_id matches what we stored
            if (task_id != tasks_to_execute[i].task_id) {
                log_message(LOG_ERROR, "Task ID mismatch detected: %d vs %d - skipping execution", 
                    task_id, tasks_to_execute[i].task_id);
                continue;
            }
            
            int exit_code = 0;
            
            log_message(LOG_INFO, "Executing task ID %d: %s", task_id, task->name);
            
            // Execute task based on mode
            if (task->exec_mode == EXEC_SCRIPT) {
                // Create temporary script and execute
                char temp_path[512];
                if (task_prepare_script(task, temp_path, sizeof(temp_path))) {
                    run_command_with_timeout(
                        temp_path,
                        task->working_dir[0] ? task->working_dir : NULL,
                        task->max_runtime,
                        &exit_code
                    );
                    
                    // Delete temporary file after execution
                    unlink(temp_path);
                } else {
                    log_message(LOG_ERROR, "Failed to prepare script for task %d", task_id);
                    exit_code = -1;
                }
                
                // Update task's execution status
                task_mark_executed(task, exit_code);
                
            } else if (task->exec_mode == EXEC_AI_DYNAMIC) {
                // Reload configuration each time to ensure latest API key
                ai_load_config(NULL);
                
                // Get Deepseek API key from config or environment
                const char *api_key = ai_get_api_key();
                if (!api_key) {
                    log_message(LOG_ERROR, "DeepSeek API key not found in config file or environment variable");
                    log_message(LOG_INFO, "Please set your API key using 'taskscheduler set-api-key YOUR_API_KEY' command");
                    exit_code = -1;
                } else {
                    // Collect system metrics
                    SystemMetrics metrics;
                    init_system_metrics(&metrics); // Initialize metrics before collection
                    
                    if (!collect_system_metrics(task->system_metrics, &metrics)) {
                        log_message(LOG_ERROR, "Failed to collect system metrics for task %d", task_id);
                        exit_code = -1;
                    } else {
                        // Use AI function to generate command
                        char *command = ai_generate_command(&metrics, task->ai_prompt);
                        
                        if (!command || strlen(command) == 0) {
                            log_message(LOG_ERROR, "Failed to generate AI command for task %d", task_id);
                            if (command) free(command);
                            exit_code = -1;
                        } else {
                            // Ghi log lệnh được tạo ra
                            log_message(LOG_INFO, "AI generated command for task %d: %s", task_id, command);
                            
                            // Kiểm tra và cải thiện lệnh để đảm bảo thông báo hoạt động
                            if (strstr(command, "notify-send") != NULL) {
                                // Lệnh này đã có notify-send từ AI - thêm wrapper đơn giản
                                // để tự động xử lý trường hợp không có notify-send
                                char *modified_command = malloc(strlen(command) * 2 + 256);
                                
                                if (modified_command) {
                                    // Thêm kiểm tra notify-send với fallback echo đơn giản
                                    sprintf(modified_command, 
                                        "if command -v notify-send >/dev/null 2>&1; then %s; else echo \"NOTIFY: Task %d (%s) notification\"; fi",
                                        command, task_id, task->name);
                                    
                                    log_message(LOG_INFO, "Added notify-send compatibility wrapper for task %d", task_id);
                                    
                                    // Thực thi lệnh đã sửa đổi trực tiếp
                                    run_command_with_timeout(
                                        modified_command,
                                        task->working_dir[0] ? task->working_dir : NULL,
                                        task->max_runtime,
                                        &exit_code
                                    );
                                    
                                    // Force exit code to 0 for notification commands
                                    exit_code = 0;
                                    free(modified_command);
                                } else {
                                    // Nếu không thể cấp phát bộ nhớ, thực thi lệnh gốc
                                    log_message(LOG_WARNING, "Could not create wrapper, executing original command");
                                    run_command_with_timeout(
                                        command,
                                        task->working_dir[0] ? task->working_dir : NULL,
                                        task->max_runtime,
                                        &exit_code
                                    );
                                }
                            } else {
                                // Thực thi lệnh gốc nếu không có notify-send
                                run_command_with_timeout(
                                    command,
                                    task->working_dir[0] ? task->working_dir : NULL,
                                    task->max_runtime,
                                    &exit_code
                                );
                            }
                            
                            // Cập nhật trạng thái thực thi vào task
                            task_mark_executed(task, exit_code);
                            
                            // Giải phóng lệnh
                            free(command);
                        }
                    }
                }
            } else {
                // Execute shell command
                run_command_with_timeout(
                    task->command,
                    task->working_dir[0] ? task->working_dir : NULL,
                    task->max_runtime,
                    &exit_code
                );
            }
            
            // Find task again in main list (pointer may have changed)
            pthread_mutex_lock(&scheduler->lock);
            
            int task_index = find_task_index(scheduler, task_id);
            if (task_index >= 0) {
                Task *original_task = &scheduler->tasks[task_index];
                
                // Verify task ID matches again
                if (original_task->id != task_id) {
                    log_message(LOG_ERROR, "Task ID mismatch after execution in thread: Expected %d, found %d", 
                                task_id, original_task->id);
                    pthread_mutex_unlock(&scheduler->lock);
                    continue;
                }
                
                // For AI mode, the task_mark_executed was already called, so we just update based on exec_mode
                if (task->exec_mode != EXEC_AI_DYNAMIC) {
                    // Update execution status
                    task_mark_executed(original_task, exit_code);
                } else {
                    // Copy execution status from our temporary task if it's AI mode
                    original_task->last_run_time = task->last_run_time;
                    original_task->exit_code = task->exit_code;
                    
                    // Update next run time
                    task_calculate_next_run(original_task);
                }
                
                // Save to database
                pthread_mutex_unlock(&scheduler->lock); // Unlock mutex before DB operation
                db_update_task(original_task);
                
                // Log execution
                if (task->exit_code == 0) {
                    log_message(LOG_INFO, "Task executed successfully: ID=%d, Name=%s, Exit code=0",
                           original_task->id, original_task->name);
                } else {
                    log_message(LOG_INFO, "Task executed with errors: ID=%d, Name=%s, Exit code=%d",
                           original_task->id, original_task->name, original_task->exit_code);
                }
                
                // Relock for next iteration
                pthread_mutex_lock(&scheduler->lock);
            } else {
                log_message(LOG_WARNING, "Task not found after execution: ID=%d", task_id);
            }
            
            pthread_mutex_unlock(&scheduler->lock);
        }
        
        // Sleep before checking again
        sleep(scheduler->check_interval);
    }
    
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
    for (int i = 0; i < scheduler->task_count; i++) {
        if (scheduler->tasks[i].id == task_id) {
            return i;
        }
    }
    return -1;
}

// Helper function to check if dependencies are satisfied
static bool check_dependencies_satisfied(Scheduler *scheduler, const Task *task) {
    // If no dependencies, default to satisfied
    if (task->dependency_count == 0) {
        return true;
    }
    
    // Check each dependency
    for (int i = 0; i < task->dependency_count; i++) {
        int dep_id = task->dependencies[i];
        bool found = false;
        
        // Find dependent task in the list
        for (int j = 0; j < scheduler->task_count; j++) {
            if (scheduler->tasks[j].id == dep_id) {
                found = true;
                
                // Check dependency status based on defined behavior
                switch (task->dep_behavior) {
                    case DEP_ANY_SUCCESS:
                        if (scheduler->tasks[j].last_run_time > 0 && scheduler->tasks[j].exit_code == 0) {
                            return true; // At least one task succeeded
                        }
                        break;
                        
                    case DEP_ALL_SUCCESS:
                        if (scheduler->tasks[j].last_run_time <= 0 || scheduler->tasks[j].exit_code != 0) {
                            return false; // Need all tasks to succeed
                        }
                        break;
                        
                    case DEP_ANY_COMPLETION:
                        if (scheduler->tasks[j].last_run_time > 0) {
                            return true; // At least one task completed
                        }
                        break;
                        
                    case DEP_ALL_COMPLETION:
                        if (scheduler->tasks[j].last_run_time <= 0) {
                            return false; // Need all tasks to complete
                        }
                        break;
                        
                    default:
                        return false; // Undefined behavior
                }
                
                break;
            }
        }
        
        // If dependent task not found, dependency not satisfied
        if (!found) {
            return false;
        }
    }
    
    // If DEP_ALL_SUCCESS or DEP_ALL_COMPLETION and all checked, return true
    if (task->dep_behavior == DEP_ALL_SUCCESS || task->dep_behavior == DEP_ALL_COMPLETION) {
        return true;
    }
    
    // For DEP_ANY_SUCCESS or DEP_ANY_COMPLETION, if we get here, no task satisfied the condition
    return false;
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
bool scheduler_set_exec_mode(Scheduler *scheduler, int task_id, TaskExecMode mode, 
                            const char *script_content, const char *ai_prompt, const char *system_metrics) {
    if (!scheduler) {
        return false;
    }
    
    pthread_mutex_lock(&scheduler->lock);
    
    int index = find_task_index(scheduler, task_id);
    if (index < 0) {
        pthread_mutex_unlock(&scheduler->lock);
        return false;
    }
    
    Task *task = &scheduler->tasks[index];
    
    // Set the new execution mode
    task->exec_mode = mode;
    
    // Clear script content and AI-related fields
    task->script_content[0] = '\0';
    task->ai_prompt[0] = '\0';
    task->system_metrics[0] = '\0';
    
    // Set the appropriate content based on mode
    if (mode == EXEC_SCRIPT && script_content) {
        safe_strcpy(task->script_content, script_content, sizeof(task->script_content));
    } else if (mode == EXEC_AI_DYNAMIC) {
        if (ai_prompt) {
            safe_strcpy(task->ai_prompt, ai_prompt, sizeof(task->ai_prompt));
        }
        if (system_metrics) {
            safe_strcpy(task->system_metrics, system_metrics, sizeof(task->system_metrics));
        }
    }
    
    pthread_mutex_unlock(&scheduler->lock);
    
    // Update the task in the database
    if (!db_update_task(task)) {
        log_message(LOG_ERROR, "Failed to update task in database after changing execution mode");
        return false;
    }
    
    log_message(LOG_INFO, "Changed execution mode of task %d to %d", task_id, mode);
    return true;
} 
