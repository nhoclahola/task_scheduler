# Task Scheduler

Hệ thống quản lý và lập lịch tác vụ được viết bằng C với giao diện web sử dụng Python Flask.

## Tính năng

- Lập lịch tác vụ với nhiều tùy chọn: thủ công, định kỳ, biểu thức cron
- Thực thi lệnh shell hoặc script
- Quản lý phụ thuộc giữa các tác vụ
- Theo dõi trạng thái và lịch sử thực thi
- Giao diện web hiện đại, dễ sử dụng
- Thư viện C có thể tích hợp vào các ứng dụng khác

## Yêu cầu

- C compiler (GCC hoặc tương đương)
- Python 3.8+
- Make

## Cài đặt

```bash
git clone https://github.com/your-username/task-scheduler.git
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

## Cấu trúc thư mục

```
task_scheduler/
├── bin/              # Thư mục chứa file binary 
├── data/             # Dữ liệu của scheduler
├── include/          # Header files
├── lib/              # Thư viện được biên dịch
├── src/              # Mã nguồn C
├── web/              # Giao diện web (Python Flask)
├── Makefile          # File biên dịch
├── run.py            # Script khởi động tất cả thành phần
└── README.md         # Tài liệu này
```

## Phát triển

Tham khảo thêm tài liệu trong thư mục `docs/` cho thông tin chi tiết về API và kiến trúc hệ thống.

## Giấy phép

[Thêm thông tin về giấy phép của dự án] 