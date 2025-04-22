#include "../../include/task.h"
#include "../../include/utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

bool task_init(Task *task) {
    if (!task) {
        return false;
    }
    
    memset(task, 0, sizeof(Task));
    task->id = -1; // Will be set by the scheduler
    task->creation_time = time(NULL);
    task->last_run_time = 0;
    task->next_run_time = 0;
    task->frequency = ONCE;
    task->interval = 0;
    task->enabled = true;
    task->exit_code = 0;
    task->max_runtime = 0; // No limit
    task->exec_mode = EXEC_COMMAND; // Default to command execution
    task->dependency_count = 0;
    task->dep_behavior = DEP_ALL_SUCCESS; // Default behavior
    
    return true;
}

bool task_calculate_next_run(Task *task) {
    if (!task) {
        return false;
    }
    
    // Lưu giá trị last_run_time và exit_code ban đầu
    time_t original_last_run_time = task->last_run_time;
    int original_exit_code = task->exit_code;
    
    time_t now = time(NULL);
    struct tm *local_time;
    struct tm next_time;
    
    // Nếu task bị vô hiệu hóa, chỉ đặt next_run_time = 0
    // và giữ nguyên last_run_time và exit_code
    if (!task->enabled) {
        task->next_run_time = 0;
        // Đảm bảo giữ nguyên các giá trị
        task->last_run_time = original_last_run_time;
        task->exit_code = original_exit_code;
        return true;
    }
    
    // Check the schedule type
    switch (task->schedule_type) {
        case SCHEDULE_MANUAL:
            // Manually scheduled tasks don't get automatic next run times
            task->next_run_time = 0;
            // Khôi phục lại các giá trị
            task->last_run_time = original_last_run_time;
            task->exit_code = original_exit_code;
            return true;
            
        case SCHEDULE_INTERVAL:
            // For interval-based scheduling, add interval minutes to the last run time
            if (task->last_run_time > 0) {
                task->next_run_time = task->last_run_time + (task->interval * 60);
            } else {
                // First time? Schedule from now
                task->next_run_time = now + (task->interval * 60);
            }
            // Không khôi phục lại các giá trị cho SCHEDULE_INTERVAL
            return true;
            
        case SCHEDULE_CRON:
            // For cron-based scheduling, calculate next run using cron expression
            if (task->cron_expression[0] != '\0') {
                // Simple cron parsing for common patterns
                // Format: min hour day month weekday
                
                time_t next_time = now;
                
                // Handle pattern "* * * * *" (every minute)
                if (strcmp(task->cron_expression, "* * * * *") == 0) {
                    // Calculate next minute
                    next_time = now + 60;
                    task->next_run_time = next_time;
                    return true;
                }
                
                // Handle pattern "*/n * * * *" (every n minutes)
                if (strncmp(task->cron_expression, "*/", 2) == 0 && 
                    strchr(task->cron_expression, ' ') != NULL) {
                    int minutes = atoi(task->cron_expression + 2);
                    if (minutes > 0) {
                        // To ensure execution at standard minute points (0, n, 2n, ...),
                        // we need to align the next run time properly
                        time_t now_seconds = now % 60;
                        time_t now_minutes = (now / 60) % 60;
                        time_t minutes_offset = now_minutes % minutes;
                        
                        // If partway through a cycle, calculate remaining time
                        time_t wait_time;
                        if (minutes_offset == 0 && now_seconds == 0) {
                            // Exactly at cycle minute point, wait full n minutes
                            wait_time = minutes * 60;
                        } else {
                            // Wait until next minute in the cycle
                            wait_time = ((minutes - minutes_offset) * 60) - now_seconds;
                        }
                        
                        next_time = now + wait_time;
                        task->next_run_time = next_time;
                        return true;
                    }
                }
                
                // Handle pattern "0 * * * *" (every hour at minute 0)
                if (strcmp(task->cron_expression, "0 * * * *") == 0) {
                    struct tm *tm_now = localtime(&now);
                    struct tm next;
                    memcpy(&next, tm_now, sizeof(struct tm));
                    
                    // Calculate next hour, minute 0
                    next.tm_min = 0;
                    next.tm_sec = 0;
                    next.tm_hour += 1;
                    
                    time_t next_hour = mktime(&next);
                    if (next_hour <= now) {
                        next_hour = now + 3600; // If calculation error, default to 1 hour later
                    }
                    
                    task->next_run_time = next_hour;
                    return true;
                }
                
                // Handle pattern "0 0 * * *" (every day at 00:00)
                if (strcmp(task->cron_expression, "0 0 * * *") == 0) {
                    struct tm *tm_now = localtime(&now);
                    struct tm next;
                    memcpy(&next, tm_now, sizeof(struct tm));
                    
                    // Calculate next day, minute 0, hour 0
                    next.tm_min = 0;
                    next.tm_sec = 0;
                    next.tm_hour = 0;
                    next.tm_mday += 1;
                    
                    time_t next_day = mktime(&next);
                    if (next_day <= now) {
                        next_day = now + 86400; // If calculation error, default to 1 day later
                    }
                    
                    task->next_run_time = next_day;
                    return true;
                }
                
                // For other cron patterns that we don't handle specifically,
                // use smarter fallback logic
                
                // If run before, schedule next based on recent interval
                if (task->last_run_time > 0) {
                    // For cron expressions not fully supported, use fallback logic
                    if (strstr(task->cron_expression, "* * * *") != NULL) {
                        // Indication of running every minute
                        task->next_run_time = now + 60;
                    } else if (strstr(task->cron_expression, "0 * * *") != NULL) {
                        // Indication of running every hour
                        task->next_run_time = now + 3600;
                    } else if (strstr(task->cron_expression, "0 0 * *") != NULL) {
                        // Indication of running every day
                        task->next_run_time = now + 86400;
                    } else {
                        // Default to 1 hour later
                        task->next_run_time = now + 3600;
                    }
                } else {
                    // If never run before, schedule for 1 minute later
                    task->next_run_time = now + 60;
                }
                
                // Print debug message
                log_message(LOG_DEBUG, "Cron expression '%s' not fully supported. Using default scheduling.", 
                           task->cron_expression);
            } else {
                // Invalid cron expression
                task->next_run_time = 0;
                log_message(LOG_WARNING, "Empty or invalid cron expression");
            }
            return true;
    }
    
    // Legacy frequency-based scheduling (for backward compatibility)
    // Start with the current time
    time_t next_run = now;
    local_time = localtime(&next_run);
    memcpy(&next_time, local_time, sizeof(struct tm));
    
    switch (task->frequency) {
        case ONCE:
            // If it's a one-time task that already ran, don't reschedule
            if (task->last_run_time > 0) {
                task->next_run_time = 0;
                // Khôi phục lại các giá trị
                task->last_run_time = original_last_run_time;
                task->exit_code = original_exit_code;
                return true;
            }
            // If next_run_time is already set, keep it
            if (task->next_run_time > now) {
                // Khôi phục lại các giá trị
                task->last_run_time = original_last_run_time;
                task->exit_code = original_exit_code;
                return true;
            }
            // Otherwise, schedule it for now
            task->next_run_time = now;
            break;
            
        case DAILY:
            // Set time to the specified time tomorrow
            next_time.tm_mday += 1;
            next_time.tm_hour = task->interval / 100;
            next_time.tm_min = task->interval % 100;
            next_time.tm_sec = 0;
            next_run = mktime(&next_time);
            
            // If we haven't run today and the time is still in the future, run today
            if (task->last_run_time < now - 86400 && 
                (local_time->tm_hour < next_time.tm_hour ||
                (local_time->tm_hour == next_time.tm_hour && local_time->tm_min < next_time.tm_min))) {
                next_time.tm_mday -= 1;
                next_run = mktime(&next_time);
            }
            
            task->next_run_time = next_run;
            break;
            
        case WEEKLY:
            // Schedule for next week on the specified day
            next_time.tm_mday += (task->interval - next_time.tm_wday + 7) % 7;
            next_time.tm_hour = 0;
            next_time.tm_min = 0;
            next_time.tm_sec = 0;
            next_run = mktime(&next_time);
            
            // If we haven't run this week and the day is still coming, run this week
            if (task->last_run_time < now - 7*86400 && local_time->tm_wday < task->interval) {
                next_time.tm_mday -= 7;
                next_run = mktime(&next_time);
            }
            
            task->next_run_time = next_run;
            break;
            
        case MONTHLY:
            // Schedule for next month on the specified day
            next_time.tm_mon += 1;
            next_time.tm_mday = task->interval;
            next_time.tm_hour = 0;
            next_time.tm_min = 0;
            next_time.tm_sec = 0;
            next_run = mktime(&next_time);
            
            // If we haven't run this month and the day is still coming, run this month
            if (task->last_run_time < now - 30*86400 && local_time->tm_mday < task->interval) {
                next_time.tm_mon -= 1;
                next_run = mktime(&next_time);
            }
            
            task->next_run_time = next_run;
            break;
            
        case CUSTOM:
            // For custom frequency, simply add interval seconds to the last run time
            if (task->last_run_time > 0) {
                task->next_run_time = task->last_run_time + task->interval;
            } else {
                task->next_run_time = now + task->interval;
            }
            // Không khôi phục lại giá trị cho frequency=CUSTOM (thường dùng cho interval)
            return true;
    }
    
    // Khôi phục lại các giá trị last_run_time và exit_code trước khi trả về
    // Không khôi phục lại cho SCHEDULE_INTERVAL và SCHEDULE_CRON
    if (task->schedule_type != SCHEDULE_INTERVAL && task->schedule_type != SCHEDULE_CRON) {
        task->last_run_time = original_last_run_time;
        task->exit_code = original_exit_code;
    }
    
    return true;
}

bool task_is_due(Task *task) {
    if (!task || !task->enabled) {
        return false;
    }
    
    time_t now = time(NULL);
    
    // Task is due if the next run time is in the past or now
    return task->next_run_time > 0 && task->next_run_time <= now;
}

bool task_mark_executed(Task *task, int exit_code) {
    if (!task) {
        return false;
    }
    
    // Lưu lại thời gian trước khi cập nhật
    time_t old_next_run = task->next_run_time;
    time_t now = time(NULL);
    
    // Cập nhật thời gian và exit code
    task->last_run_time = now;
    task->exit_code = exit_code;
    
    // Ghi log chi tiết trước khi tính thời gian chạy tiếp theo
    log_message(LOG_DEBUG, "Task %d (%s): Marked as executed with exit_code=%d", 
               task->id, task->name, exit_code);
    
    if (task->schedule_type == SCHEDULE_CRON) {
        log_message(LOG_DEBUG, "Task %d (%s): Calculating next run time for cron task with expression '%s'", 
                   task->id, task->name, task->cron_expression);
    } else if (task->schedule_type == SCHEDULE_INTERVAL) {
        log_message(LOG_DEBUG, "Task %d (%s): Calculating next run time for interval task with interval %d minutes", 
                   task->id, task->name, task->interval);
    }
    
    // Update the next run time
    bool result = task_calculate_next_run(task);
    
    // Ghi log kết quả tính toán thời gian chạy tiếp theo
    if (result) {
        char next_time_str[64];
        time_to_string(task->next_run_time, next_time_str, sizeof(next_time_str), NULL);
        
        log_message(LOG_DEBUG, "Task %d (%s): Calculated next run time = %s (old: %ld, new: %ld)",
                  task->id, task->name, next_time_str, old_next_run, task->next_run_time);
    } else {
        log_message(LOG_ERROR, "Task %d (%s): Failed to calculate next run time",
                  task->id, task->name);
    }
    
    return result;
}

bool task_add_dependency(Task *task, int dependency_id) {
    if (!task || dependency_id < 0) {
        return false;
    }
    
    // Check if already at max dependencies
    if (task->dependency_count >= MAX_DEPENDENCIES) {
        log_message(LOG_ERROR, "Cannot add dependency: Maximum number of dependencies reached");
        return false;
    }
    
    // Check if dependency already exists
    for (int i = 0; i < task->dependency_count; i++) {
        if (task->dependencies[i] == dependency_id) {
            // Already exists, not an error
            return true;
        }
    }
    
    // Check for circular dependency (simple case: direct circular)
    if (task->id == dependency_id) {
        log_message(LOG_ERROR, "Cannot add dependency: Task cannot depend on itself");
        return false;
    }
    
    // Add the dependency
    task->dependencies[task->dependency_count++] = dependency_id;
    log_message(LOG_INFO, "Added dependency: Task %d now depends on Task %d", 
               task->id, dependency_id);
    
    return true;
}

bool task_remove_dependency(Task *task, int dependency_id) {
    if (!task) {
        return false;
    }
    
    // Find the dependency
    int found = -1;
    for (int i = 0; i < task->dependency_count; i++) {
        if (task->dependencies[i] == dependency_id) {
            found = i;
            break;
        }
    }
    
    if (found < 0) {
        // Dependency not found
        return false;
    }
    
    // Remove by shifting remaining dependencies
    for (int i = found; i < task->dependency_count - 1; i++) {
        task->dependencies[i] = task->dependencies[i + 1];
    }
    
    task->dependency_count--;
    log_message(LOG_INFO, "Removed dependency: Task %d no longer depends on Task %d", 
               task->id, dependency_id);
    
    return true;
}

bool task_prepare_script(Task *task, char *temp_path, size_t temp_path_size) {
    if (!task || !temp_path || temp_path_size == 0 || 
        task->exec_mode != EXEC_SCRIPT || task->script_content[0] == '\0') {
        return false;
    }
    
    // Create a temporary file for the script
    char template[] = "/tmp/taskscript_XXXXXX";
    int fd = mkstemp(template);
    if (fd == -1) {
        log_message(LOG_ERROR, "Failed to create temporary script file");
        return false;
    }
    
    // Make the script executable
    fchmod(fd, 0755);
    
    // Write the script content to the file
    if (write(fd, task->script_content, strlen(task->script_content)) == -1) {
        log_message(LOG_ERROR, "Failed to write script content to temporary file");
        close(fd);
        unlink(template);
        return false;
    }
    
    close(fd);
    
    // Copy the path to the output buffer
    safe_strcpy(temp_path, template, temp_path_size);
    
    return true;
} 
