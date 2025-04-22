# Task Scheduler

Hệ thống quản lý và lập lịch tác vụ được viết bằng C với giao diện web sử dụng Python Flask.

## Tính năng

- Lập lịch tác vụ với nhiều tùy chọn: thủ công, định kỳ, biểu thức cron
- Thực thi lệnh shell hoặc script
- **Tác vụ AI tự động**: Tạo và thực thi lệnh dựa trên các thông số hệ thống
- Quản lý phụ thuộc giữa các tác vụ
- Theo dõi trạng thái và lịch sử thực thi
- Giao diện web hiện đại, dễ sử dụng
- Thư viện C có thể tích hợp vào các ứng dụng khác

## Yêu cầu

- C compiler (GCC hoặc tương đương)
- Python 3.8+
- Make
- libcurl, libcjson (cho tính năng AI)

## Cài đặt

```bash
git clone https://github.com/nhoclahola/task-scheduler.git
cd task-scheduler
make
```

## Sử dụng

### Khởi động hoàn chỉnh (Backend C + Web Interface)

Sử dụng script `run.py` để khởi động cả backend C và giao diện web:

```bash
python run.py
```

Script này sẽ tự động:
1. Kiểm tra và biên dịch các thành phần C nếu cần
2. Khởi động backend C
3. Khởi động giao diện web Flask
4. Cung cấp đường dẫn để truy cập vào giao diện web

### Chỉ sử dụng thư viện C

Nếu bạn chỉ muốn sử dụng backend C:

```bash
./bin/taskscheduler [options]
```

Các tùy chọn:
- `-h`: Hiển thị trợ giúp
- `-d`: Chạy ở chế độ daemon
- `-D <directory>`: Chỉ định thư mục dữ liệu

### Chỉ sử dụng giao diện web

Nếu bạn đã có backend C chạy và chỉ muốn khởi động giao diện web:

```bash
cd web
python app.py
```

Sau đó truy cập:
```
http://localhost:5000
```

## Tính năng AI 

Task Scheduler cung cấp hai tính năng AI mạnh mẽ:

### 1. AI Agent - Tạo Task từ Mô tả Ngôn ngữ Tự nhiên

Tính năng này cho phép tạo task chỉ cần dùng mô tả bằng ngôn ngữ tự nhiên. AI sẽ tự động phân tích yêu cầu, tạo ra lệnh shell hoặc script phù hợp, rồi hỏi người dùng xác nhận và tên cho task.

```bash
# Create a new task using the AI Agent - just describe it in natural language
./bin/taskscheduler ai-create "check disk space every 10 minutes and notify if less than 10% free"

# Create a task to clean temp files at midnight
./bin/taskscheduler ai-create "clean temporary files in /tmp daily at midnight"
```

AI sẽ:
1. Tự động tạo lệnh đơn giản hoặc script đầy đủ tùy theo độ phức tạp của yêu cầu
2. Tự phân tích khoảng thời gian từ mô tả (ví dụ: "every 1 minute", "every 10 minutes", "daily", "midnight")
3. Hiển thị lệnh/script để người dùng xác nhận bằng tiếng Anh
4. Hỏi tên task (hoặc sử dụng mô tả làm tên nếu không có tên được cung cấp)

Với quy trình đơn giản này, người dùng không cần phải chỉ định các tham số phức tạp như khoảng thời gian, cron expression hay thư mục làm việc.

### 2. AI Dynamic - Tạo lệnh dựa trên Metrics Hệ thống

Tính năng này tạo và thực thi lệnh dựa trên thông số hệ thống thời gian thực.

```bash
# Thiết lập API key
./bin/taskscheduler set-api-key your_deepseek_api_key_here

# Chuyển đổi task sang AI Dynamic
./bin/taskscheduler to-ai <task_id> "<mô tả mục tiêu>" "<thông số hệ thống>"

# Ví dụ
./bin/taskscheduler to-ai 1 "Dọn dẹp các tệp nhật ký cũ" "disk:/var/log,mem_free,cpu_load"
```

Các thông số hệ thống có thể bao gồm:
- `cpu_load` - Phần trăm tải CPU
- `mem` - Thông tin sử dụng bộ nhớ
- `disk:/path` - Mức sử dụng ổ đĩa cho đường dẫn cụ thể
- `load_avg` - Trung bình tải hệ thống
- `processes` - Thông tin về các tiến trình

## Cấu trúc thư mục

```
task_scheduler/
├── bin/              # Thư mục chứa file binary 
├── data/             # Dữ liệu của scheduler
│   └── config.json   # File cấu hình (chứa API key)
├── include/          # Header files
├── lib/              # Thư viện được biên dịch
├── src/              # Mã nguồn C
├── web/              # Giao diện web (Python Flask)
├── Makefile          # File biên dịch
├── run.py            # Script khởi động tất cả thành phần
└── README.md         # Tài liệu này
```


