#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <pthread.h>
#include <stdbool.h>
#include <time.h>
#include "task.h"

// Định nghĩa cấu trúc Scheduler
typedef struct {
    Task *tasks;                // Mảng các tác vụ
    int task_count;             // Số lượng tác vụ
    int capacity;               // Dung lượng mảng
    bool running;               // Trạng thái của scheduler
    int check_interval;         // Chu kỳ kiểm tra (giây)
    pthread_t thread;           // Thread ID
    pthread_mutex_t lock;       // Mutex để đồng bộ hóa
} Scheduler;

// Khởi tạo scheduler
Scheduler* scheduler_init(int check_interval);

// Thêm tác vụ vào scheduler
int scheduler_add_task(Scheduler *scheduler, Task *task);

// Xóa tác vụ khỏi scheduler
bool scheduler_remove_task(Scheduler *scheduler, int task_id);

// Cập nhật tác vụ trong scheduler
bool scheduler_update_task(Scheduler *scheduler, Task *task);

// Tìm tác vụ theo ID
Task* scheduler_get_task(Scheduler *scheduler, int task_id);

// Tìm index của tác vụ trong mảng dựa trên ID
int find_task_index(Scheduler *scheduler, int task_id);

// Khởi động scheduler
bool scheduler_start(Scheduler *scheduler);

// Dừng scheduler
void scheduler_stop(Scheduler *scheduler);

// Giải phóng scheduler
void scheduler_free(Scheduler *scheduler);

// Lấy danh sách tất cả các tác vụ
Task* scheduler_get_all_tasks(Scheduler *scheduler, int *count);

// Kiểm tra xem tác vụ có các phụ thuộc đã được thỏa mãn chưa
bool check_dependencies_satisfied(Scheduler *scheduler, Task *task);

#endif // SCHEDULER_H 