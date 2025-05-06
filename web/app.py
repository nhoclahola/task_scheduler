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
    """Xem chi tiết tác vụ"""
    try:
        print(f"Debug: Starting view_task for task_id={task_id}")
        
        # Lấy thông tin task, bao gồm cả refresh để luôn có dữ liệu mới nhất
        task = task_api.get_task(task_id, force_refresh=True)
        
        # Debug log sau khi nhận response từ API
        print(f"Debug: Task data received: {task is not None}")
        if task:
            print(f"Debug: Task {task_id} info - Name: {task.get('name')}, Enabled: {task.get('enabled')}, Exec mode: {task.get('exec_mode')}")
            if 'cron_expr' in task:
                print(f"Debug: Schedule info - cron_expr: {task.get('cron_expr')}")
            if 'cron_expression' in task:
                print(f"Debug: Cron expression: {task.get('cron_expression')}")
            if 'script_content' in task:
                script_length = len(task.get('script_content', ''))
                print(f"Debug: Script content length: {script_length} characters")
        else:
            print(f"Debug: No task data received from API for task_id={task_id}")
        
        if not task:
            # Thử lấy danh sách tác vụ để xem task_id có tồn tại không
            all_tasks = task_api.get_all_tasks()
            task_ids = [t.get('id') for t in all_tasks]
            
            if task_id in task_ids:
                error_msg = f'Tác vụ {task_id} tồn tại nhưng không thể tải thông tin chi tiết. Hãy thử lại sau.'
                print(f"Debug: {error_msg}")
                flash(error_msg, 'error')
            else:
                error_msg = f'Không tìm thấy tác vụ với ID {task_id}!'
                print(f"Debug: {error_msg}")
                flash(error_msg, 'error')
                
            return redirect(url_for('index'))
            
        # Thêm các thông tin hiển thị
        task['creation_time_fmt'] = format_timestamp(task.get('creation_time', 0))
        task['next_run_time_fmt'] = format_timestamp(task.get('next_run_time', 0))
        task['last_run_time_fmt'] = format_timestamp(task.get('last_run_time', 0))
        
        # Thêm các tên mô tả
        task['schedule_type_name'] = get_schedule_type_name(task.get('schedule_type', 0))
        task['frequency_name'] = get_frequency_name(task.get('frequency', 0))
        task['dependency_behavior_name'] = get_dependency_behavior_name(task.get('dep_behavior', 0))
        task['exec_mode_name'] = get_exec_mode_name(task.get('exec_mode', 0))
        
        # Định dạng cron expression để hiển thị
        if task.get('schedule_type') == 2 and 'cron_expression' in task:
            task['cron_formatted'] = f"Cron: {task.get('cron_expression')}"
        elif 'cron_expr' in task and task.get('cron_expr', '').startswith('Cron:'):
            cron_expr = task.get('cron_expr')
            task['cron_formatted'] = cron_expr
            # Tạo cron_expression nếu không có
            if 'cron_expression' not in task:
                # Trích xuất phần sau "Cron:"
                cron_parts = cron_expr.split('Cron:', 1)
                if len(cron_parts) > 1:
                    task['cron_expression'] = cron_parts[1].strip()
                    
            # Đảm bảo schedule_type được đặt đúng
            task['schedule_type'] = 2
        
        # Mô tả trạng thái dựa trên exit_code
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
        
        # Lấy danh sách tên của các task phụ thuộc
        dependency_names = {}
        if task.get('dependencies'):
            for dep_id in task['dependencies']:
                # Không cần force_refresh ở đây vì chỉ cần tên
                dep_task = task_api.get_task(dep_id, force_refresh=False)
                if dep_task:
                    dependency_names[dep_id] = dep_task['name']
                else:
                    dependency_names[dep_id] = f"Tác vụ {dep_id}"
        
        # Lấy nội dung script nếu là task script
        if task.get('exec_mode') == 1:
            # Kiểm tra xem đã có script_content chưa
            if 'script_content' not in task or not task['script_content'] or len(task['script_content']) < 20:
                # Lấy script content từ API
                print(f"Debug: Fetching script content for task {task_id} via separate call")
                script_content = task_api.get_script_content(task_id)
                if script_content:
                    task['script_content'] = script_content
                    print(f"Debug: Script content fetched via separate call, length: {len(script_content)} characters")
                else:
                    print(f"Debug: No script content found via separate call for task {task_id}")
            else:
                print(f"Debug: Using existing script content, length: {len(task['script_content'])} characters")
        
        # Render template với thông tin task
        print(f"Debug: Rendering template for task {task_id}")
        return render_template('task_detail.html', task=task, dependency_names=dependency_names)
        
    except Exception as e:
        print(f"Error in view_task: {e}")
        import traceback
        traceback.print_exc()
        flash(f'Lỗi khi xem chi tiết tác vụ: {e}', 'error')
        return redirect(url_for('index'))

# Hiển thị form chỉnh sửa tác vụ
@app.route('/task/<int:task_id>/edit')
def edit_task(task_id):
    # Sử dụng force_refresh=True để đảm bảo dữ liệu mới nhất
    task = task_api.get_task(task_id, force_refresh=True)
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
    # Sử dụng force_refresh=True để đảm bảo dữ liệu mới nhất
    task = task_api.get_task(task_id, force_refresh=True)
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
        current_task = task_api.get_task(task_id, force_refresh=True)
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
        current_task = task_api.get_task(task_id, force_refresh=True)
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
        current_task = task_api.get_task(task_id, force_refresh=True)
    
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
    """Xóa một tác vụ"""
    try:
        # Sử dụng biến task_api toàn cục thay vì gọi hàm get_task_api()
        success = task_api.delete_task(task_id)
        
        if success:
            flash('Tác vụ đã được xóa thành công!', 'success')
        else:
            flash('Không thể xóa tác vụ. Vui lòng thử lại!', 'error')
            
        return redirect(url_for('index'))
    except Exception as e:
        flash(f'Lỗi khi xóa tác vụ: {str(e)}', 'error')
        return redirect(url_for('index'))

# Chạy tác vụ ngay lập tức
@app.route('/task/<int:task_id>/run', methods=['POST'])
def run_task(task_id):
    # Lấy thông tin task trước khi thực thi, với force_refresh=True để đảm bảo dữ liệu mới nhất
    task = task_api.get_task(task_id, force_refresh=True)
    
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
    # Lấy thông tin task với force_refresh=True để đảm bảo dữ liệu mới nhất
    task = task_api.get_task(task_id, force_refresh=True)
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

@app.route('/ai-dashboard')
def ai_dashboard():
    # Lấy API key
    api_key = task_api.get_api_key()
    
    # Khởi tạo danh sách tác vụ AI trống
    ai_tasks = []
    
    # Kiểm tra xem có API key hợp lệ không
    api_key_valid = api_key and api_key != "CONFIGURED_API_KEY" and len(api_key) > 10
    
    # Chỉ lấy danh sách tác vụ AI nếu có API key hợp lệ
    if api_key_valid:
        try:
            # Lấy tất cả các tác vụ và lọc ra các tác vụ AI Dynamic
            all_tasks = task_api.get_all_tasks()
            ai_tasks = [task for task in all_tasks if task.get('exec_mode') == 2]  # 2 = EXEC_AI_DYNAMIC
            print(f"Found {len(ai_tasks)} AI Dynamic tasks")
        except Exception as e:
            print(f"Error fetching AI tasks: {e}")
            flash("Lỗi khi lấy danh sách tác vụ AI", "error")
    
    # Lấy danh sách các system metrics có sẵn
    system_metrics = task_api.get_available_system_metrics()
    
    return render_template('ai_dashboard.html', 
                          api_key=api_key, 
                          api_key_valid=api_key_valid, 
                          ai_tasks=ai_tasks,
                          system_metrics=system_metrics)

@app.route('/ai/set_api_key', methods=['POST'])
def set_api_key():
    """Cập nhật API key cho DeepSeek"""
    api_key = request.form.get('api_key', '').strip()
    
    # Debug log
    print(f"Debug: Processing API key update. Key length: {len(api_key)}")
    
    # Kiểm tra API key có rỗng không
    if not api_key:
        flash('API key không thể để trống. Vui lòng nhập API key hợp lệ.', 'danger')
        return redirect(url_for('ai_dashboard'))
    
    # Cập nhật API key
    success = task_api.set_api_key(api_key)
    
    if success:
        print("Debug: API key updated successfully")
        flash('API key đã được cập nhật thành công.', 'success')
    else:
        print("Debug: API key update failed")
        flash('Không thể cập nhật API key. Vui lòng kiểm tra lại.', 'danger')
    
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
    api_key = task_api.get_api_key()
    if not api_key:
        flash('Vui lòng thiết lập API key của DeepSeek trước khi sử dụng tính năng này!', 'error')
        return redirect(url_for('ai_dashboard'))
    
    # Hiển thị thông báo đang xử lý cho người dùng
    flash('Đang xử lý yêu cầu tạo tác vụ... Quá trình này có thể mất từ 15-30 giây.', 'info')
    
    try:
        print(f"Sending AI task generation request with description: '{description}'")
        result = task_api.ai_generate_task(description)
        
        if result.get('success'):
            # Kiểm tra xem kết quả có hợp lệ không
            if not result.get('content') or len(result.get('content', '').strip()) < 3:
                # Log lỗi để debug
                print(f"Received empty or invalid content from AI: {result}")
                
                # Nếu không có nội dung thì thêm lệnh mặc định
                if "disk" in description.lower() or "ổ đĩa" in description.lower():
                    result['content'] = 'df -h | awk \'NR>1 {print $5,$1}\' | while read size fs; do pct=$(echo $size | cut -d\'%\' -f1); if [ $pct -ge 90 ]; then echo "WARNING: $fs is $size full!"; fi; done'
                    result['suggested_name'] = 'Kiểm tra ổ đĩa'
                else:
                    # Tạo script cơ bản dựa trên mô tả
                    result['content'] = f'''#!/bin/bash
# Auto-generated script based on: "{description}"
echo "Executing task: {description}"
# Add your commands here
if [ $? -eq 0 ]; then
  echo "Task completed successfully"
  exit 0
else
  echo "Task failed"
  exit 1
fi'''
                    
                    # Tạo tên gợi ý từ mô tả
                    words = description.split()
                    if len(words) > 3:
                        result['suggested_name'] = "_".join(words[:3]).lower()
                    else:
                        result['suggested_name'] = "_".join(words).lower()
                    
                    # Loại bỏ ký tự đặc biệt
                    import re
                    result['suggested_name'] = re.sub(r'[^a-zA-Z0-9_]', '', result['suggested_name'])
                    if len(result['suggested_name']) > 30:
                        result['suggested_name'] = result['suggested_name'][:30]
                
                result['is_script'] = True
                result['schedule_description'] = "Chạy mỗi ngày"
                
                # Ghi log 
                print(f"Using fallback content for AI task: {result['suggested_name']}")
            
            # Lưu kết quả vào session để hiển thị ở trang xác nhận
            session['ai_generated_task'] = result
            return redirect(url_for('ai_confirm_task'))
        else:
            error_message = result.get('error', 'Lỗi không xác định')
            print(f"AI task generation failed: {error_message}")
            flash(f'Không thể tạo tác vụ: {error_message}', 'error')
            return redirect(url_for('ai_create_task_form'))
    except Exception as e:
        import traceback
        # In traceback để debugging
        traceback.print_exc()
        print(f"Exception during AI task generation: {str(e)}")
        flash(f'Lỗi xử lý: {str(e)}', 'error')
        return redirect(url_for('ai_create_task_form'))

@app.route('/ai/confirm_task')
def ai_confirm_task():
    """Hiển thị trang xác nhận tác vụ được tạo bởi AI"""
    task_data = session.get('ai_generated_task')
    
    if not task_data:
        flash('Không có dữ liệu tác vụ từ AI. Vui lòng tạo lại tác vụ!', 'error')
        return redirect(url_for('ai_create_task_form'))
    
    # Log dữ liệu task để debug
    print(f"Task data for confirmation: {task_data.keys()}")
    
    # Đảm bảo task_data chứa tất cả các field cần thiết
    if 'content' not in task_data or not task_data['content']:
        flash('Nội dung tác vụ không hợp lệ. Vui lòng tạo lại tác vụ!', 'error')
        return redirect(url_for('ai_create_task_form'))
    
    # Kiểm tra xem có biểu thức cron không
    import re
    
    # Gán is_cron nếu không có
    if 'is_cron' not in task_data:
        task_data['is_cron'] = False
    
    # Kiểm tra cron trong kết quả trả về
    if 'cron' in task_data and task_data['cron']:
        cron_expr = task_data.get('cron')
        if cron_expr and re.match(r'^\d+\s+\d+\s+\*\s+\*\s+\*$', cron_expr.strip()):
            task_data['is_cron'] = True
            task_data['cron_expression'] = cron_expr
            # Hiển thị trực tiếp biểu thức cron thay vì chuyển đổi
            task_data['schedule_description'] = f'Cron: {cron_expr}'
            print(f"Detected cron expression: {cron_expr}")
    
    # Kiểm tra từ khoá 'Cron Expression' trong output
    raw_output = task_data.get('raw_output', '')
    if raw_output and 'is_cron' not in task_data:
        # Tìm trong raw_output
        cron_match = re.search(r'Cron Expression:\s*(\d+\s+\d+\s+\*\s+\*\s+\*)', raw_output)
        if cron_match:
            cron_expr = cron_match.group(1).strip()
            task_data['is_cron'] = True
            task_data['cron'] = cron_expr
            task_data['cron_expression'] = cron_expr
            task_data['schedule_description'] = f'Cron: {cron_expr}'
            print(f"Extracted cron from raw output: {cron_expr}")
    
    # Chuẩn hóa schedule_description nếu chưa có
    if 'schedule_description' not in task_data or not task_data['schedule_description']:
        if task_data.get('is_cron', False) and 'cron' in task_data:
            cron_expr = task_data.get('cron', '* * * * *')
            task_data['schedule_description'] = f'Cron: {cron_expr}'
        else:
            # Dựa vào interval_minutes để tạo mô tả phù hợp
            interval_minutes = task_data.get('interval_minutes', 60)
            task_data['schedule_description'] = f'Interval: {interval_minutes} phút'
    
    # Kiểm tra tên đề xuất
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

# Email Configuration Routes
@app.route('/email')
def email_settings():
    """Display email configuration settings"""
    try:
        email_config = task_api.get_email_config() or {}
        return render_template('email_settings.html', email_config=email_config)
    except Exception as e:
        flash(f'Lỗi khi tải cài đặt email: {str(e)}', 'error')
        return render_template('email_settings.html', email_config={})

@app.route('/email/update', methods=['POST'])
def update_email_settings():
    """Update email configuration"""
    email_address = request.form.get('email_address', '').strip()
    email_password = request.form.get('email_password', '').strip()
    smtp_server = request.form.get('smtp_server', '').strip()
    smtp_port = request.form.get('smtp_port', 587)
    recipient_email = request.form.get('recipient_email', '').strip()
    
    # Validate inputs
    if not email_address or not smtp_server:
        flash('Địa chỉ email và máy chủ SMTP là bắt buộc', 'error')
        return redirect(url_for('email_settings'))
    
    try:
        smtp_port = int(smtp_port)
    except ValueError:
        flash('Số cổng SMTP không hợp lệ', 'error')
        return redirect(url_for('email_settings'))
    
    # Log the parameters (without showing full password)
    print(f"Debug: Updating email config with: email={email_address}, server={smtp_server}, port={smtp_port}, recipient={recipient_email}")
    if email_password:
        print(f"Debug: Password provided (length: {len(email_password)})")
    else:
        print("Debug: No new password provided")
    
    # If no password provided, check if we already have one configured
    current_config = task_api.get_email_config() or {}
    if not email_password and current_config.get('email_address') == email_address:
        # Get current password from backend
        result = task_api.update_email_config(
            email_address,
            "", # Leave password empty to keep current one
            smtp_server,
            smtp_port,
            recipient_email if recipient_email else None
        )
    else:
        # Use new password
        result = task_api.update_email_config(
            email_address,
            email_password,
            smtp_server,
            smtp_port,
            recipient_email if recipient_email else None
        )
    
    if result:
        flash('Cài đặt email đã được cập nhật thành công', 'success')
    else:
        flash('Không thể cập nhật cài đặt email', 'error')
    
    return redirect(url_for('email_settings'))

@app.route('/email/toggle/<action>', methods=['POST'])
def toggle_email_notifications(action):
    """Enable or disable email notifications"""
    if action == 'enable':
        result = task_api.enable_email()
        if result:
            flash('Đã bật thông báo email thành công', 'success')
        else:
            flash('Không thể bật thông báo email. Vui lòng đảm bảo cài đặt email của bạn chính xác.', 'error')
    else:
        result = task_api.disable_email()
        if result:
            flash('Đã tắt thông báo email thành công', 'success')
        else:
            flash('Không thể tắt thông báo email', 'error')
    
    return redirect(url_for('email_settings'))

@app.route('/email/set-recipient', methods=['POST'])
def set_recipient_email():
    """Set recipient email address"""
    recipient_email = request.form.get('recipient_email', '')
    
    if not recipient_email:
        flash('Địa chỉ email người nhận là bắt buộc', 'error')
        return redirect(url_for('email_settings'))
    
    result = task_api.set_recipient_email(recipient_email)
    
    if result:
        flash('Đã cập nhật email người nhận thành công', 'success')
    else:
        flash('Không thể cập nhật email người nhận', 'error')
    
    return redirect(url_for('email_settings'))

# Run app
if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000) 