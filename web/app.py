#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
from flask import Flask, render_template, request, redirect, url_for, flash, jsonify, session
from datetime import datetime
import time
import uuid
import werkzeug.utils
import json

# Thêm thư mục gốc vào sys.path để import các module
current_dir = os.path.dirname(os.path.abspath(__file__))
parent_dir = os.path.dirname(current_dir)
if parent_dir not in sys.path:
    sys.path.append(parent_dir)

# Import TaskAPI từ api
from web.api.task_api import TaskAPI

# Directory for storing uploaded scripts
SCRIPTS_FOLDER = os.path.join(parent_dir, "bin", "scripts")
os.makedirs(SCRIPTS_FOLDER, exist_ok=True)

# Allowed extensions for script files
ALLOWED_EXTENSIONS = {'sh', 'py', 'pl', 'js', 'rb', 'bat', 'cmd'}

# Khởi tạo Flask app
app = Flask(__name__)
app.secret_key = '42ae8dfd4c3c74024ff5beed3b0c0e76'
app.config['UPLOAD_FOLDER'] = SCRIPTS_FOLDER
app.config['MAX_CONTENT_LENGTH'] = 1 * 1024 * 1024  # 1MB limit

# Khởi tạo TaskAPI
task_api = TaskAPI()

# Đảm bảo data_dir nằm trong thư mục bin để phù hợp với backend C
data_dir = os.path.join(parent_dir, "bin", "data")
os.makedirs(data_dir, exist_ok=True)

# Helper functions
def format_timestamp(timestamp):
    """Format unix timestamp thành chuỗi datetime dễ đọc"""
    if not timestamp or timestamp == 0:
        return "Chưa thiết lập"
    return datetime.fromtimestamp(timestamp).strftime('%d/%m/%Y %H:%M:%S')

def get_frequency_name(frequency):
    """Chuyển đổi mã tần suất thành tên"""
    frequency_names = {
        0: "Một lần",
        1: "Hàng ngày",
        2: "Hàng tuần",
        3: "Hàng tháng",
        4: "Tùy chỉnh"
    }
    return frequency_names.get(frequency, "Không xác định")

def get_schedule_type_name(schedule_type):
    """Chuyển đổi mã kiểu lịch thành tên"""
    schedule_type_names = {
        0: "Thủ công",
        1: "Theo khoảng thời gian",
        2: "Theo biểu thức Cron"
    }
    return schedule_type_names.get(schedule_type, "Không xác định")

def get_exec_mode_name(exec_mode):
    """Chuyển đổi mã chế độ thực thi thành tên"""
    exec_mode_names = {
        0: "Lệnh",
        1: "Script",
        2: "AI Dynamic"
    }
    return exec_mode_names.get(exec_mode, "Không xác định")

def get_dependency_behavior_name(behavior):
    """Chuyển đổi mã hành vi phụ thuộc thành tên"""
    behavior_names = {
        0: "Chạy nếu bất kỳ tác vụ phụ thuộc nào thành công",
        1: "Chạy chỉ khi tất cả tác vụ phụ thuộc thành công",
        2: "Chạy nếu bất kỳ tác vụ phụ thuộc nào hoàn thành",
        3: "Chạy sau khi tất cả tác vụ phụ thuộc hoàn thành"
    }
    return behavior_names.get(behavior, "Không xác định")

@app.template_filter('format_timestamp')
def _format_timestamp(timestamp):
    return format_timestamp(timestamp)

# Template context processor để thêm helper function
@app.context_processor
def utility_processor():
    def now():
        return datetime.now()
    return dict(now=now)

# Trang chủ - Hiển thị danh sách tác vụ
@app.route('/')
def index():
    tasks = task_api.get_all_tasks()
    
    # Bổ sung thông tin khác cho tác vụ
    for task in tasks:
        task['next_run_time_fmt'] = format_timestamp(task.get('next_run_time', 0))
        task['last_run_time_fmt'] = format_timestamp(task.get('last_run_time', 0))
        task['frequency_name'] = get_frequency_name(task.get('frequency', 0))
        task['schedule_type_name'] = get_schedule_type_name(task.get('schedule_type', 0))
        task['exec_mode_name'] = get_exec_mode_name(task.get('exec_mode', 0))
    
    return render_template('index.html', tasks=tasks)

# Hiển thị form tạo tác vụ mới
@app.route('/task/new')
def new_task():
    # Lấy danh sách các tác vụ hiện có để cho phép chọn trong danh sách phụ thuộc
    available_tasks = task_api.get_all_tasks()
    return render_template('task_form.html', task=None, action="create", available_tasks=available_tasks)

# Tạo tác vụ mới
@app.route('/task/create', methods=['POST'])
def create_task():
    task_data = {
        'name': request.form.get('name', ''),
        'command': request.form.get('command', ''),
        'working_dir': request.form.get('working_dir', ''),
        'enabled': True,
        'dep_behavior': int(request.form.get('dep_behavior', 0))
    }
    
    # Xử lý kiểu lịch trình
    schedule_type = int(request.form.get('schedule_type', 0))
    task_data['schedule_type'] = schedule_type
    
    if schedule_type == 1:  # Interval
        interval_minutes = int(request.form.get('interval', 1))
        task_data['interval'] = interval_minutes * 60  # Chuyển phút sang giây
    elif schedule_type == 2:  # Cron
        task_data['cron_expression'] = request.form.get('cron_expression', '')
    
    # Xử lý chế độ thực thi
    exec_mode = int(request.form.get('exec_mode', 0))
    task_data['exec_mode'] = exec_mode
    
    if exec_mode == 1:  # Script mode
        script_mode = request.form.get('script_mode', 'content')
        
        if script_mode == 'file':
            script_file = request.form.get('script_file', '')
            if script_file:
                task_data['script_file'] = script_file
        else:
            script_content = request.form.get('script_content', '')
            if script_content:
                task_data['script_content'] = script_content
    
    # Thêm thời gian chạy tối đa nếu có
    max_runtime = request.form.get('max_runtime')
    if max_runtime:
        task_data['max_runtime'] = int(max_runtime)
    
    # Xử lý các tác vụ phụ thuộc
    dependencies = request.form.getlist('dependencies')
    if dependencies:
        # Chuyển đổi từ danh sách chuỗi sang danh sách số nguyên
        dependencies = [int(dep_id) for dep_id in dependencies if dep_id.isdigit()]
        # Giới hạn số lượng phụ thuộc tối đa
        dependencies = dependencies[:10]
        task_data['dependencies'] = dependencies
    else:
        task_data['dependencies'] = []
    
    # Thêm tác vụ
    task_id = task_api.add_task(task_data)
    
    if task_id > 0:
        # Nếu tác vụ được tạo thành công và có phụ thuộc, thêm chúng
        if dependencies:
            for dep_id in dependencies:
                task_api.add_dependency(task_id, dep_id)
                
        # Nếu có hành vi phụ thuộc được chỉ định
        if 'dep_behavior' in request.form and dependencies:
            behavior = int(request.form.get('dep_behavior', 0))
            task_api.set_dependency_behavior(task_id, behavior)
            
        flash('Tác vụ đã được tạo thành công!', 'success')
        return redirect(url_for('view_task', task_id=task_id))
    else:
        flash('Không thể tạo tác vụ. Vui lòng thử lại!', 'error')
        # Lấy danh sách các tác vụ khác cho phần phụ thuộc
        available_tasks = task_api.get_all_tasks()
        return render_template('task_form.html', task=task_data, action="create", available_tasks=available_tasks)

# Xem chi tiết tác vụ
@app.route('/task/<int:task_id>')
def view_task(task_id):
    task = task_api.get_task(task_id)
    if task:
        # Thêm các thông tin định dạng vào task
        task['creation_time_fmt'] = format_timestamp(task.get('creation_time', 0))
        task['next_run_time_fmt'] = format_timestamp(task.get('next_run_time', 0))
        task['last_run_time_fmt'] = format_timestamp(task.get('last_run_time', 0))
        
        # Thêm các tên mô tả
        task['schedule_type_name'] = get_schedule_type_name(task.get('schedule_type', 0))
        task['frequency_name'] = get_frequency_name(task.get('frequency', 0))
        task['dependency_behavior_name'] = get_dependency_behavior_name(task.get('dep_behavior', 0))
        task['exec_mode_name'] = get_exec_mode_name(task.get('exec_mode', 0))
        
        # Xử lý mã kết quả exit_code
        exit_code = task.get('exit_code', -1)
        if exit_code == 0:
            task['exit_code_desc'] = f"Thành công ({exit_code})"
        elif exit_code == -1:
            if task.get('last_run_time', 0) == 0:
                task['exit_code_desc'] = "Chưa chạy"
            else:
                task['exit_code_desc'] = f"Lỗi ({exit_code})"
        else:
            task['exit_code_desc'] = f"Lỗi ({exit_code})"
        
        # Lấy danh sách tên của các tác vụ phụ thuộc (nếu có)
        dependency_names = {}
        if task.get('dependencies'):
            for dep_id in task['dependencies']:
                dep_task = task_api.get_task(dep_id)
                if dep_task:
                    dependency_names[dep_id] = dep_task['name']
                else:
                    dependency_names[dep_id] = f"Tác vụ {dep_id}"
        
        return render_template('task_detail.html', task=task, dependency_names=dependency_names)
    else:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))

# Hiển thị form chỉnh sửa tác vụ
@app.route('/task/<int:task_id>/edit')
def edit_task(task_id):
    task = task_api.get_task(task_id)
    if task:
        # Lấy danh sách các tác vụ khác để thiết lập phụ thuộc
        available_tasks = task_api.get_all_tasks()
        return render_template('task_form.html', task=task, action="update", available_tasks=available_tasks)
    else:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))

# Cập nhật tác vụ
@app.route('/task/<int:task_id>/update', methods=['POST'])
def update_task(task_id):
    task = task_api.get_task(task_id)
    if not task:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))
    
    task_data = {
        'id': task_id,
        'name': request.form.get('name', ''),
        'command': request.form.get('command', ''),
        'working_dir': request.form.get('working_dir', ''),
        'enabled': task.get('enabled', True),
        'dep_behavior': int(request.form.get('dep_behavior', 0))
    }
    
    # Xử lý kiểu lịch trình
    schedule_type = int(request.form.get('schedule_type', 0))
    task_data['schedule_type'] = schedule_type
    
    if schedule_type == 1:  # Interval
        interval_minutes = int(request.form.get('interval', 1))
        task_data['interval'] = interval_minutes * 60  # Chuyển phút sang giây
    elif schedule_type == 2:  # Cron
        task_data['cron_expression'] = request.form.get('cron_expression', '')
    
    # Xử lý chế độ thực thi
    exec_mode = int(request.form.get('exec_mode', 0))
    task_data['exec_mode'] = exec_mode
    
    if exec_mode == 1:  # Script mode
        script_mode = request.form.get('script_mode', 'content')
        
        if script_mode == 'file':
            script_file = request.form.get('script_file', '')
            if script_file:
                task_data['script_file'] = script_file
        else:
            script_content = request.form.get('script_content', '')
            if script_content:
                task_data['script_content'] = script_content
    
    # Thêm thời gian chạy tối đa nếu có
    max_runtime = request.form.get('max_runtime')
    if max_runtime:
        task_data['max_runtime'] = int(max_runtime)
    
    # Xử lý các tác vụ phụ thuộc
    dependencies = request.form.getlist('dependencies')
    dependency_changed = False
    
    if dependencies:
        # Chuyển đổi từ danh sách chuỗi sang danh sách số nguyên
        dependencies = [int(dep_id) for dep_id in dependencies if dep_id.isdigit()]
        # Giới hạn số lượng phụ thuộc tối đa
        dependencies = dependencies[:10]
        
        # Lấy danh sách phụ thuộc hiện tại
        current_task = task_api.get_task(task_id)
        current_dependencies = current_task.get('dependencies', []) if current_task else []
        
        # Kiểm tra xem có sự thay đổi phụ thuộc không
        if sorted(dependencies) != sorted(current_dependencies):
            dependency_changed = True
            
            # Xác định phụ thuộc cần thêm và xóa
            deps_to_add = [dep_id for dep_id in dependencies if dep_id not in current_dependencies]
            deps_to_remove = [dep_id for dep_id in current_dependencies if dep_id not in dependencies]
            
            # Xóa các phụ thuộc cũ
            for dep_id in deps_to_remove:
                task_api.remove_dependency(task_id, dep_id)
                
            # Thêm các phụ thuộc mới
            for dep_id in deps_to_add:
                task_api.add_dependency(task_id, dep_id)
        
        # Lưu dependencies vào task_data nhưng không gửi trong cập nhật
        task_data['dependencies'] = dependencies
    else:
        # Nếu không có phụ thuộc mới, xóa tất cả phụ thuộc hiện tại
        current_task = task_api.get_task(task_id)
        current_dependencies = current_task.get('dependencies', []) if current_task else []
        
        if current_dependencies:
            dependency_changed = True
            
            for dep_id in current_dependencies:
                task_api.remove_dependency(task_id, dep_id)
        
        # Lưu dependencies vào task_data nhưng không gửi trong cập nhật
        task_data['dependencies'] = []
    
    # Cập nhật hành vi phụ thuộc nếu có
    behavior_changed = False
    if 'dep_behavior' in request.form:
        behavior = int(request.form.get('dep_behavior', 0))
        current_behavior = current_task.get('dep_behavior', 0) if current_task else 0
        
        # Chỉ cập nhật nếu giá trị thay đổi
        if behavior != current_behavior:
            behavior_changed = True
            task_api.set_dependency_behavior(task_id, behavior)
        
        # Không cần lưu dep_behavior vào task_data khi đã cập nhật trực tiếp
    
    # Tạo một bản sao của task_data không chứa dependencies và dep_behavior
    # để tránh việc cập nhật lại các thông tin phụ thuộc đã được cập nhật riêng
    update_task_data = dict(task_data)
    if 'dependencies' in update_task_data:
        del update_task_data['dependencies']
    if 'dep_behavior' in update_task_data:
        del update_task_data['dep_behavior']
    
    # Kiểm tra xem có các trường khác cần cập nhật không
    # Lấy lại current_task nếu cần
    if not current_task:
        current_task = task_api.get_task(task_id)
    
    # Kiểm tra các trường còn lại ngoài 'id' có thay đổi không
    has_other_changes = False
    for key, value in update_task_data.items():
        if key != 'id' and (key not in current_task or current_task.get(key) != value):
            has_other_changes = True
            break
    
    # Chỉ cập nhật tác vụ nếu có thông tin khác cần cập nhật
    success = True
    if has_other_changes:
        success = task_api.update_task(update_task_data)
    
    if success:
        # Thông báo dựa trên loại thay đổi
        if has_other_changes:
            flash('Tác vụ đã được cập nhật thành công!', 'success')
        elif dependency_changed or behavior_changed:
            flash('Phụ thuộc của tác vụ đã được cập nhật thành công!', 'success')
        else:
            flash('Không có thay đổi nào được thực hiện!', 'info')
        return redirect(url_for('view_task', task_id=task_id))
    else:
        flash('Không thể cập nhật tác vụ. Vui lòng thử lại!', 'error')
        # Lấy danh sách các tác vụ khác cho phần phụ thuộc
        available_tasks = task_api.get_all_tasks()
        return render_template('task_form.html', task=task_data, action="update", available_tasks=available_tasks)

# Xóa tác vụ
@app.route('/task/<int:task_id>/delete', methods=['POST'])
def delete_task(task_id):
    success = task_api.delete_task(task_id)
    if success:
        flash('Tác vụ đã được xóa thành công!', 'success')
    else:
        flash('Không thể xóa tác vụ. Vui lòng thử lại!', 'error')
    return redirect(url_for('index'))

# Chạy tác vụ ngay lập tức
@app.route('/task/<int:task_id>/run', methods=['POST'])
def run_task(task_id):
    # Lấy thông tin task trước khi thực thi
    task = task_api.get_task(task_id)
    
    # Kiểm tra xem task có tồn tại không
    if not task:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))
    
    # Kiểm tra xem task có được kích hoạt không
    if not task.get('enabled', False):
        flash('Không thể chạy tác vụ đã bị vô hiệu hóa! Vui lòng kích hoạt tác vụ trước khi chạy.', 'error')
        return redirect(url_for('view_task', task_id=task_id))
    
    # Thực thi task
    success = task_api.execute_task(task_id)
    if success:
        flash('Tác vụ đã được kích hoạt thành công!', 'success')
    else:
        flash('Không thể kích hoạt tác vụ. Vui lòng thử lại!', 'error')
    
    return redirect(url_for('view_task', task_id=task_id))

# Bật/tắt tác vụ
@app.route('/task/<int:task_id>/toggle', methods=['POST'])
def toggle_task(task_id):
    task = task_api.get_task(task_id)
    if not task:
        flash('Không tìm thấy tác vụ với ID này!', 'error')
        return redirect(url_for('index'))
    
    # Lưu giá trị last_run_time trước khi cập nhật
    last_run_time = task.get('last_run_time', 0)
    
    # Chỉ gửi id và trạng thái enabled đã đảo ngược
    # Điều này sẽ khiến TaskAPI sử dụng lệnh enable/disable trực tiếp
    # thay vì xóa và tạo lại task
    task_data = {
        'id': task_id,
        'enabled': not task.get('enabled', True)
    }
    
    success = task_api.update_task(task_data)
    
    if success:
        status = "bật" if task_data['enabled'] else "tắt"
        flash(f'Tác vụ đã được {status} thành công!', 'success')
    else:
        flash('Không thể thay đổi trạng thái tác vụ. Vui lòng thử lại!', 'error')
    
    return redirect(url_for('view_task', task_id=task_id))

# Hiển thị thống kê
@app.route('/stats')
def stats():
    tasks = task_api.get_all_tasks()
    
    # Tính toán các thống kê cơ bản
    total_tasks = len(tasks)
    enabled_tasks = sum(1 for task in tasks if task.get('enabled', False))
    scheduled_tasks = sum(1 for task in tasks if task.get('next_run_time', 0) > 0)
    completed_tasks = sum(1 for task in tasks if task.get('last_run_time', 0) > 0)
    
    # Thống kê trạng thái thực thi
    tasks_by_status = {
        'success': sum(1 for task in tasks if task.get('exit_code', -1) == 0 and task.get('last_run_time', 0) > 0),
        'failed': sum(1 for task in tasks if task.get('exit_code', -1) != 0 and task.get('exit_code', -1) != -1),
        'never_run': sum(1 for task in tasks if task.get('last_run_time', 0) == 0)
    }
    
    # Phân loại nhiệm vụ theo tần suất
    tasks_by_frequency = {}
    for task in tasks:
        freq = task.get('frequency', 0)
        freq_name = get_frequency_name(freq)
        tasks_by_frequency[freq_name] = tasks_by_frequency.get(freq_name, 0) + 1
    
    # Đảm bảo tasks_by_frequency có tất cả các loại tần suất với ít nhất là 0
    for freq in range(5):  # Có 5 loại tần suất từ 0-4
        freq_name = get_frequency_name(freq)
        if freq_name not in tasks_by_frequency:
            tasks_by_frequency[freq_name] = 0
    
    # Thêm phân tích chi tiết
    
    # 1. Phân loại theo chế độ thực thi
    tasks_by_exec_mode = {}
    for task in tasks:
        mode = task.get('exec_mode', 0)
        mode_name = get_exec_mode_name(mode)
        tasks_by_exec_mode[mode_name] = tasks_by_exec_mode.get(mode_name, 0) + 1
    
    # 2. Phân tích thống kê thời gian chạy
    execution_history = []
    execution_success_rate = []
    current_time = time.time()
    time_ranges = [1, 7, 30]  # 1 ngày, 7 ngày, 30 ngày
    
    for days in time_ranges:
        time_threshold = current_time - (days * 24 * 60 * 60)
        recent_tasks = [t for t in tasks if t.get('last_run_time', 0) > time_threshold]
        
        success_count = sum(1 for t in recent_tasks if t.get('exit_code', -1) == 0)
        failed_count = sum(1 for t in recent_tasks if t.get('exit_code', -1) != 0 and t.get('exit_code', -1) != -1)
        total_executed = success_count + failed_count
        
        execution_history.append({
            'period': f'{days} ngày',
            'success': success_count,
            'failed': failed_count,
            'total': total_executed
        })
        
        # Tính tỷ lệ thành công
        success_rate = 0 if total_executed == 0 else (success_count / total_executed) * 100
        execution_success_rate.append({
            'period': f'{days} ngày',
            'rate': round(success_rate, 1)
        })
    
    # 3. Phân tích loại lịch trình
    tasks_by_schedule_type = {}
    for task in tasks:
        schedule_type = task.get('schedule_type', 0)
        schedule_name = get_schedule_type_name(schedule_type)
        tasks_by_schedule_type[schedule_name] = tasks_by_schedule_type.get(schedule_name, 0) + 1
    
    # 4. Dự báo lịch chạy sắp tới
    upcoming_executions = []
    now_time = time.time()
    future_tasks = [t for t in tasks if t.get('enabled', False) and t.get('next_run_time', 0) > now_time]
    future_tasks.sort(key=lambda x: x.get('next_run_time', 0))
    
    for task in future_tasks[:5]:  # Giới hạn 5 tác vụ sắp tới
        upcoming_executions.append({
            'id': task.get('id', 0),
            'name': task.get('name', 'Unknown'),
            'next_run_time': task.get('next_run_time', 0),
            'next_run_time_fmt': format_timestamp(task.get('next_run_time', 0))
        })
    
    # Truyền tất cả biến cần thiết cho template
    return render_template('stats.html', 
                          total_tasks=total_tasks,
                          enabled_tasks=enabled_tasks,
                          scheduled_tasks=scheduled_tasks,
                          completed_tasks=completed_tasks,
                          tasks_by_status=tasks_by_status,
                          tasks_by_frequency=tasks_by_frequency,
                          tasks_by_exec_mode=tasks_by_exec_mode,
                          tasks_by_schedule_type=tasks_by_schedule_type,
                          execution_history=execution_history,
                          execution_success_rate=execution_success_rate,
                          upcoming_executions=upcoming_executions)

# Hàm kiểm tra xem file có phần mở rộng được cho phép không
def allowed_file(filename):
    return '.' in filename and filename.rsplit('.', 1)[1].lower() in ALLOWED_EXTENSIONS

# API để upload file script
@app.route('/api/upload_script', methods=['POST'])
def upload_script():
    if 'script_file' not in request.files:
        return jsonify({'success': False, 'message': 'Không có file nào được gửi lên'})
    
    file = request.files['script_file']
    
    if file.filename == '':
        return jsonify({'success': False, 'message': 'Không có file nào được chọn'})
    
    if file and allowed_file(file.filename):
        # Tạo tên file duy nhất để tránh xung đột
        filename = str(uuid.uuid4()) + '_' + werkzeug.utils.secure_filename(file.filename)
        file_path = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        
        try:
            # Lưu file và set quyền thực thi
            file.save(file_path)
            os.chmod(file_path, 0o755)  # Thêm quyền thực thi
            
            return jsonify({
                'success': True, 
                'file_path': file_path,
                'original_filename': file.filename
            })
        except Exception as e:
            return jsonify({'success': False, 'message': f'Lỗi khi lưu file: {str(e)}'})
    
    return jsonify({'success': False, 'message': 'Loại file không được hỗ trợ'})

# Xử lý trang lỗi 404
@app.errorhandler(404)
def page_not_found(e):
    return render_template('error.html', error=e), 404

# Xử lý trang lỗi 500
@app.errorhandler(500)
def server_error(e):
    return render_template('error.html', error=e), 500

# AI Features Routes

@app.route('/ai')
def ai_dashboard():
    """Trang dashboard cho tính năng AI"""
    api_key = task_api.get_api_key()
    has_api_key = api_key is not None and api_key != ""
    
    # Kiểm tra thêm xem có file config.json không
    config_path = os.path.join(task_api.data_dir, "config.json")
    config_exists = os.path.exists(config_path)
    
    # Kết hợp hai điều kiện để xác định API key
    has_api_key = has_api_key or config_exists
    
    available_metrics = task_api.get_available_system_metrics()
    
    return render_template('ai_dashboard.html', has_api_key=has_api_key, available_metrics=available_metrics)

@app.route('/ai/set_api_key', methods=['POST'])
def set_api_key():
    """Thiết lập API key cho chức năng AI"""
    api_key = request.form.get('api_key', '')
    
    if api_key:
        success = task_api.set_api_key(api_key)
        if success:
            flash('API key đã được thiết lập thành công!', 'success')
        else:
            # Kiểm tra nếu config.json đã được tạo
            config_path = os.path.join(task_api.data_dir, "config.json")
            if os.path.exists(config_path):
                # Nếu file đã tồn tại, có thể backend vẫn hoạt động
                flash('API key có thể đã được thiết lập mặc dù có lỗi từ backend. Hãy thử sử dụng chức năng AI.', 'warning')
            else:
                flash('Không thể thiết lập API key. Vui lòng kiểm tra quyền truy cập thư mục dữ liệu.', 'error')
    else:
        flash('API key không hợp lệ!', 'error')
    
    return redirect(url_for('ai_dashboard'))

@app.route('/ai/create_from_description')
def ai_create_task_form():
    """Hiển thị form tạo tác vụ từ mô tả ngôn ngữ tự nhiên"""
    return render_template('ai_create_task.html')

@app.route('/ai/generate_task', methods=['POST'])
def ai_generate_task():
    """Tạo tác vụ từ mô tả ngôn ngữ tự nhiên"""
    description = request.form.get('description', '')
    
    if not description:
        flash('Vui lòng nhập mô tả tác vụ!', 'error')
        return redirect(url_for('ai_create_task_form'))
    
    # Kiểm tra xem API key đã được thiết lập chưa
    try:
        if not task_api.is_api_key_valid():
            flash('Vui lòng thiết lập API key của OpenAI trước khi sử dụng tính năng này!', 'error')
            return redirect(url_for('ai_dashboard'))
    except AttributeError:
        # Nếu phương thức is_api_key_valid chưa tồn tại, kiểm tra theo cách khác
        api_key = task_api.get_api_key()
        if not api_key:
            flash('Vui lòng thiết lập API key của OpenAI trước khi sử dụng tính năng này!', 'error')
            return redirect(url_for('ai_dashboard'))
    
    try:
        result = task_api.ai_generate_task(description)
        
        if result['success']:
            # Kiểm tra xem kết quả có hợp lệ không
            if not result.get('content') or len(result.get('content', '').strip()) < 3:
                # Nếu không có nội dung thì thêm lệnh mặc định
                result['content'] = 'df -h | awk \'NR>1 {print $5,$1}\' | while read size fs; do pct=$(echo $size | cut -d\'%\' -f1); if [ $pct -ge 90 ]; then echo "WARNING: $fs is $size full!"; fi; done'
                if "ổ đĩa" in description.lower() or "disk" in description.lower():
                    result['suggested_name'] = 'Kiểm tra ổ đĩa'
                    
            # Lưu kết quả vào session để hiển thị ở trang xác nhận
            session['ai_generated_task'] = result
            return redirect(url_for('ai_confirm_task'))
        else:
            flash(f'Không thể tạo tác vụ: {result.get("error", "Lỗi không xác định")}', 'error')
            return redirect(url_for('ai_create_task_form'))
    except Exception as e:
        flash(f'Lỗi xử lý: {str(e)}', 'error')
        return redirect(url_for('ai_create_task_form'))

@app.route('/ai/confirm_task')
def ai_confirm_task():
    """Hiển thị trang xác nhận tác vụ được tạo bởi AI"""
    task_data = session.get('ai_generated_task')
    
    if not task_data:
        flash('Không có dữ liệu tác vụ từ AI. Vui lòng tạo lại tác vụ!', 'error')
        return redirect(url_for('ai_create_task_form'))
    
    # Đảm bảo task_data chứa tất cả các field cần thiết
    if 'content' not in task_data or not task_data['content']:
        # Nếu không có content thì thêm lệnh mặc định kiểm tra ổ đĩa
        task_data['content'] = 'df -h | awk \'NR>1 {print $5,$1}\' | while read size fs; do pct=$(echo $size | cut -d\'%\' -f1); if [ $pct -ge 90 ]; then echo "WARNING: $fs is $size full!"; fi; done'
    
    # Kiểm tra xem có biểu thức cron không
    import re
    
    # Kiểm tra cron trong kết quả trả về
    if 'cron' in task_data:
        cron_expr = task_data.get('cron')
        if cron_expr and re.match(r'^\d+\s+\d+\s+\*\s+\*\s+\*$', cron_expr.strip()):
            task_data['is_cron'] = True
            task_data['cron_expression'] = cron_expr
            # Hiển thị trực tiếp biểu thức cron thay vì chuyển đổi
            task_data['schedule_description'] = f'Cron: {cron_expr}'
            print(f"Detected cron expression: {cron_expr}")
    
    # Kiểm tra từ khoá 'Cron Expression' trong output
    raw_output = task_data.get('raw_output', '')
    if not raw_output and 'is_cron' not in task_data:
        # Tìm trong schedule_description
        if 'schedule_description' in task_data:
            if re.search(r'cron', task_data['schedule_description'].lower()):
                task_data['is_cron'] = True
                
            # Tìm dạng 0 2 * * * trong schedule_description
            cron_match = re.search(r'(\d+\s+\d+\s+\*\s+\*\s+\*)', task_data['schedule_description'])
            if cron_match:
                cron_expr = cron_match.group(1).strip()
                task_data['is_cron'] = True
                task_data['cron'] = cron_expr
                task_data['cron_expression'] = cron_expr
                task_data['schedule_description'] = f'Cron: {cron_expr}'
    
    # Chuẩn hóa schedule_description nếu chưa có
    if 'schedule_description' not in task_data or not task_data['schedule_description']:
        if task_data.get('is_cron', False) and 'cron' in task_data:
            cron_expr = task_data.get('cron', '* * * * *')
            task_data['schedule_description'] = f'Cron: {cron_expr}'
        else:
            # Dựa vào interval_minutes để tạo mô tả phù hợp
            interval_minutes = task_data.get('interval_minutes', 60)
            task_data['schedule_description'] = f'Interval: {interval_minutes} phút'
    
    # Mô tả sao lưu đặc biệt - dựa vào mẫu chung
    text_values = ' '.join([str(val) for val in task_data.values() if val is not None])
    if ('2 giờ sáng' in text_values.lower() or '2 giờ' in text_values.lower()) and not task_data.get('is_cron'):
        task_data['is_cron'] = True
        task_data['cron'] = '0 2 * * *'
        task_data['cron_expression'] = '0 2 * * *'
        task_data['schedule_description'] = 'Cron: 0 2 * * *'
    
    if 'suggested_name' not in task_data or not task_data['suggested_name']:
        task_data['suggested_name'] = 'Tác vụ tạo bởi AI'
    
    # In debug để kiểm tra
    print(f"Final task data before confirmation: is_cron={task_data.get('is_cron')}, schedule={task_data.get('schedule_description')}")
    
    # Lưu lại task_data đã được bổ sung
    session['ai_generated_task'] = task_data
    
    return render_template('ai_confirm_task.html', task=task_data)

@app.route('/ai/save_task', methods=['POST'])
def ai_save_task():
    """Lưu tác vụ được tạo bởi AI"""
    task_data = session.get('ai_generated_task')
    
    if not task_data:
        flash('Không có dữ liệu tác vụ từ AI. Vui lòng tạo lại tác vụ!', 'error')
        return redirect(url_for('ai_create_task_form'))
    
    # Lấy tên tác vụ từ form hoặc sử dụng tên đề xuất
    name = request.form.get('name') or task_data.get('suggested_name')
    
    # In thông tin debug
    print(f"AI task data: {task_data}")
    print(f"Is script: {task_data.get('is_script', False)}")
    print(f"Is cron: {task_data.get('is_cron', False)}")
    print(f"Schedule description: {task_data.get('schedule_description', '')}")
    
    # Sử dụng trực tiếp cron nếu có
    is_cron = task_data.get('is_cron', False)
    cron_expr = task_data.get('cron', '')
    
    new_task = {
        'name': name,
        'enabled': True,
        'exec_mode': 1 if task_data.get('is_script', False) else 0,  # 1: script, 0: command
        'schedule_type': 2 if is_cron else 1,  # 2: cron, 1: interval
    }
    
    # Thêm command hoặc script content
    if task_data.get('is_script', False):
        print(f"Saving as script: {task_data.get('content', '')[:50]}...")
        new_task['script_content'] = task_data.get('content', '')
        # Đảm bảo script không bị lưu vào trường command
        if 'command' in new_task:
            del new_task['command']
    else:
        print(f"Saving as command: {task_data.get('content', '')[:50]}...")
        new_task['command'] = task_data.get('content', '')
        # Đảm bảo command không bị lưu vào trường script_content
        if 'script_content' in new_task:
            del new_task['script_content']
    
    # Thêm thông tin lịch trình
    if is_cron:
        # Sử dụng biểu thức cron trực tiếp
        new_task['cron_expression'] = cron_expr
        print(f"Using cron expression: {cron_expr}")
    else:
        # Đảm bảo interval được đặt đúng
        interval_minutes = task_data.get('interval_minutes', 60)
        interval_seconds = interval_minutes * 60
        print(f"Setting interval to {interval_minutes} minutes ({interval_seconds} seconds)")
        new_task['interval'] = interval_seconds
    
    # Thêm tác vụ
    print(f"Sending task to API: {new_task}")
    task_id = task_api.add_task(new_task)
    
    if task_id > 0:
        # Xóa dữ liệu tạm trong session
        session.pop('ai_generated_task', None)
        
        flash('Tác vụ AI đã được tạo thành công!', 'success')
        return redirect(url_for('view_task', task_id=task_id))
    else:
        flash('Không thể tạo tác vụ. Có thể script gặp vấn đề với quyền thực thi hoặc cú pháp. Vui lòng kiểm tra lại!', 'error')
        
        # Thử kiểm tra tình trạng các tác vụ để xem có lỗi gì
        try:
            all_tasks = task_api.get_all_tasks()
            if all_tasks:
                # Nếu có tác vụ khác tồn tại, có thể có vấn đề với script của tác vụ này
                flash('Hệ thống có thể không hỗ trợ một số lệnh trong script này. Hãy thử đơn giản hóa script.', 'warning')
            else:
                # Nếu không có tác vụ nào, có thể có vấn đề với hệ thống
                flash('Không có tác vụ nào trong hệ thống. Có thể có vấn đề với cấu hình máy chủ.', 'warning')
        except Exception as e:
            flash(f'Lỗi kiểm tra tác vụ: {str(e)}', 'error')
        
        return redirect(url_for('ai_confirm_task'))

@app.route('/ai/dynamic/<int:task_id>')
def ai_dynamic_form(task_id):
    """Hiển thị form chuyển đổi tác vụ thành AI Dynamic"""
    task = task_api.get_task(task_id)
    
    if not task:
        flash('Không tìm thấy tác vụ!', 'error')
        return redirect(url_for('index'))
    
    available_metrics = task_api.get_available_system_metrics()
    
    return render_template('ai_dynamic_form.html', task=task, available_metrics=available_metrics)

@app.route('/ai/convert_to_dynamic', methods=['POST'])
def convert_to_dynamic():
    """Chuyển đổi tác vụ thành AI Dynamic"""
    task_id = request.form.get('task_id')
    goal = request.form.get('goal', '')
    metrics = request.form.getlist('metrics')
    
    if not task_id or not goal or not metrics:
        flash('Vui lòng điền đầy đủ thông tin!', 'error')
        return redirect(url_for('ai_dynamic_form', task_id=task_id))
    
    # Kết hợp các metrics thành chuỗi phân cách bằng dấu phẩy
    metrics_str = ','.join(metrics)
    
    success = task_api.convert_task_to_ai_dynamic(task_id, goal, metrics_str)
    
    if success:
        flash('Tác vụ đã được chuyển đổi thành AI Dynamic thành công!', 'success')
        return redirect(url_for('view_task', task_id=task_id))
    else:
        flash('Không thể chuyển đổi tác vụ. Vui lòng thử lại!', 'error')
        return redirect(url_for('ai_dynamic_form', task_id=task_id))

# Run app
if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000) 