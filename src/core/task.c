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

// Hàm mới để xác định nếu một giá trị nằm trong biểu thức cron field
// Xử lý cả số cụ thể, khoảng (1-5), danh sách (1,3,5), bước nhảy (*/2, 1-10/2)
static bool is_matching_cron_field(const char *field_expr, int value) {
    // Kiểm tra wildcard "*" - khớp với mọi giá trị
    if (strcmp(field_expr, "*") == 0) {
        return true;
    }
    
    // Kiểm tra giá trị cụ thể
    if (isdigit((unsigned char)field_expr[0])) {
        int specific_value;
        if (sscanf(field_expr, "%d", &specific_value) == 1) {
            if (specific_value == value) {
                return true;
            }
        }
    }
    
    // Kiểm tra khoảng (range) - ví dụ "1-5"
    int range_start, range_end;
    if (sscanf(field_expr, "%d-%d", &range_start, &range_end) == 2) {
        if (value >= range_start && value <= range_end) {
            return true;
        }
    }
    
    // Kiểm tra danh sách - ví dụ "1,3,5,7"
    char *field_copy = strdup(field_expr);
    if (field_copy) {
        char *token = strtok(field_copy, ",");
        while (token) {
            // Kiểm tra từng phần tử trong danh sách
            int list_val;
            if (sscanf(token, "%d", &list_val) == 1) {
                if (list_val == value) {
                    free(field_copy);
                    return true;
                }
            }
            
            // Kiểm tra khoảng trong danh sách
            int list_range_start, list_range_end;
            if (sscanf(token, "%d-%d", &list_range_start, &list_range_end) == 2) {
                if (value >= list_range_start && value <= list_range_end) {
                    free(field_copy);
                    return true;
                }
            }
            
            token = strtok(NULL, ",");
        }
        free(field_copy);
    }
    
    // Kiểm tra bước nhảy (step) - ví dụ "*/2", "1-10/2"
    if (strstr(field_expr, "/") != NULL) {
        char range_part[32] = "*";
        int step = 1;
        
        if (sscanf(field_expr, "%31[^/]/%d", range_part, &step) == 2 && step > 0) {
            // Xác định phạm vi
            int min_val = 0, max_val = 59; // Mặc định cho phút
            
            // Xử lý phạm vi cụ thể
            if (strcmp(range_part, "*") != 0) {
                int range_start = 0, range_end = 59;
                if (sscanf(range_part, "%d-%d", &range_start, &range_end) == 2) {
                    min_val = range_start;
                    max_val = range_end;
                } else if (sscanf(range_part, "%d", &range_start) == 1) {
                    min_val = range_start;
                    max_val = 59; // Cho phút
                }
            }
            
            // Kiểm tra nếu giá trị nằm trong phạm vi và chia hết cho bước
            if (value >= min_val && value <= max_val && (value - min_val) % step == 0) {
                return true;
            }
        }
    }
    
    return false;
}

// Hàm mới để tìm ngày tiếp theo mà biểu thức cron của chúng ta sẽ chạy
static time_t find_next_cron_time(const char *cron_expr, time_t now) {
    char min_str[32], hr_str[32], dom_str[32], mon_str[32], dow_str[32];
    if (sscanf(cron_expr, "%31s %31s %31s %31s %31s", 
              min_str, hr_str, dom_str, mon_str, dow_str) != 5) {
        // Lỗi phân tích cú pháp, sử dụng mặc định sau 1 giờ
        return now + 3600;
    }
    
    struct tm *tm_now = localtime(&now);
    struct tm next = *tm_now;
    next.tm_sec = 0;
    
    // Bắt đầu từ phút tiếp theo
    next.tm_min++;
    if (next.tm_min >= 60) {
        next.tm_min = 0;
        next.tm_hour++;
        if (next.tm_hour >= 24) {
            next.tm_hour = 0;
            next.tm_mday++;
            // mktime sẽ xử lý chuyển đổi ngày/tháng nếu cần
        }
    }
    
    // Tìm thời gian hợp lệ tiếp theo, tối đa 366 ngày vào tương lai
    for (int days = 0; days < 366; days++) {
        for (int hours = 0; hours < 24; hours++) {
            for (int minutes = 0; minutes < 60; minutes++) {
                // Nếu vượt qua ngày đầu tiên, reset về đầu ngày
                if (days > 0 && hours == 0 && minutes == 0) {
                    next.tm_hour = 0;
                    next.tm_min = 0;
                }
                
                // Nếu vượt qua giờ đầu tiên của ngày, reset về đầu giờ
                if (hours > 0 && minutes == 0) {
                    next.tm_min = 0;
                }
                
                // Kiểm tra nếu thời điểm hiện tại khớp với biểu thức cron
                // Sử dụng phương pháp loại trừ - nếu bất kỳ trường nào không khớp, thì tiếp tục
                
                // Kiểm tra phút
                if (!is_matching_cron_field(min_str, next.tm_min)) {
                    // Tăng phút lên và kiểm tra lại
                    next.tm_min++;
                    if (next.tm_min >= 60) {
                        next.tm_min = 0;
                        next.tm_hour++;
                        if (next.tm_hour >= 24) {
                            next.tm_hour = 0;
                            next.tm_mday++;
                            mktime(&next); // Điều chỉnh ngày/tháng nếu cần
                        }
                    }
                    continue;
                }
                
                // Kiểm tra giờ
                if (!is_matching_cron_field(hr_str, next.tm_hour)) {
                    // Tăng giờ và reset phút
                    next.tm_hour++;
                    next.tm_min = 0;
                    if (next.tm_hour >= 24) {
                        next.tm_hour = 0;
                        next.tm_mday++;
                        mktime(&next); // Điều chỉnh ngày/tháng nếu cần
                    }
                    continue;
                }
                
                // Thực hiện chuẩn hóa struct tm
                mktime(&next);
                
                // Kiểm tra ngày trong tháng
                if (!is_matching_cron_field(dom_str, next.tm_mday)) {
                    // Chuyển sang ngày tiếp theo
                    next.tm_mday++;
                    next.tm_hour = 0;
                    next.tm_min = 0;
                    mktime(&next); // Điều chỉnh nếu cần
                    continue;
                }
                
                // Kiểm tra tháng (phải +1 vì tm_mon bắt đầu từ 0 nhưng cron từ 1)
                if (!is_matching_cron_field(mon_str, next.tm_mon + 1)) {
                    // Chuyển sang tháng tiếp theo
                    next.tm_mon++;
                    next.tm_mday = 1;
                    next.tm_hour = 0;
                    next.tm_min = 0;
                    mktime(&next); // Điều chỉnh nếu cần
                    continue;
                }
                
                // Kiểm tra ngày trong tuần (0 = Chủ nhật trong struct tm)
                int dow_value = next.tm_wday;
                
                // Điều chỉnh để 0 = Chủ nhật, 1 = Thứ hai, ... như trong cron
                if (!is_matching_cron_field(dow_str, dow_value)) {
                    // Nếu DOM là wildcard (*) và DOW không khớp, thì tăng 1 ngày
                    if (strcmp(dom_str, "*") == 0) {
                        next.tm_mday++;
                        next.tm_hour = 0;
                        next.tm_min = 0;
                        mktime(&next);
                        continue;
                    }
                    // Nếu cả DOM và DOW đều chỉ định, chúng được giải thích là OR
                    // Nếu DOM khớp, chúng ta đã OK từ các kiểm tra trước
                }
                
                // Nếu tất cả các kiểm tra đều pass, đây là thời gian hợp lệ tiếp theo
                return mktime(&next);
            }
        }
    }
    
    // Nếu không tìm thấy thời gian hợp lệ, sử dụng mặc định sau 1 giờ
    return now + 3600;
}

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
            // Sử dụng hàm phân tích cron được cải thiện để tìm thời gian hợp lệ tiếp theo
            if (task->cron_expression[0] != '\0') {
                time_t next_time = find_next_cron_time(task->cron_expression, now);
                
                if (next_time > now) {
                    task->next_run_time = next_time;
                    
                    // Log thời gian chạy tiếp theo
                    char time_str[64];
                    struct tm *next_tm = localtime(&task->next_run_time);
                    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", next_tm);
                    log_message(LOG_DEBUG, "Task %d (%s): Next run time for cron '%s' is %s",
                               task->id, task->name, task->cron_expression, time_str);
                    
                    return true;
                } else {
                    // Fallback nếu tính toán cron thất bại
                    log_message(LOG_WARNING, "Task %d (%s): Cron calculation failed, using hourly fallback",
                               task->id, task->name);
                    task->next_run_time = now + 3600;
                    return true;
                }
            } else {
                // Invalid cron expression
                task->next_run_time = 0;
                log_message(LOG_WARNING, "Empty or invalid cron expression");
                return true;
            }
            break;
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
    
    // Tính toán thời gian chạy tiếp theo
    bool result = task_calculate_next_run(task);
    
    // Log thời gian chạy tiếp theo
    char time_str[64];
    if (task->next_run_time > 0) {
        struct tm *next_tm = localtime(&task->next_run_time);
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", next_tm);
        log_message(LOG_DEBUG, "Task %d (%s): Next run time calculated: %s", 
                   task->id, task->name, time_str);
    } else {
        log_message(LOG_DEBUG, "Task %d (%s): No next run time scheduled", 
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
