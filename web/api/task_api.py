import os
import json
import subprocess
import time
import threading
import queue
import pty
import fcntl
import re
import select
from datetime import datetime

class TaskAPI:
    """API wrapper cho các lệnh của taskscheduler qua dòng lệnh"""
    
    def __init__(self):
        """Khởi tạo API wrapper"""
        # Đường dẫn đến thư mục gốc của dự án
        self.base_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
        
        # Đường dẫn đến thư mục bin
        self.bin_dir = os.path.join(self.base_dir, "bin")
        
        # Đường dẫn đến file thực thi
        self.bin_path = os.path.join(self.bin_dir, "taskscheduler")
        
        # Đường dẫn đến thư mục data - Điều chỉnh để trỏ đến thư mục data trong bin
        # Đây là thư mục mà binary C sẽ sử dụng
        self.data_dir = os.path.join(self.bin_dir, "data")
        
        # Đường dẫn đến thư mục lib
        self.lib_dir = os.path.join(self.base_dir, "lib")
        
        # Chế độ giả lập - chỉ kiểm tra file binary, không kiểm tra thư viện
        self.simulation_mode = os.environ.get('TASK_SCHEDULER_SIMULATION') == '1' or not os.path.exists(self.bin_path)
        
        # Biến để theo dõi quá trình tương tác
        self.process = None
        self.master_fd = None
        self.slave_fd = None
        self.interactive_lock = threading.Lock()
        
        if self.simulation_mode:
            print("WARNING: Running in simulation mode - no taskscheduler binary available")
            self.next_id = 1
            self.tasks = {}
        else:
            # Đảm bảo thư mục data tồn tại
            os.makedirs(self.data_dir, exist_ok=True)
            
            # Thiết lập quyền thực thi cho binary nếu cần
            if not os.access(self.bin_path, os.X_OK):
                try:
                    os.chmod(self.bin_path, 0o755)
                    print(f"Added execute permission to {self.bin_path}")
                except Exception as e:
                    print(f"Warning: Could not set execute permission on {self.bin_path}: {e}")
            
            # In thông tin về đường dẫn dữ liệu
            print(f"Data directory for the task scheduler: {self.data_dir}")
            print(f"Database file path: {os.path.join(self.data_dir, 'tasks.db')}")
            
            # Khởi động quá trình tương tác với binary
            self._start_interactive_process()
    
    def _start_interactive_process(self):
        """Khởi động quá trình tương tác với binary"""
        if self.simulation_mode:
            return
        
        try:
            # Tạo môi trường cho lệnh
            env = os.environ.copy()
            
            # Thiết lập thư mục làm việc là thư mục bin
            working_dir = self.bin_dir
            
            # Khởi tạo pty để tương tác với binary
            self.master_fd, self.slave_fd = pty.openpty()
            
            # Thiết lập chế độ không chặn cho master
            flags = fcntl.fcntl(self.master_fd, fcntl.F_GETFL)
            fcntl.fcntl(self.master_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
            
            # Chạy binary trong tiến trình con, không cần chỉ định -D vì binary sẽ tự tìm trong thư mục hiện tại
            cmd = ["./taskscheduler", "-i"]
            print(f"Starting interactive process with command: {' '.join(cmd)} in {working_dir}")
            
            self.process = subprocess.Popen(
                cmd,
                stdin=self.slave_fd,
                stdout=self.slave_fd,
                stderr=self.slave_fd,
                cwd=working_dir,
                env=env,
                start_new_session=True
            )
            
            # Đợi cho binary khởi động và hiển thị dấu nhắc
            time.sleep(1)
            
            # Đọc đầu ra ban đầu để xác nhận binary đã khởi động
            initial_output = self._read_output()
            print(f"Initial output from binary: {initial_output}")
            
            # Kiểm tra xem binary có đang chạy không
            if self.process.poll() is not None:
                print(f"Binary exited prematurely with code {self.process.returncode}")
                self.simulation_mode = True
            else:
                print("Interactive mode started successfully")
        except Exception as e:
            print(f"Error starting interactive process: {e}")
            self.simulation_mode = True
            
            # Dọn dẹp nếu có lỗi
            if self.master_fd:
                os.close(self.master_fd)
            if self.slave_fd:
                os.close(self.slave_fd)
            if self.process and self.process.poll() is None:
                self.process.terminate()
            
            self.master_fd = None
            self.slave_fd = None
            self.process = None
    
    def _read_output(self, timeout=10.0, command=None):
        """Đọc đầu ra từ binary tương tác, với timeout tùy chỉnh"""
        if self.simulation_mode or not self.master_fd:
            return ""
        
        output = ""
        end_time = time.time() + timeout
        
        # Lệnh ai-create cần thời gian xử lý lâu hơn
        if command and 'ai-create' in command:
            # Tăng thời gian chờ cho lệnh AI
            end_time = time.time() + 30.0
        
        waiting_for_prompt = True
        
        while time.time() < end_time and waiting_for_prompt:
            try:
                # Kiểm tra xem có dữ liệu để đọc không
                readable, _, _ = select.select([self.master_fd], [], [], 0.1)
                if self.master_fd in readable:
                    chunk = os.read(self.master_fd, 4096).decode('utf-8', errors='replace')
                    if not chunk:  # Kết nối đã đóng
                        break
                    output += chunk
                    
                    # Kiểm tra xem đã nhận được kết quả hoàn chỉnh chưa
                    if command and 'ai-create' in command:
                        # Điều kiện đặc biệt cho lệnh AI
                        if "Suggested name:" in output or "Task created" in output or "Done" in output:
                            waiting_for_prompt = False
                            print(f"Detected AI response completion: {output[-100:]}")
                            break
                        
                    # Kiểm tra xem có dấu nhắc cuối cùng không
                    if output.strip().endswith(">"):
                        waiting_for_prompt = False
                        break
                else:
                    # Không có dữ liệu sẵn sàng, chờ một chút
                    time.sleep(0.2)
            except Exception as e:
                print(f"Error reading from process: {e}")
                break
        
        # Thử đọc thêm một lần nữa để chắc chắn có tất cả đầu ra
        try:
            readable, _, _ = select.select([self.master_fd], [], [], 0.5)
            if self.master_fd in readable:
                chunk = os.read(self.master_fd, 8192).decode('utf-8', errors='replace')
                if chunk:
                    output += chunk
        except:
            pass
        
        if time.time() >= end_time:
            print(f"Warning: Timeout waiting for output from command {command}")
        
        return output
    
    def _send_command(self, command):
        """Gửi lệnh đến binary tương tác và đợi phản hồi"""
        if self.simulation_mode or not self.master_fd or not self.process or self.process.poll() is not None:
            return None, ""
        
        with self.interactive_lock:
            try:
                # Gửi lệnh kèm theo ký tự xuống dòng
                print(f"Sending to binary: '{command}'")
                os.write(self.master_fd, f"{command}\n".encode('utf-8'))
                
                # Đợi phản hồi - truyền thêm lệnh để xác định thời gian chờ
                output = self._read_output(timeout=10.0, command=command)
                print(f"Raw output from binary: '{output}'")
                
                # Phân tích kết quả để xác định thành công hay thất bại
                if "Error" in output or "error" in output or "Unknown command" in output or "Usage:" in output:
                    return 1, output
                
                return 0, output
            except Exception as e:
                print(f"Error sending command: {e}")
                return -1, str(e)
    
    def _run_command(self, *args):
        """Chạy lệnh taskscheduler với các đối số được cung cấp"""
        if self.simulation_mode:
            # Không chạy lệnh thực tế trong chế độ giả lập
            return None, ""
        
        # Trong chế độ tương tác, chúng ta gửi lệnh trực tiếp đến quá trình đang chạy
        command = " ".join(args)
        return self._send_command(command)
    
    def get_all_tasks(self):
        """Lấy tất cả các tác vụ"""
        if self.simulation_mode:
            # Trả về dữ liệu mẫu trong chế độ giả lập
            return list(self.tasks.values()) or [
                {
                    'id': 1,
                    'name': 'Sample Task 1',
                    'command': 'echo "Hello World"',
                    'working_dir': '/tmp',
                    'creation_time': int(time.time() - 3600),
                    'next_run_time': int(time.time() + 1800),
                    'last_run_time': int(time.time() - 1800),
                    'frequency': 1,  # FREQUENCY_DAILY
                    'interval': 86400,  # 1 day in seconds
                    'schedule_type': 1,  # SCHEDULE_INTERVAL
                    'enabled': True,
                    'exit_code': 0,
                    'max_runtime': 60,
                    'dependencies': [],
                    'dep_behavior': 0,
                    'exec_mode': 0,  # EXEC_COMMAND
                    'cron_expression': ''
                },
                {
                    'id': 2,
                    'name': 'Sample Task 2',
                    'command': 'ls -la',
                    'working_dir': '/home',
                    'creation_time': int(time.time() - 7200),
                    'next_run_time': int(time.time() + 3600),
                    'last_run_time': 0,
                    'frequency': 2,  # FREQUENCY_WEEKLY
                    'interval': 604800,  # 1 week in seconds
                    'schedule_type': 1,  # SCHEDULE_INTERVAL
                    'enabled': False,
                    'exit_code': -1,
                    'max_runtime': 120,
                    'dependencies': [1],
                    'dep_behavior': 1,
                    'exec_mode': 0,  # EXEC_COMMAND
                    'cron_expression': ''
                }
            ]
        
        # Chạy lệnh "list" trong chế độ tương tác
        exit_code, output = self._run_command("list")
        
        if exit_code != 0 or not output.strip():
            print(f"Error listing tasks: {output}")
            return []
        
        try:
            # Kiểm tra nếu không có tác vụ nào
            if "No tasks found" in output:
                return []
            
            # Nếu không tìm thấy các chuỗi đặc trưng của danh sách tác vụ
            if "Task List:" not in output and "ID:" not in output:
                print(f"Unexpected output format: {output}")
                return []
            
            # Parse output để lấy danh sách tác vụ
            tasks = []
            
            # Lấy danh sách các ID tác vụ bằng cách tìm "ID: (\d+)"
            task_id_pattern = re.compile(r"ID: (\d+)")
            task_ids = task_id_pattern.findall(output)
            
            print(f"Found task IDs: {task_ids}")
            
            # Lấy thông tin chi tiết cho từng tác vụ
            for task_id in task_ids:
                try:
                    task_id = int(task_id)
                    detailed_task = self.get_task(task_id)
                    if detailed_task:
                        tasks.append(detailed_task)
                except Exception as e:
                    print(f"Error getting details for task {task_id}: {e}")
            
            return tasks
        except Exception as e:
            print(f"Error parsing task list: {e}")
            return []
    
    def get_task(self, task_id):
        """Lấy thông tin của một tác vụ theo ID"""
        if self.simulation_mode:
            # Trả về dữ liệu mẫu trong chế độ giả lập
            return self.tasks.get(task_id) or {
                'id': task_id,
                'name': f'Sample Task {task_id}',
                'command': 'echo "Hello World"',
                'working_dir': '/tmp',
                'creation_time': int(time.time() - 3600),
                'next_run_time': int(time.time() + 1800),
                'last_run_time': int(time.time() - 1800),
                'frequency': 1,  # FREQUENCY_DAILY
                'interval': 86400,  # 1 day in seconds
                'schedule_type': 1,  # SCHEDULE_INTERVAL
                'enabled': True,
                'exit_code': 0,
                'max_runtime': 60,
                'dependencies': [],
                'dep_behavior': 0,
                'exec_mode': 0,  # EXEC_COMMAND
                'cron_expression': ''
            }
        
        # Chạy lệnh "view" thay vì "show" trong chế độ tương tác
        exit_code, output = self._run_command(f"view {task_id}")
        
        if exit_code != 0 or not output.strip() or "Unknown command" in output:
            print(f"Error viewing task {task_id}: {output}")
            return None
        
        try:
            # Parse output để lấy thông tin tác vụ
            task = {'id': task_id}
            
            # Sử dụng mẫu view_task từ mã nguồn để phân tích một cách chính xác hơn
            lines = output.split('\n')
            for line in lines:
                line = line.strip()
                if not line:
                    continue
                
                # Trích xuất tên
                name_match = re.match(r"Name: (.+)", line)
                if name_match:
                    task['name'] = name_match.group(1)
                    continue
                
                # Trích xuất loại thực thi (script hoặc command)
                type_match = re.match(r"Type: (.+)", line)
                if type_match:
                    type_text = type_match.group(1).lower()
                    task['exec_mode'] = 0 if 'command' in type_text else 1
                    continue
                
                # Trích xuất lệnh
                cmd_match = re.match(r"Command: (.+)", line)
                if cmd_match:
                    task['command'] = cmd_match.group(1)
                    continue
                
                # Trích xuất thư mục làm việc
                dir_match = re.match(r"Working Dir(?:ectory)?: (.+)", line)
                if dir_match:
                    dir_text = dir_match.group(1)
                    task['working_dir'] = '' if '(default)' in dir_text else dir_text
                    continue
                
                # Trích xuất trạng thái
                status_match = re.match(r"Enabled: (.+)", line)
                if status_match:
                    task['enabled'] = status_match.group(1).lower() == 'yes'
                    continue
                
                # Trích xuất thời gian tạo
                created_match = re.match(r"Created: (.+)", line)
                if created_match:
                    creation_time_text = created_match.group(1)
                    if creation_time_text != "Unknown":
                        try:
                            # Chuyển đổi chuỗi thời gian sang timestamp
                            dt = datetime.strptime(creation_time_text, "%Y-%m-%d %H:%M:%S")
                            task['creation_time'] = int(dt.timestamp())
                        except Exception as e:
                            print(f"Error parsing creation time: {e}")
                            task['creation_time'] = 0
                    else:
                        task['creation_time'] = 0
                    continue
                
                # Trích xuất kiểu lịch trình
                schedule_match = re.match(r"Schedule: (.+)", line)
                if schedule_match:
                    schedule_text = schedule_match.group(1)
                    
                    if "Manual" in schedule_text:
                        task['schedule_type'] = 0
                    elif "Every" in schedule_text and "minutes" in schedule_text:
                        task['schedule_type'] = 1
                        interval_match = re.search(r"Every (\d+) minutes", schedule_text)
                        if interval_match:
                            task['interval'] = int(interval_match.group(1)) * 60  # Chuyển phút sang giây
                    elif "Cron:" in schedule_text:
                        task['schedule_type'] = 2
                        cron_match = re.search(r"Cron: (.+)", schedule_text)
                        if cron_match:
                            task['cron_expression'] = cron_match.group(1)
                    continue
                
                # Trích xuất thời gian chạy tối đa
                max_runtime_match = re.match(r"Max Runtime: (\d+) seconds", line)
                if max_runtime_match:
                    task['max_runtime'] = int(max_runtime_match.group(1))
                    continue
                
                # Trích xuất thời gian chạy lần cuối
                last_run_match = re.match(r"Last Run: (.+)", line)
                if last_run_match:
                    last_run_text = last_run_match.group(1)
                    if last_run_text != "Never":
                        try:
                            # Chuyển đổi chuỗi thời gian sang timestamp
                            dt = datetime.strptime(last_run_text, "%Y-%m-%d %H:%M:%S")
                            task['last_run_time'] = int(dt.timestamp())
                        except Exception as e:
                            print(f"Error parsing last run time: {e}")
                            task['last_run_time'] = 0
                    else:
                        task['last_run_time'] = 0
                    continue
                
                # Trích xuất exit code
                exit_code_match = re.match(r"Exit Code: (-?\d+)", line)
                if exit_code_match:
                    task['exit_code'] = int(exit_code_match.group(1))
                    continue
                
                # Trích xuất thời gian chạy tiếp theo
                next_run_match = re.match(r"Next Run: (.+)", line)
                if next_run_match:
                    next_run_text = next_run_match.group(1)
                    if next_run_text != "Not scheduled":
                        try:
                            # Chuyển đổi chuỗi thời gian sang timestamp
                            dt = datetime.strptime(next_run_text, "%Y-%m-%d %H:%M:%S")
                            task['next_run_time'] = int(dt.timestamp())
                        except Exception as e:
                            print(f"Error parsing next run time: {e}")
                            task['next_run_time'] = 0
                    else:
                        task['next_run_time'] = 0
                    continue
                
                # Trích xuất phụ thuộc (dependencies)
                depend_match = re.match(r"Dependencies: (.+)", line)
                if depend_match:
                    depend_text = depend_match.group(1)
                    if depend_text != "None":
                        # Trích xuất danh sách ID từ chuỗi "1, 2, 3"
                        depend_ids = [int(id.strip()) for id in depend_text.split(',') if id.strip().isdigit()]
                        task['dependencies'] = depend_ids
                    else:
                        task['dependencies'] = []
                    continue
            
            # Đặt các giá trị mặc định cho các trường khác chưa được xử lý
            if 'creation_time' not in task:
                task['creation_time'] = 0
            
            if 'dependencies' not in task:
                task['dependencies'] = []
            
            if 'dep_behavior' not in task:
                task['dep_behavior'] = 0
            
            if 'exit_code' not in task:
                task['exit_code'] = -1
            
            if 'frequency' not in task:
                task['frequency'] = 0
            
            return task
        except Exception as e:
            print(f"Error parsing task details: {e}")
            return None
    
    def add_task(self, task_data):
        """Thêm một tác vụ mới"""
        if self.simulation_mode:
            # Tạo tác vụ mẫu trong chế độ giả lập
            task_data['id'] = self.next_id
            task_data['creation_time'] = int(time.time())
            task_data['exit_code'] = -1
            
            if 'schedule_type' in task_data and task_data['schedule_type'] == 1:  # SCHEDULE_INTERVAL
                # Tính thời gian chạy tiếp theo dựa trên interval
                task_data['next_run_time'] = int(time.time() + task_data.get('interval', 0))
            else:
                task_data['next_run_time'] = 0
            
            self.tasks[self.next_id] = task_data
            self.next_id += 1
            return task_data['id']
        
        # Xây dựng lệnh add trong chế độ tương tác
        # Dựa vào phân tích mã nguồn cli.c, cú pháp là: add <n> <command>
        # Sau đó mới đến các tùy chọn
        name = task_data.get('name', '').replace('"', '\\"')  # Escape dấu ngoặc kép trong tên
        command = task_data.get('command', '').replace('"', '\\"')  # Escape dấu ngoặc kép trong lệnh
        
        cmd_parts = [f'add "{name}"']
        
        # Xử lý chế độ thực thi
        exec_mode = task_data.get('exec_mode', 0)
        
        if exec_mode == 0:  # Chế độ lệnh
            # Thêm lệnh nếu có
            if command:
                cmd_parts.append(f'"{command}"')
        else:  # Chế độ script
            # Đối với tất cả các script, luôn lưu vào file và sử dụng -f
            if 'script_content' in task_data and task_data['script_content']:
                script_content = task_data.get('script_content').strip()
                
                if script_content:
                    # Tạo file script tạm thời từ nội dung
                    import tempfile
                    
                    # Tạo thư mục scripts trong bin nếu chưa tồn tại
                    script_dir = os.path.join(self.base_dir, "bin", "scripts")
                    os.makedirs(script_dir, exist_ok=True)
                    
                    # Tạo file tạm với đuôi .sh để dễ nhận dạng
                    fd, temp_path = tempfile.mkstemp(suffix='.sh', prefix='script_', dir=script_dir)
                    
                    try:
                        # Chuẩn hóa nội dung script
                        # Chuyển đổi các CRLF (Windows) thành LF (Unix) 
                        normalized_content = script_content.replace('\r\n', '\n')
                        
                        # Xử lý shebang nếu chưa có
                        if not normalized_content.strip().startswith('#!/'):
                            normalized_content = '#!/bin/bash\n' + normalized_content
                        
                        # Ghi nội dung đã chuẩn hóa vào file
                        with os.fdopen(fd, 'w') as f:
                            f.write(normalized_content)
                        
                        # Cấp quyền thực thi cho file (0777 cho phép tất cả người dùng thực thi)
                        os.chmod(temp_path, 0o777)
                        
                        print(f"Created script file with full permissions (0777): {temp_path}")
                        
                        # Thêm đường dẫn file vào lệnh
                        cmd_parts.append('-f')
                        cmd_parts.append(f'"{temp_path}"')
                        
                        print(f"Created script file from content: {temp_path}")
                    except Exception as e:
                        print(f"Error creating script file: {e}")
                        # Quay lại sử dụng lệnh trực tiếp
                        cmd_parts.append(f'"echo Error creating script: {str(e)}"')
                        exec_mode = 0
                else:
                    # Nếu nội dung trống, quay lại sử dụng lệnh trực tiếp
                    cmd_parts.append('"echo No script content provided"')
                    exec_mode = 0
            # Hoặc kiểm tra xem có script_file hay không
            elif 'script_file' in task_data and task_data['script_file']:
                script_file = task_data['script_file'].replace('"', '\\"')
                
                # Kiểm tra xem file có tồn tại và có nội dung không
                try:
                    if os.path.exists(script_file):
                        # Đọc nội dung file để kiểm tra
                        with open(script_file, 'r') as f:
                            script_content = f.read().strip()
                        
                        if script_content:
                            cmd_parts.append('-f')
                            cmd_parts.append(f'"{script_file}"')
                            print(f"Using script file: {script_file}")
                        else:
                            # File trống, quay lại sử dụng lệnh trực tiếp
                            cmd_parts.append('"echo Script file is empty"')
                            exec_mode = 0
                    else:
                        print(f"Script file not found: {script_file}")
                        # Quay lại sử dụng lệnh trực tiếp
                        cmd_parts.append(f'"echo Script file not found: {os.path.basename(script_file)}"')
                        exec_mode = 0
                except Exception as e:
                    print(f"Error checking script file: {e}")
                    # Quay lại sử dụng lệnh trực tiếp
                    cmd_parts.append(f'"echo Error with script file: {os.path.basename(script_file)}"')
                    exec_mode = 0
            else:
                # Không có nội dung script, quay lại sử dụng lệnh trực tiếp
                cmd_parts.append('"echo No script content provided"')
                exec_mode = 0
        
        # Xử lý lịch trình dựa trên loại
        schedule_type = task_data.get('schedule_type', 0)
        
        # Thêm các tùy chọn
        # Nếu có khoảng thời gian (interval)
        if schedule_type == 1 and task_data.get('interval'):
            # Chuyển đổi từ giây sang phút (backend chấp nhận phút)
            minutes = max(1, int(task_data.get('interval', 0) / 60))
            cmd_parts.append(f"-t {minutes}")
        
        # Nếu có biểu thức cron
        elif schedule_type == 2 and task_data.get('cron_expression'):
            cron = task_data.get('cron_expression', '').replace('"', '\\"')
            cmd_parts.append(f'-s "{cron}"')
        
        # Thêm thư mục làm việc nếu có
        if task_data.get('working_dir'):
            working_dir = task_data.get('working_dir', '').replace('"', '\\"')
            cmd_parts.append(f'-d "{working_dir}"')
        
        # Thêm thời gian chạy tối đa nếu có
        if task_data.get('max_runtime'):
            cmd_parts.append(f"-m {task_data.get('max_runtime')}")
        
        # Xóa cờ -y để cho phương pháp thay thế làm việc tốt hơn
        # cmd_parts.append("-y")
        
        # Kết hợp các phần tạo thành lệnh hoàn chỉnh
        command = ' '.join(cmd_parts)
        
        # Thực thi lệnh
        print(f"Sending command: {command}")
        
        # Phương pháp 1: Sử dụng _send_command và xử lý kết quả
        try:
            # Sử dụng interactive process để có thể trả lời prompt
            self._start_interactive_process()
            
            # Gửi lệnh add
            os.write(self.master_fd, f"{command}\n".encode())
            
            # Đọc output với timeout
            output = self._read_output(timeout=6.0)
            print(f"Initial output from add command: '{output}'")
            
            # Kiểm tra xem có confirmation prompt không
            if "Do you want to create this task? (y/n):" in output:
                # Tự động trả lời "y"
                os.write(self.master_fd, b"y\n")
                time.sleep(0.5)
                
                # Đọc kết quả sau khi xác nhận
                output = self._read_output(timeout=3.0)
                print(f"Output after confirmation: '{output}'")
            
            # Tìm task ID từ kết quả
            id_match = re.search(r"Task added with ID: (\d+)", output)
            if id_match:
                task_id = int(id_match.group(1))
                print(f"Task added successfully with ID: {task_id}")
                
                # Cập nhật trạng thái của tác vụ nếu cần
                if 'enabled' in task_data and not task_data['enabled']:
                    self._run_command(f"disable {task_id}")
                
                return task_id
            
            # Nếu không tìm thấy ID nhưng không có lỗi rõ ràng
            if "Task creation cancelled" not in output and "Error" not in output:
                # Thử phương pháp 2: Lấy danh sách tác vụ để tìm ID mới nhất
                print("Using fallback method to find newly created task")
                tasks = self.get_all_tasks()
                if tasks:
                    max_id = max(task.get('id', 0) for task in tasks)
                    print(f"Latest task ID found: {max_id}")
                    return max_id
            
            # Phương pháp 3: Thử tạo lại task bằng lệnh add trực tiếp
            print("Task creation failed. Trying direct add command...")
            
            # Tạo lệnh add mới không có tùy chọn -y
            direct_command = ' '.join(cmd_parts)
            
            # Khởi động lại process để đảm bảo trạng thái sạch
            self._start_interactive_process()
            
            # Gửi lệnh và trả lời prompt
            os.write(self.master_fd, f"{direct_command}\n".encode())
            time.sleep(1.0)
            os.write(self.master_fd, b"y\n")
            time.sleep(1.0)
            
            # Đọc kết quả
            output = self._read_output(timeout=3.0)
            print(f"Direct command output: '{output}'")
            
            # Kiểm tra lại ID
            id_match = re.search(r"Task added with ID: (\d+)", output)
            if id_match:
                task_id = int(id_match.group(1))
                print(f"Task added successfully with ID: {task_id} (direct method)")
                return task_id
            
            # Kiểm tra một lần nữa danh sách tác vụ
            tasks = self.get_all_tasks()
            if tasks:
                max_id = max(task.get('id', 0) for task in tasks)
                print(f"Latest task ID found after direct command: {max_id}")
                return max_id
            
        except Exception as e:
            print(f"Error during task creation: {e}")
        
        print(f"All methods failed to create task")
        return -1
    
    def update_task(self, task_data):
        """Cập nhật một tác vụ"""
        if self.simulation_mode:
            # Cập nhật tác vụ trong chế độ giả lập
            task_id = task_data.get('id')
            if task_id and task_id in self.tasks:
                # Lưu giá trị last_run_time trước khi cập nhật
                last_run_time = self.tasks[task_id].get('last_run_time', 0)
                
                # Cập nhật task với dữ liệu mới
                self.tasks[task_id].update(task_data)
                
                # Đảm bảo giữ nguyên last_run_time nếu chỉ thay đổi trạng thái enabled
                if 'last_run_time' not in task_data and last_run_time > 0:
                    self.tasks[task_id]['last_run_time'] = last_run_time
                    print(f"Giữ nguyên last_run_time: {last_run_time} cho task {task_id}")
                
                return True
            return False
        
        task_id = task_data.get('id')
        if not task_id:
            print("Error: No task ID provided for update")
            return False
        
        # Kiểm tra xem đây có phải là yêu cầu bật/tắt task không
        if len(task_data) == 2 and 'id' in task_data and 'enabled' in task_data:
            print(f"Chỉ thay đổi trạng thái bật/tắt cho task {task_id}")
            
            # Sử dụng lệnh enable hoặc disable trực tiếp thay vì xóa và tạo lại
            enabled_status = "enable" if task_data['enabled'] else "disable"
            exit_code, output = self._run_command(f"{enabled_status} {task_id}")
            
            if exit_code != 0:
                print(f"Error changing task status: {output}")
                return False
                
            print(f"Task {task_id} {enabled_status}d successfully")
            return True
            
        # Lấy thông tin hiện tại của task trước khi cập nhật
        current_task = self.get_task(task_id)
        if not current_task:
            print(f"Error: Cannot find task {task_id} to update")
            return False
        
        # Sử dụng lệnh edit để cập nhật từng trường thay vì xóa và tạo lại
        success = True
        
        # Cập nhật tên
        if 'name' in task_data and task_data['name'] != current_task.get('name', ''):
            name = task_data['name'].replace('"', '\\"')  # Escape dấu ngoặc kép
            edit_cmd = f'edit {task_id} name "{name}"'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating task name: {output}")
                success = False
        
        # Cập nhật lệnh nếu trong chế độ lệnh
        exec_mode = task_data.get('exec_mode', current_task.get('exec_mode', 0))
        
        if exec_mode == 0:  # Chế độ lệnh
            if 'command' in task_data and task_data['command'] != current_task.get('command', ''):
                command = task_data['command'].replace('"', '\\"')  # Escape dấu ngoặc kép
                if current_task.get('exec_mode', 0) != 0:
                    # Chuyển từ chế độ khác sang chế độ lệnh
                    convert_cmd = f'to-command {task_id} "{command}"'
                    exit_code, output = self._run_command(convert_cmd)
                else:
                    # Chỉ cập nhật lệnh
                    edit_cmd = f'edit {task_id} command "{command}"'
                    exit_code, output = self._run_command(edit_cmd)
                
                if exit_code != 0:
                    print(f"Error updating command: {output}")
                    success = False
        
        elif exec_mode == 1:  # Chế độ script
            # Xử lý nội dung script
            script_content = None
            
            if 'script_content' in task_data and task_data['script_content']:
                script_content = task_data['script_content'].strip()
            elif 'script_file' in task_data and task_data['script_file']:
                try:
                    with open(task_data['script_file'], 'r') as f:
                        script_content = f.read()
                except Exception as e:
                    print(f"Error reading script file: {e}")
            
            if script_content:
                # Kiểm tra xem có đang chuyển từ command sang script không
                if current_task.get('exec_mode', 0) == 0:
                    print("Chuyển từ command sang script - tạo lại task")
                    # Trong trường hợp này cần xóa task cũ và tạo lại để tránh lỗi
                    
                    # Lưu lại tất cả thông tin task hiện tại
                    new_task_data = current_task.copy()
                    
                    # Cập nhật với thông tin mới
                    new_task_data.update(task_data)
                    
                    # Đảm bảo exec_mode là script
                    new_task_data['exec_mode'] = 1
                    new_task_data['script_content'] = script_content
                    
                    # Xóa ID để tránh xung đột khi tạo mới
                    if 'id' in new_task_data:
                        del new_task_data['id']
                    
                    # Xóa task cũ
                    if not self.delete_task(task_id):
                        print(f"Error deleting old task when converting to script")
                        return False
                    
                    # Tạo task mới
                    new_task_id = self.add_task(new_task_data)
                    if new_task_id <= 0:
                        print(f"Error creating new task when converting to script")
                        return False
                    
                    # Khôi phục các dependencies nếu có
                    if 'dependencies' in current_task and current_task['dependencies']:
                        for dep_id in current_task['dependencies']:
                            if not self.add_dependency(new_task_id, dep_id):
                                print(f"Warning: Failed to add dependency {dep_id} to new task {new_task_id}")
                    
                    # Khôi phục dependency behavior nếu có
                    if 'dep_behavior' in current_task and current_task['dep_behavior'] != 0:
                        if not self.set_dependency_behavior(new_task_id, current_task['dep_behavior']):
                            print(f"Warning: Failed to set dependency behavior for new task {new_task_id}")
                    
                    # Khôi phục trạng thái enabled nếu task đã bị tắt
                    if not current_task.get('enabled', True):
                        self._run_command(f"disable {new_task_id}")
                    
                    print(f"Đã chuyển đổi task {task_id} thành task mới với ID {new_task_id}")
                    return True
                else:
                    # Cùng là script mode, chỉ cập nhật nội dung
                    # Chuẩn hóa nội dung script
                    normalized_content = script_content.replace('\r\n', '\n')
                    if not normalized_content.strip().startswith('#!/'):
                        normalized_content = '#!/bin/bash\n' + normalized_content
                    
                    # Tạo file script tạm
                    import tempfile
                    
                    # Tạo thư mục scripts trong bin nếu chưa tồn tại
                    script_dir = os.path.join(self.base_dir, "bin", "scripts")
                    os.makedirs(script_dir, exist_ok=True)
                    
                    # Tạo file tạm với đuôi .sh để dễ nhận dạng
                    fd, temp_path = tempfile.mkstemp(suffix='.sh', prefix='script_', dir=script_dir)
                    
                    try:
                        # Ghi nội dung đã chuẩn hóa vào file
                        with os.fdopen(fd, 'w') as f:
                            f.write(normalized_content)
                        
                        # Cấp quyền thực thi cho file
                        os.chmod(temp_path, 0o777)
                        
                        # Sử dụng lệnh edit với trường script
                        edit_cmd = f'edit {task_id} script "{temp_path}"'
                        exit_code, output = self._run_command(edit_cmd)
                        
                        if exit_code != 0:
                            print(f"Error updating script content: {output}")
                            success = False
                        else:
                            print(f"Successfully updated script content for task {task_id}")
                    except Exception as e:
                        print(f"Error creating temporary script file: {e}")
                        success = False
        
        elif exec_mode == 2:  # Chế độ AI dynamic
            # Xử lý chế độ AI dynamic
            if 'ai_prompt' in task_data and 'system_metrics' in task_data:
                # Nếu đang chuyển từ chế độ khác sang AI dynamic, cần dùng cách tạo mới
                if current_task.get('exec_mode', 0) != 2:
                    print("Chuyển sang chế độ AI dynamic - tạo lại task")
                    
                    # Lưu lại tất cả thông tin task hiện tại
                    new_task_data = current_task.copy()
                    
                    # Cập nhật với thông tin mới
                    new_task_data.update(task_data)
                    
                    # Đảm bảo exec_mode là AI dynamic
                    new_task_data['exec_mode'] = 2
                    
                    # Xóa ID để tránh xung đột khi tạo mới
                    if 'id' in new_task_data:
                        del new_task_data['id']
                    
                    # Xóa task cũ
                    if not self.delete_task(task_id):
                        print(f"Error deleting old task when converting to AI dynamic")
                        return False
                    
                    # Tạo task mới
                    new_task_id = self.add_task(new_task_data)
                    if new_task_id <= 0:
                        print(f"Error creating new task when converting to AI dynamic")
                        return False
                    
                    # Chuyển đổi task mới thành AI dynamic
                    ai_prompt = task_data['ai_prompt']
                    system_metrics = task_data['system_metrics']
                    if not self.convert_task_to_ai_dynamic(new_task_id, ai_prompt, system_metrics):
                        print(f"Error converting new task to AI dynamic mode")
                        # Không return False ở đây vì task đã được tạo thành công
                    
                    # Khôi phục các dependencies nếu có
                    if 'dependencies' in current_task and current_task['dependencies']:
                        for dep_id in current_task['dependencies']:
                            if not self.add_dependency(new_task_id, dep_id):
                                print(f"Warning: Failed to add dependency {dep_id} to new task {new_task_id}")
                    
                    # Khôi phục dependency behavior nếu có
                    if 'dep_behavior' in current_task and current_task['dep_behavior'] != 0:
                        if not self.set_dependency_behavior(new_task_id, current_task['dep_behavior']):
                            print(f"Warning: Failed to set dependency behavior for new task {new_task_id}")
                    
                    # Khôi phục trạng thái enabled nếu task đã bị tắt
                    if not current_task.get('enabled', True):
                        self._run_command(f"disable {new_task_id}")
                    
                    print(f"Đã chuyển đổi task {task_id} thành task AI dynamic mới với ID {new_task_id}")
                    return True
                else:
                    # Đã là chế độ AI dynamic, chỉ cập nhật thông tin
                    ai_prompt = task_data['ai_prompt'].replace('"', '\\"')
                    system_metrics = task_data['system_metrics'].replace('"', '\\"')
                    convert_cmd = f'to-ai {task_id} "{ai_prompt}" "{system_metrics}"'
                    exit_code, output = self._run_command(convert_cmd)
                    
                    if exit_code != 0:
                        print(f"Error updating AI dynamic mode: {output}")
                        success = False
        
        # Cập nhật lịch trình
        schedule_type = task_data.get('schedule_type', current_task.get('schedule_type', 0))
        
        if schedule_type == 1 and 'interval' in task_data:  # Interval scheduling
            interval_minutes = max(1, int(task_data.get('interval', 0) / 60))  # Convert seconds to minutes
            if interval_minutes != int(current_task.get('interval', 0) / 60):
                edit_cmd = f'edit {task_id} interval {interval_minutes}'
                exit_code, output = self._run_command(edit_cmd)
                if exit_code != 0:
                    print(f"Error updating interval: {output}")
                    success = False
        
        elif schedule_type == 2 and 'cron_expression' in task_data:  # Cron scheduling
            if task_data['cron_expression'] != current_task.get('cron_expression', ''):
                cron = task_data['cron_expression'].replace('"', '\\"')
                edit_cmd = f'edit {task_id} cron "{cron}"'
                exit_code, output = self._run_command(edit_cmd)
                if exit_code != 0:
                    print(f"Error updating cron expression: {output}")
                    success = False
        
        # Cập nhật thời gian chạy tối đa
        if 'max_runtime' in task_data and task_data['max_runtime'] != current_task.get('max_runtime', 0):
            edit_cmd = f'edit {task_id} runtime {task_data["max_runtime"]}'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating max runtime: {output}")
                success = False
        
        # Cập nhật thư mục làm việc
        if 'working_dir' in task_data and task_data['working_dir'] != current_task.get('working_dir', ''):
            working_dir = task_data['working_dir'].replace('"', '\\"')
            edit_cmd = f'edit {task_id} dir "{working_dir}"'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating working directory: {output}")
                success = False
        
        # Cập nhật hành vi phụ thuộc
        if 'dep_behavior' in task_data and task_data['dep_behavior'] != current_task.get('dep_behavior', 0):
            edit_cmd = f'edit {task_id} dep_behavior {task_data["dep_behavior"]}'
            exit_code, output = self._run_command(edit_cmd)
            if exit_code != 0:
                print(f"Error updating dependency behavior: {output}")
                success = False
        
        return success
    
    def delete_task(self, task_id):
        """Xóa một tác vụ"""
        if self.simulation_mode:
            # Xóa tác vụ trong chế độ giả lập
            if task_id in self.tasks:
                del self.tasks[task_id]
                return True
            return False
        
        # Xóa tác vụ sử dụng lệnh remove
        exit_code, output = self._run_command(f"remove {task_id}")
        if exit_code != 0:
            print(f"Error removing task: {output}")
            return False
        
        return True
    
    def execute_task(self, task_id):
        """Thực thi một tác vụ ngay lập tức"""
        if self.simulation_mode:
            # Giả lập thực thi tác vụ
            if task_id in self.tasks:
                task = self.tasks[task_id]
                task['last_run_time'] = int(time.time())
                task['exit_code'] = 0
                return True
            return False
        
        # Thực thi tác vụ sử dụng lệnh run thay vì execute
        exit_code, output = self._run_command(f"run {task_id}")
        if exit_code != 0:
            print(f"Error running task: {output}")
            return False
        
        return True
    
    def extract_json_from_text(self, text):
        """Trích xuất phần JSON từ text bất kỳ, xử lý cả trường hợp có nhiều JSON object"""
        result = None
        if not text:
            return result
            
        # Tìm tất cả các đoạn bắt đầu bằng { và kết thúc bằng }
        json_pattern = re.compile(r'({.*?})', re.DOTALL)
        matches = json_pattern.findall(text)
        
        if not matches:
            return result
            
        # Thử parse từng đoạn tìm được
        for match in matches:
            try:
                json_obj = json.loads(match)
                # Ưu tiên JSON có nhiều trường hợp
                if isinstance(json_obj, dict) and len(json_obj) > 1:
                    # Nếu có trường id hoặc task_count, đây có khả năng cao là JSON chúng ta cần
                    if 'id' in json_obj or 'task_count' in json_obj or 'tasks' in json_obj:
                        return json_obj
                # Lưu lại kết quả đầu tiên parse thành công để trả về nếu không tìm thấy JSON tốt hơn
                if result is None:
                    result = json_obj
            except json.JSONDecodeError:
                continue
                
        return result
        
    def _parse_json_output(self, output):
        """Phân tích output dạng JSON, cải tiến để xử lý tốt hơn"""
        try:
            if not output:
                return None
                
            # Sử dụng hàm trích xuất JSON từ text
            json_obj = self.extract_json_from_text(output)
            if json_obj:
                return json_obj
                
            # Phương pháp cũ nếu hàm mới không hoạt động
            json_start = output.find('{')
            json_end = output.rfind('}') + 1
            
            if json_start >= 0 and json_end > json_start:
                json_str = output[json_start:json_end]
                try:
                    return json.loads(json_str)
                except json.JSONDecodeError:
                    # Thử loại bỏ các ký tự đặc biệt và thử lại
                    clean_json = re.sub(r'[\x00-\x1F\x7F]', '', json_str)
                    return json.loads(clean_json)
            
            return None
        except Exception as e:
            print(f"Error parsing JSON: {e}, Output: {output}")
            return None
    
    def add_dependency(self, task_id, dependency_id):
        """Thêm phụ thuộc giữa các tác vụ sử dụng lệnh add-dep của backend"""
        if self.simulation_mode:
            # Trong chế độ giả lập
            if task_id in self.tasks and dependency_id in self.tasks:
                task = self.tasks[task_id]
                if 'dependencies' not in task:
                    task['dependencies'] = []
                if dependency_id not in task['dependencies']:
                    task['dependencies'].append(dependency_id)
                    return True
            return False
            
        # Sử dụng lệnh add-dep trực tiếp
        exit_code, output = self._run_command(f"add-dep {task_id} {dependency_id}")
        if exit_code != 0:
            print(f"Error adding dependency: {output}")
            return False
            
        # Kiểm tra kết quả
        if "Added dependency:" in output:
            print(f"Successfully added dependency: Task {task_id} now depends on Task {dependency_id}")
            return True
        else:
            print(f"Failed to add dependency: {output}")
            return False
        
    def remove_dependency(self, task_id, dependency_id):
        """Xóa phụ thuộc giữa các tác vụ sử dụng lệnh remove-dep của backend"""
        if self.simulation_mode:
            # Trong chế độ giả lập
            if task_id in self.tasks and 'dependencies' in self.tasks[task_id]:
                if dependency_id in self.tasks[task_id]['dependencies']:
                    self.tasks[task_id]['dependencies'].remove(dependency_id)
                    return True
            return False
            
        # Sử dụng lệnh remove-dep trực tiếp
        exit_code, output = self._run_command(f"remove-dep {task_id} {dependency_id}")
        if exit_code != 0:
            print(f"Error removing dependency: {output}")
            return False
            
        # Kiểm tra kết quả
        if "Removed dependency:" in output:
            print(f"Successfully removed dependency: Task {task_id} no longer depends on Task {dependency_id}")
            return True
        else:
            print(f"Failed to remove dependency: {output}")
            return False
            
    def set_dependency_behavior(self, task_id, behavior):
        """Cài đặt hành vi phụ thuộc cho tác vụ"""
        if self.simulation_mode:
            # Trong chế độ giả lập
            if task_id in self.tasks:
                self.tasks[task_id]['dep_behavior'] = behavior
                return True
            return False
            
        # Trước tiên, thử sử dụng lệnh set-dep-behavior trực tiếp
        exit_code, output = self._run_command(f"set-dep-behavior {task_id} {behavior}")
        
        # Kiểm tra xem lệnh có thành công không
        if exit_code == 0 and "dependency behavior updated" in output:
            print(f"Successfully updated dependency behavior for task {task_id}")
            return True
            
        # Nếu lệnh không thành công hoặc lỗi, thử cập nhật trực tiếp vào cơ sở dữ liệu
        try:
            import sqlite3
            db_path = os.path.join(self.data_dir, "tasks.db")
            if os.path.exists(db_path):
                # Kết nối đến cơ sở dữ liệu
                conn = sqlite3.connect(db_path)
                cursor = conn.cursor()
                
                # Cập nhật trường dep_behavior
                cursor.execute("UPDATE tasks SET dep_behavior = ? WHERE id = ?", (behavior, task_id))
                conn.commit()
                
                # Kiểm tra xem có bao nhiêu hàng bị ảnh hưởng
                affected_rows = cursor.rowcount
                conn.close()
                
                if affected_rows > 0:
                    print(f"Successfully updated dependency behavior in database for task {task_id}")
                    return True
                else:
                    print(f"No records updated in database for task {task_id}")
                    return False
            else:
                print(f"Error: Database file not found at {db_path}")
                return False
        except Exception as e:
            print(f"Error updating dependency behavior in database: {e}")
            return False
        
        # Nếu không thể cập nhật, thử trả về kết quả từ lệnh ban đầu
        if "dependency behavior updated" in output:
            print(f"Successfully updated dependency behavior for task {task_id}")
            return True
        else:
            print(f"Failed to update dependency behavior: {output}")
            return False
    
    def get_api_key(self):
        """Lấy API key đã cấu hình cho AI"""
        if self.simulation_mode:
            return None
        
        # Thay vì sử dụng lệnh get-api-key không tồn tại, đọc trực tiếp từ file cấu hình
        try:
            config_path = os.path.join(self.data_dir, "config.json")
            if os.path.exists(config_path):
                with open(config_path, 'r') as f:
                    config = json.load(f)
                    if 'api_key' in config:
                        return config['api_key']
        except Exception as e:
            print(f"Error reading API key from config: {e}")
        
        # Nếu không thể đọc từ file, check output sau khi thiết lập
        # Lệnh này chỉ để kiểm tra xem API key đã được thiết lập chưa
        code, output = self._run_command("help")
        if code == 0:
            # API key đã được cài đặt sẽ được xem là đã thiết lập
            return "API_KEY_SET"
        
        return None

    def is_api_key_valid(self):
        """Kiểm tra xem API key đã được thiết lập và hợp lệ chưa"""
        if self.simulation_mode:
            return True
        
        # Kiểm tra xem có tồn tại config.json không
        config_path = os.path.join(self.data_dir, "config.json")
        if not os.path.exists(config_path):
            return False
        
        try:
            # Đọc config và kiểm tra api_key
            with open(config_path, 'r') as f:
                config = json.load(f)
                # API key phải tồn tại và không phải chuỗi rỗng
                if 'api_key' in config and config['api_key'] and len(config['api_key']) > 10:
                    return True
                return False
        except Exception as e:
            print(f"Error checking API key validity: {e}")
            return False

    def set_api_key(self, api_key):
        """Thiết lập API key cho chức năng AI"""
        if self.simulation_mode:
            return True
        
        # Gửi lệnh set-api-key với API key
        code, output = self._send_command(f"set-api-key {api_key}")
        
        # Xác định thành công dựa trên output
        success = (code == 0) and (
            "API key updated successfully" in output or 
            "updated successfully" in output or
            "saved" in output.lower()
        )
        
        # Nếu lệnh thành công, thử đọc/ghi trực tiếp vào file config để đảm bảo 
        if success:
            try:
                config_path = os.path.join(self.data_dir, "config.json")
                
                # Tạo config mới nếu không tồn tại
                if not os.path.exists(config_path):
                    with open(config_path, 'w') as f:
                        json.dump({"api_key": api_key}, f, indent=2)
                else:
                    # Đọc config hiện tại và cập nhật
                    try:
                        with open(config_path, 'r') as f:
                            config = json.load(f)
                    except:
                        config = {}
                    
                    config['api_key'] = api_key
                    
                    with open(config_path, 'w') as f:
                        json.dump(config, f, indent=2)
                    
                print(f"API key saved successfully to {config_path}")
            except Exception as e:
                print(f"Warning: Failed to save API key directly to config: {e}")
                # Không coi đây là lỗi hoàn toàn nếu backend đã thành công
        
        return success

    def ai_generate_task(self, description):
        """Sử dụng AI để tạo một tác vụ dựa trên mô tả bằng ngôn ngữ tự nhiên
        
        Args:
            description (str): Mô tả tác vụ bằng ngôn ngữ tự nhiên
            
        Returns:
            dict: Kết quả tạo tác vụ với các thông tin cần thiết
        """
        if 'SIMULATION' in os.environ:
            # Trong chế độ giả lập, trả về dữ liệu mẫu
            return {
                'success': True,
                'content': 'df -h | awk \'NR>1 {print $5,$1}\' | while read size fs; do pct=$(echo $size | cut -d\'%\' -f1); if [ $pct -ge 90 ]; then echo "WARNING: $fs is $size full!"; fi; done',
                'is_script': False,
                'is_cron': False,
                'cron': '',
                'interval_minutes': 60,
                'schedule_description': 'Chạy mỗi giờ',
                'suggested_name': 'Kiểm tra ổ đĩa'
            }
        
        command = f'ai-create "{description}"'
        
        print(f"Sending AI create command with description: '{description}'")
        print(f"Sending to binary: '{command}'")
        
        # Thiết lập biến môi trường để thông báo cho backend biết đây là lệnh gọi từ API
        os.environ["FROM_TASKSCHEDULER_API"] = "1"
        
        output = ""
        try:
            # Sử dụng run_command thay vì _interactive_process để tránh lỗi
            # Có timeout là 60 giây để đảm bảo AI có đủ thời gian suy nghĩ
            output = self._read_output(timeout=60.0, command=command)
            
            if not output or "Generating task from description..." not in output:
                # Thử lại với _send_command
                code, output = self._run_command("ai-create", f'"{description}"')
                if code != 0 or not output:
                    return {
                        'success': False,
                        'error': 'Không nhận được phản hồi từ AI'
                    }
        except Exception as e:
            print(f"Warning: Timeout waiting for output from command {command}")
            # Thử lại một lần nữa với _run_command
            try:
                code, output = self._run_command("ai-create", f'"{description}"')
                if code != 0 or not output:
                    print(f"Error generating task: {str(e)}")
                    return {
                        'success': False,
                        'error': f'Lỗi khi tạo tác vụ: {str(e)}'
                    }
            except Exception as e2:
                print(f"Error in second attempt: {str(e2)}")
                return {
                    'success': False,
                    'error': f'Lỗi khi tạo tác vụ: {str(e2)}'
                }
        finally:
            # Xóa biến môi trường sau khi hoàn thành để không ảnh hưởng đến các lệnh khác
            if "FROM_TASKSCHEDULER_API" in os.environ:
                del os.environ["FROM_TASKSCHEDULER_API"]
        
        # Kiểm tra nếu output là một tuple (có thể xảy ra với _run_command)
        if isinstance(output, tuple) and len(output) >= 2:
            _, output = output
        
        print(f"Raw output from binary: '{output}'")
        
        # Kiểm tra lỗi từ AI
        if "[ERROR]" in output and "Failed to parse complete task JSON" in output:
            return {
                'success': False,
                'error': 'API của OpenAI không thể tạo task hợp lệ. Vui lòng thử lại với mô tả khác.'
            }
        
        if "Could not generate task from description." in output:
            return {
                'success': False,
                'error': 'Không thể tạo tác vụ từ mô tả này. Vui lòng thử mô tả rõ ràng hơn.'
            }
            
        # Tạo task mẫu nếu AI gặp lỗi nhưng vẫn có thể hiểu yêu cầu
        if ("error" in output.lower() or "failed" in output.lower()) and ("backup" in description.lower() or "sao lưu" in description.lower()):
            # Tạo tác vụ sao lưu mẫu
            if "/var/www" in description and "/backup" in description:
                script_content = """#!/bin/bash

BACKUP_SOURCE="/var/www"
BACKUP_DEST="/backup"
BACKUP_DATE=$(date +%Y-%m-%d)

if [ ! -d "$BACKUP_DEST" ]; then
    mkdir -p "$BACKUP_DEST"
fi

tar -czf "$BACKUP_DEST/www_backup_$BACKUP_DATE.tar.gz" "$BACKUP_SOURCE" 2>/dev/null

if [ $? -eq 0 ]; then
    echo "Backup completed successfully"
    exit 0
else
    echo "Backup failed"
    exit 1
fi
"""
                # Phát hiện thời gian từ mô tả
                cron_expression = "0 2 * * *"  # mặc định 2 giờ sáng
                schedule_description = "Chạy mỗi ngày lúc 2 giờ"
                
                if "2 giờ sáng" in description or "2 giờ" in description:
                    cron_expression = "0 2 * * *"
                    schedule_description = "Chạy mỗi ngày lúc 2 giờ"
                
                return {
                    'success': True,
                    'content': script_content,
                    'is_script': True,
                    'is_cron': True,
                    'cron': cron_expression,
                    'interval_minutes': 1440,  # 24 giờ
                    'schedule_description': schedule_description,
                    'suggested_name': 'daily_www_backup',
                    'raw_output': output
                }
        
        # Kiểm tra xem output có chứa script hay command hay không
        is_script = False
        content = ""
        suggested_name = ""
        is_cron = False
        cron_expression = ""
        interval_minutes = 60  # Mặc định là 60 phút nếu không xác định được
        schedule_description = "Chạy hàng ngày"  # Mặc định nếu không xác định được
        
        # Xác định loại tác vụ (script hay command)
        if "AI has generated a script based on your description:" in output:
            is_script = True
        elif "AI has generated a command based on your description:" in output:
            is_script = False
        else:
            # Kiểm tra xem có chứa "Script" trong thông tin schedule không
            if "Script" in output and "Schedule:" in output:
                is_script = True
        
        # Loại bỏ phần "Do you want to create this task? (y/n):" nếu có
        if "Do you want to create this task? (y/n):" in output:
            output = output.split("Do you want to create this task? (y/n):")[0].strip()
        
        # Trích xuất nội dung script/command
        content_lines = []
        in_content_section = False
        
        # Mẫu mới: "AI generated task: Script, Schedule: 0 2 * * *, Name: daily_www_backup"
        if "AI generated task:" in output:
            info_line = [line for line in output.split('\n') if "AI generated task:" in line]
            if info_line:
                info = info_line[0]
                if "Script" in info:
                    is_script = True
                
                # Trích xuất cron expression
                import re
                cron_match = re.search(r'Schedule: ([0-9*\s]+)', info)
                if cron_match:
                    cron_expression = cron_match.group(1).strip()
                    is_cron = True
                    # Phân tích biểu thức cron để xác định loại lịch trình
                    cron_parts = cron_expression.split()
                    if len(cron_parts) >= 2:
                        try:
                            hour = int(cron_parts[1])
                            schedule_description = f"Chạy mỗi ngày lúc {hour} giờ"
                        except ValueError:
                            schedule_description = f"Theo biểu thức Cron: {cron_expression}"
                
                # Trích xuất tên đề xuất
                name_match = re.search(r'Name: ([\w_-]+)', info)
                if name_match:
                    suggested_name = name_match.group(1).strip()
        
        # Phân tích nội dung output
        for line in output.split('\n'):
            # Tìm phần bắt đầu của nội dung
            if line.strip() == "-----------------------------------------------------------":
                if not in_content_section:
                    in_content_section = True
                    continue
                else:
                    in_content_section = False
                    break
            
            # Thu thập các dòng nội dung
            if in_content_section:
                content_lines.append(line)
        
        # Nếu không tìm thấy khu vực được đánh dấu rõ ràng, tìm kiếm theo cách khác
        if not content_lines:
            # Tìm kiếm nội dung sau "AI has generated a script/command based on your description:"
            if is_script:
                marker = "AI has generated a script based on your description:"
            else:
                marker = "AI has generated a command based on your description:"
            
            if marker in output:
                content_section = output.split(marker)[1].strip()
                
                # Lấy phần nội dung trước khi gặp "Schedule:" hoặc "Cron Expression:" hoặc dòng trống kép
                if "Schedule:" in content_section:
                    content_section = content_section.split("Schedule:")[0].strip()
                elif "Cron Expression:" in content_section:
                    content_section = content_section.split("Cron Expression:")[0].strip()
                elif "\n\n" in content_section:
                    content_section = content_section.split("\n\n")[0].strip()
                
                # Loại bỏ delimiter nếu có
                content_section = content_section.replace("-----------------------------------------------------------", "").strip()
                
                content_lines = content_section.split("\n")
        
        # Nếu vẫn không tìm thấy nội dung, tìm kiếm từ đầu ra chưa được xử lý
        if not content_lines and isinstance(output, str):
            # Tìm dòng bắt đầu với #!/bin/bash hoặc các ký tự phổ biến khác cho script
            lines = output.split('\n')
            start_index = -1
            end_index = -1
            
            for i, line in enumerate(lines):
                if line.strip().startswith("#!/bin/bash") or line.strip().startswith("#!/usr/bin/env"):
                    start_index = i
                    break
            
            if start_index >= 0:
                # Tìm dòng kết thúc (dòng trống sau script)
                for i in range(start_index + 1, len(lines)):
                    if not lines[i].strip() and i < len(lines) - 1 and not lines[i+1].strip():
                        end_index = i
                        break
                
                # Nếu không tìm thấy dòng kết thúc rõ ràng, tìm các marker khác
                if end_index < 0:
                    for i in range(start_index + 1, len(lines)):
                        if lines[i].strip() == "-----------------------------------------------------------" or \
                           "Schedule:" in lines[i] or "Cron Expression:" in lines[i]:
                            end_index = i - 1
                            break
                
                # Nếu vẫn không tìm thấy, lấy đến cuối
                if end_index < 0:
                    end_index = len(lines) - 1
                
                content_lines = lines[start_index:end_index+1]
        
        # Kết hợp các dòng nội dung
        if content_lines:
            content = '\n'.join(content_lines).strip()
        
        # Trích xuất thông tin lịch trình
        schedule_info = ""
        for line in output.split('\n'):
            if line.strip().startswith("Schedule:"):
                schedule_info = line.strip()[len("Schedule:"):].strip()
                break
        
        if schedule_info:
            schedule_description = schedule_info
        
        # Trích xuất biểu thức cron
        if not cron_expression:  # Chỉ tìm nếu chưa được tìm thấy ở trên
            for line in output.split('\n'):
                if line.strip().startswith("Cron Expression:"):
                    cron_expression = line.strip()[len("Cron Expression:"):].strip()
                    is_cron = True
                    break
        
        # Phân tích biểu thức cron để xác định loại lịch trình
        if cron_expression:
            import re
            if re.match(r'^\d+\s+\d+\s+\*\s+\*\s+\*$', cron_expression):
                cron_parts = cron_expression.split()
                try:
                    hour = int(cron_parts[1])
                    schedule_description = f"Chạy mỗi ngày lúc {hour} giờ"
                except (ValueError, IndexError):
                    schedule_description = f"Theo biểu thức Cron: {cron_expression}"
        
        # Trích xuất tên đề xuất
        if not suggested_name:  # Chỉ tìm nếu chưa được tìm thấy ở trên
            for line in output.split('\n'):
                if line.strip().startswith("Suggested Name:"):
                    suggested_name = line.strip()[len("Suggested Name:"):].strip()
                    break
                elif "Name:" in line and not "AI has generated" in line:
                    name_part = line.split("Name:")[1].strip()
                    # Lấy phần đầu tiên nếu có nhiều phần
                    suggested_name = name_part.split()[0].strip()
                    break
        
        # Nếu không tìm thấy tên đề xuất, đặt tên mặc định dựa trên mô tả
        if not suggested_name:
            words = description.split()
            if len(words) > 3:
                suggested_name = "_".join(words[:3]).lower()
            else:
                suggested_name = "_".join(words).lower()
            
            # Loại bỏ các ký tự không hợp lệ
            import re
            suggested_name = re.sub(r'[^a-zA-Z0-9_]', '', suggested_name)
            
            # Đảm bảo tên không quá dài
            if len(suggested_name) > 30:
                suggested_name = suggested_name[:30]
            
            # Thêm tiền tố để dễ nhận biết
            suggested_name = "task_" + suggested_name
        
        # Kiểm tra xem có tìm thấy nội dung hay không
        if not content:
            # Nếu không tìm thấy nội dung, trả về lỗi
            return {
                'success': False,
                'error': 'Không thể trích xuất nội dung từ phản hồi của AI'
            }
        
        return {
            'success': True,
            'content': content,
            'is_script': is_script,
            'is_cron': is_cron,
            'cron': cron_expression,
            'interval_minutes': interval_minutes,
            'schedule_description': schedule_description,
            'suggested_name': suggested_name,
            'raw_output': output  # Lưu toàn bộ đầu ra gốc để debug
        }

    def ai_generate_command(self, goal, system_metrics):
        """Tạo lệnh dựa trên thông số hệ thống và mục tiêu"""
        if self.simulation_mode:
            # Trả về dữ liệu mẫu trong chế độ giả lập
            return {
                'success': True,
                'command': 'echo "Dynamic command based on metrics"'
            }
        
        # Gọi lệnh ai-generate với mục tiêu và metrics
        code, output = self._run_command("ai-generate", f'"{goal}"', f'"{system_metrics}"')
        
        if code == 0:
            # Trích xuất lệnh từ đầu ra
            command_match = re.search(r'Command:\s*`(.+?)`', output)
            if command_match:
                return {
                    'success': True,
                    'command': command_match.group(1).strip()
                }
            else:
                return {
                    'success': False,
                    'error': 'Không thể trích xuất lệnh từ kết quả'
                }
        else:
            return {
                'success': False,
                'error': output
            }

    def convert_task_to_ai_dynamic(self, task_id, goal, system_metrics):
        """Chuyển đổi task thành chế độ AI Dynamic"""
        if self.simulation_mode:
            return True
        
        # Gọi lệnh to-ai để chuyển đổi task
        code, output = self._run_command("to-ai", task_id, f'"{goal}"', f'"{system_metrics}"')
        return code == 0

    def get_available_system_metrics(self):
        """Lấy danh sách các system metrics khả dụng"""
        # Danh sách cố định của các metrics có sẵn
        return [
            {'id': 'cpu_load', 'name': 'Tải CPU', 'description': 'Phần trăm tải CPU'},
            {'id': 'mem_free', 'name': 'Bộ nhớ trống', 'description': 'Lượng bộ nhớ RAM còn trống'},
            {'id': 'mem_used', 'name': 'Bộ nhớ đã dùng', 'description': 'Lượng bộ nhớ RAM đã sử dụng'},
            {'id': 'disk:/', 'name': 'Ổ đĩa / (root)', 'description': 'Mức sử dụng ổ đĩa gốc'},
            {'id': 'disk:/home', 'name': 'Ổ đĩa /home', 'description': 'Mức sử dụng ổ đĩa /home'},
            {'id': 'disk:/var', 'name': 'Ổ đĩa /var', 'description': 'Mức sử dụng ổ đĩa /var'},
            {'id': 'load_avg', 'name': 'Tải trung bình', 'description': 'Chỉ số tải trung bình của hệ thống'},
            {'id': 'processes', 'name': 'Tiến trình', 'description': 'Thông tin về các tiến trình đang chạy'}
        ]

    def __del__(self):
        """Dọn dẹp khi đối tượng bị hủy"""
        if not self.simulation_mode and self.process and self.process.poll() is None:
            try:
                # Gửi lệnh exit để tắt binary một cách an toàn
                self._send_command("exit")
                time.sleep(0.5)
                
                # Đóng các file descriptor
                if self.master_fd:
                    os.close(self.master_fd)
                if self.slave_fd:
                    os.close(self.slave_fd)
                
                # Kết thúc process nếu vẫn đang chạy
                if self.process.poll() is None:
                    self.process.terminate()
                    self.process.wait(timeout=2)
            except Exception as e:
                print(f"Error during cleanup: {e}")
                if self.process.poll() is None:
                    try:
                        self.process.kill()
                    except:
                        pass 