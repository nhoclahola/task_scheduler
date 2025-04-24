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

# Phân tích Tốc độ Mạng

Công cụ này phân tích tốc độ mạng bằng cách thực hiện ping đến các trang web, thu thập dữ liệu và tạo các biểu đồ trực quan để hiển thị hiệu suất mạng.

## Tính năng

- **Ping đến nhiều trang web**: Đo thời gian ping đến danh sách các trang web
- **Phân tích dữ liệu**: Tính toán các chỉ số như thời gian ping trung bình, tối thiểu, tối đa và tỉ lệ timeout
- **Trực quan hóa dữ liệu**: Tạo các biểu đồ:
  - Biểu đồ thời gian ping theo thời gian
  - Biểu đồ mạng sử dụng NetworkX
  - Biểu đồ cột thời gian ping trung bình theo trang web
- **Quét mạng cục bộ**: Tìm các host đang hoạt động trong mạng cục bộ

## Cài đặt

```bash
# Cài đặt các thư viện cần thiết
pip install pandas numpy matplotlib networkx ping3 seaborn ipaddress
```

## Sử dụng

Chạy chương trình:

```bash
python network_speed_analyzer.py
```

Mặc định, chương trình sẽ:
1. Ping đến 8 trang web phổ biến
2. Tạo các biểu đồ phân tích
3. Quét mạng cục bộ để tìm các host đang hoạt động
4. Lưu các biểu đồ dưới dạng tệp PNG

## Kết quả

Chương trình sẽ tạo ra ba biểu đồ:
- **ping_times_chart.png**: Biểu đồ thời gian ping theo thời gian
- **network_graph.png**: Biểu đồ mạng hiển thị kết nối từ máy tính đến các trang web
- **ping_barplot.png**: Biểu đồ cột thời gian ping trung bình theo trang web

## Cấu trúc mã nguồn

```
network_speed_analyzer.py
├── Lớp NetworkTools
│   ├── get_delay_time(target): Đo thời gian ping
│   ├── resolve_hostname(ip): Phân giải tên máy chủ
│   ├── get_local_ip(): Lấy địa chỉ IP cục bộ
│   ├── discover_hosts(subnet_cidr): Tìm host trong mạng
│   ├── scan_network(): Quét mạng cục bộ
│   ├── ping_domains(domains, num_pings): Ping nhiều trang web
│   ├── analyze_ping_results(): Phân tích kết quả ping
│   ├── plot_ping_times(): Vẽ biểu đồ thời gian ping
│   ├── create_network_graph(): Tạo biểu đồ mạng
│   ├── visualize_network(): Hiển thị biểu đồ mạng
│   ├── plot_ping_barplot(): Vẽ biểu đồ cột
│   └── analyze_network_metrics(): Phân tích chỉ số mạng
└── main(): Hàm chính thực thi chương trình
```

## Yêu cầu hệ thống

- Python 3.6+
- Thư viện: pandas, numpy, matplotlib, networkx, ping3, seaborn, ipaddress

## Kết hợp tính năng

Chương trình này kết hợp các tính năng từ:
1. Chức năng quét mạng và tạo biểu đồ mạng
2. Phân tích thời gian ping đến các trang web
3. Trực quan hóa dữ liệu bằng nhiều dạng biểu đồ

Mã nguồn được thiết kế theo hướng đối tượng, dễ mở rộng và tự động hóa quá trình phân tích tốc độ mạng.


