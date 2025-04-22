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

## Tính năng AI Dynamic

Tác vụ AI Dynamic sử dụng mô hình DeepSeek để tạo lệnh tự động dựa trên thông số hệ thống hiện tại và mục tiêu được chỉ định.

### Thiết lập API Key

```bash
# Thiết lập API key
./bin/taskscheduler set-api-key your_deepseek_api_key_here

# Xem thông tin API key đã cấu hình
./bin/taskscheduler view-api-key
```

API key sẽ được lưu trong file `data/config.json` và sẽ được sử dụng khi thực thi tác vụ AI Dynamic.

### Tạo tác vụ AI Dynamic

```bash
# Tạo tác vụ AI mới
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


