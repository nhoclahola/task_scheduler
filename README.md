# Task Scheduler for Linux

Ứng dụng Task Scheduler cho Linux được viết bằng C, có chức năng tương tự như Task Scheduler trên Windows hoặc cron trên Linux, nhưng được xây dựng với cơ chế nội bộ riêng.

## Tính năng

- Lên lịch các tác vụ để chạy vào thời điểm hoặc khoảng thời gian nhất định
- Hỗ trợ lên lịch một lần, hàng ngày, hàng tuần, hàng tháng
- Quản lý danh sách các tác vụ (tạo, xóa, sửa, xem)
- Lưu trữ lịch sử thực thi tác vụ
- Xử lý lỗi và cơ chế phục hồi

## Cấu trúc dự án

```
task_scheduler/
├── include/             # Header files
├── src/                 # Source code
├── lib/                 # Libraries
├── bin/                 # Binary output
├── data/                # Data storage
└── tests/               # Test files
```

## Cài đặt

```bash
make
```

## Sử dụng

```bash
./bin/taskscheduler [options]
``` 