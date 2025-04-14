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
    
    def _read_output(self, timeout=2.0):
        """Đọc đầu ra từ binary tương tác"""
        if self.simulation_mode or not self.master_fd:
            return ""
        
        output = ""
        end_time = time.time() + timeout
        
        while time.time() < end_time:
            try:
                # Kiểm tra xem có dữ liệu để đọc không
                readable, _, _ = select.select([self.master_fd], [], [], 0.1)
                if self.master_fd in readable:
                    chunk = os.read(self.master_fd, 4096).decode('utf-8', errors='replace')
                    if not chunk:  # Kết nối đã đóng
                        break
                    output += chunk
                    
                    # Kiểm tra xem có dấu nhắc cuối cùng không
                    if output.strip().endswith(">"):
                        break
                else:
                    # Không có dữ liệu sẵn sàng, chờ một chút
                    time.sleep(0.1)
            except Exception as e:
                print(f"Error reading from process: {e}")
                break
        
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
                
                # Đợi phản hồi
                output = self._read_output(timeout=5.0)
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
                dir_match = re.match(r"Working Dir: (.+)", line)
                if dir_match:
                    dir_text = dir_match.group(1)
                    task['working_dir'] = '' if '(default)' in dir_text else dir_text
                    continue
                
                # Trích xuất trạng thái
                status_match = re.match(r"Enabled: (.+)", line)
                if status_match:
                    task['enabled'] = status_match.group(1).lower() == 'yes'
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
            # Kiểm tra xem có script_file hay không
            if 'script_file' in task_data and task_data['script_file']:
                script_file = task_data['script_file'].replace('"', '\\"')
                cmd_parts.append('-f')
                cmd_parts.append(f'"{script_file}"')
            # Hoặc dùng script_content
            elif 'script_content' in task_data and task_data['script_content']:
                cmd_parts.append('--script')
                # Mã script cần được đóng gói đúng cách
                script_content = task_data['script_content'].replace('"', '\\"')
                cmd_parts.append(f'"{script_content}"')
        
        # Thêm các tùy chọn
        # Nếu có khoảng thời gian (interval)
        if task_data.get('schedule_type') == 1 and task_data.get('interval'):
            # Chuyển đổi từ giây sang phút (backend chấp nhận phút)
            minutes = max(1, int(task_data.get('interval', 0) / 60))
            cmd_parts.append(f"-t {minutes}")
        
        # Nếu có biểu thức cron
        elif task_data.get('schedule_type') == 2 and task_data.get('cron_expression'):
            cron = task_data.get('cron_expression', '').replace('"', '\\"')
            cmd_parts.append(f'-s "{cron}"')
        
        # Thêm thư mục làm việc nếu có
        if task_data.get('working_dir'):
            working_dir = task_data.get('working_dir', '').replace('"', '\\"')
            cmd_parts.append(f'-d "{working_dir}"')
        
        # Thêm thời gian chạy tối đa nếu có
        if task_data.get('max_runtime'):
            cmd_parts.append(f"-m {task_data.get('max_runtime')}")
        
        # Kết hợp các phần tạo thành lệnh hoàn chỉnh
        command = ' '.join(cmd_parts)
        
        # Thực thi lệnh
        print(f"Sending command: {command}")
        exit_code, output = self._run_command(command)
        
        if exit_code != 0:
            print(f"Error adding task: {output}")
            return -1
        
        # Phân tích output để lấy ID tác vụ
        id_match = re.search(r"Task added with ID: (\d+)", output)
        if id_match:
            task_id = int(id_match.group(1))
            
            # Cập nhật trạng thái của tác vụ nếu cần
            if 'enabled' in task_data and not task_data['enabled']:
                self._run_command(f"disable {task_id}")
            
            return task_id
        else:
            print(f"Could not determine task ID from output: {output}")
            return -1
    
    def update_task(self, task_data):
        """Cập nhật một tác vụ"""
        if self.simulation_mode:
            # Cập nhật tác vụ trong chế độ giả lập
            task_id = task_data.get('id')
            if task_id and task_id in self.tasks:
                self.tasks[task_id].update(task_data)
                return True
            return False
        
        task_id = task_data.get('id')
        if not task_id:
            print("Error: No task ID provided for update")
            return False
        
        # Trong C backend, sử dụng lệnh remove và sau đó add lại
        # Đầu tiên, xóa tác vụ cũ
        exit_code, output = self._run_command(f"remove {task_id}")
        if exit_code != 0:
            print(f"Error removing old task: {output}")
            return False
        
        # Chuẩn bị lệnh thêm tác vụ mới
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
            # Kiểm tra xem có script_file hay không
            if 'script_file' in task_data and task_data['script_file']:
                script_file = task_data['script_file'].replace('"', '\\"')
                cmd_parts.append('-f')
                cmd_parts.append(f'"{script_file}"')
            # Hoặc dùng script_content
            elif 'script_content' in task_data and task_data['script_content']:
                cmd_parts.append('--script')
                # Mã script cần được đóng gói đúng cách
                script_content = task_data['script_content'].replace('"', '\\"')
                cmd_parts.append(f'"{script_content}"')
        
        # Thêm các tùy chọn
        # Nếu có khoảng thời gian (interval)
        if task_data.get('schedule_type') == 1 and task_data.get('interval'):
            # Chuyển đổi từ giây sang phút (backend chấp nhận phút)
            minutes = max(1, int(task_data.get('interval', 0) / 60))
            cmd_parts.append(f"-t {minutes}")
        
        # Nếu có biểu thức cron
        elif task_data.get('schedule_type') == 2 and task_data.get('cron_expression'):
            cron = task_data.get('cron_expression', '').replace('"', '\\"')
            cmd_parts.append(f'-s "{cron}"')
        
        # Thêm thư mục làm việc nếu có
        if task_data.get('working_dir'):
            working_dir = task_data.get('working_dir', '').replace('"', '\\"')
            cmd_parts.append(f'-d "{working_dir}"')
        
        # Thêm thời gian chạy tối đa nếu có
        if task_data.get('max_runtime'):
            cmd_parts.append(f"-m {task_data.get('max_runtime')}")
        
        # Kết hợp các phần tạo thành lệnh hoàn chỉnh
        command = ' '.join(cmd_parts)
        
        # Thực thi lệnh
        print(f"Sending update command: {command}")
        exit_code, output = self._run_command(command)
        if exit_code != 0:
            print(f"Error adding updated task: {output}")
            return False
        
        # Phân tích output để lấy ID tác vụ mới
        id_match = re.search(r"Task added with ID: (\d+)", output)
        if id_match:
            new_task_id = int(id_match.group(1))
            
            # Cập nhật trạng thái của tác vụ nếu cần
            if 'enabled' in task_data and not task_data['enabled']:
                self._run_command(f"disable {new_task_id}")
            
            print(f"Updated task {task_id} with new ID {new_task_id}")
            return True
        else:
            print(f"Could not determine new task ID from output: {output}")
            return False
    
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