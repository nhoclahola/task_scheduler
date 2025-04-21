/* Custom JavaScript for Task Scheduler */

// Cập nhật ngày giờ theo định dạng Việt Nam
function formatDate(date) {
    return date.toLocaleString('vi-VN', {
        day: '2-digit',
        month: '2-digit',
        year: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

// Function để hiển thị thông báo toast
function showToast(message, type = 'success') {
    // Tạo toast container nếu chưa tồn tại
    if (!document.querySelector('.toast-container')) {
        const toastContainer = document.createElement('div');
        toastContainer.className = 'toast-container position-fixed top-0 end-0 p-3';
        document.body.appendChild(toastContainer);
    }
    
    // Tạo toast
    const toastId = 'toast-' + Date.now();
    const toastHTML = `
        <div id="${toastId}" class="toast" role="alert" aria-live="assertive" aria-atomic="true">
            <div class="toast-header ${type === 'success' ? 'bg-success text-white' : 'bg-danger text-white'}">
                <strong class="me-auto">${type === 'success' ? 'Thành công' : 'Lỗi'}</strong>
                <small>${formatDate(new Date())}</small>
                <button type="button" class="btn-close btn-close-white" data-bs-dismiss="toast" aria-label="Close"></button>
            </div>
            <div class="toast-body">
                ${message}
            </div>
        </div>
    `;
    
    // Thêm toast vào container
    document.querySelector('.toast-container').innerHTML += toastHTML;
    
    // Hiển thị toast
    const toastElement = document.getElementById(toastId);
    const toast = new bootstrap.Toast(toastElement, { delay: 5000 });
    toast.show();
    
    // Xóa toast khỏi DOM sau khi ẩn
    toastElement.addEventListener('hidden.bs.toast', function () {
        toastElement.remove();
    });
}

// Quản lý form submission với AJAX
document.addEventListener('DOMContentLoaded', function() {
    // Xử lý các nút có thuộc tính data-confirm
    document.querySelectorAll('[data-confirm]').forEach(button => {
        button.addEventListener('click', function(e) {
            if (!confirm(this.getAttribute('data-confirm'))) {
                e.preventDefault();
                e.stopPropagation();
            }
        });
    });
    
    // Xử lý ẩn flash messages sau một khoảng thời gian
    document.querySelectorAll('.alert').forEach(alert => {
        setTimeout(() => {
            const closeBtn = alert.querySelector('.btn-close');
            if (closeBtn) {
                closeBtn.click();
            }
        }, 5000);
    });
    
    // Hiển thị đồng hồ hiện tại ở footer (nếu có phần tử)
    const clockElement = document.getElementById('current-time');
    if (clockElement) {
        function updateClock() {
            clockElement.textContent = formatDate(new Date());
        }
        updateClock();
        setInterval(updateClock, 1000);
    }
    
    // Helper function để chuyển đổi định dạng thời gian từ giây sang chuỗi dễ đọc
    window.formatDuration = function(seconds) {
        if (!seconds || seconds <= 0) return "0 giây";
        
        const days = Math.floor(seconds / 86400);
        seconds %= 86400;
        const hours = Math.floor(seconds / 3600);
        seconds %= 3600;
        const minutes = Math.floor(seconds / 60);
        seconds %= 60;
        
        let result = [];
        if (days > 0) result.push(`${days} ngày`);
        if (hours > 0) result.push(`${hours} giờ`);
        if (minutes > 0) result.push(`${minutes} phút`);
        if (seconds > 0) result.push(`${seconds} giây`);
        
        return result.join(', ');
    };

    // Xử lý upload file script
    const fileUpload = document.getElementById('script_file_upload');
    if (fileUpload) {
        fileUpload.addEventListener('change', function(e) {
            const uploadStatus = document.getElementById('upload_status');
            const scriptFileInput = document.getElementById('script_file');
            
            if (!e.target.files || e.target.files.length === 0) {
                return;
            }
            
            const file = e.target.files[0];
            if (uploadStatus) {
                uploadStatus.innerHTML = '<div class="alert alert-info">Đang tải lên file...</div>';
            }
            
            // Tạo FormData để upload
            const formData = new FormData();
            formData.append('script_file', file);
            
            // Upload file qua API
            fetch('/api/upload_script', {
                method: 'POST',
                body: formData
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    if (scriptFileInput) {
                        scriptFileInput.value = data.file_path;
                    }
                    
                    if (uploadStatus) {
                        uploadStatus.innerHTML = `
                            <div class="alert alert-success">
                                <i class="fas fa-check-circle me-2"></i>
                                File <strong>${data.original_filename}</strong> đã được tải lên thành công.
                                <div class="mt-1 small">Đường dẫn: ${data.file_path}</div>
                            </div>`;
                    }
                } else {
                    if (uploadStatus) {
                        uploadStatus.innerHTML = `
                            <div class="alert alert-danger">
                                <i class="fas fa-times-circle me-2"></i>
                                Lỗi: ${data.message}
                            </div>`;
                    }
                }
            })
            .catch(error => {
                console.error('Error:', error);
                if (uploadStatus) {
                    uploadStatus.innerHTML = `
                        <div class="alert alert-danger">
                            <i class="fas fa-times-circle me-2"></i>
                            Lỗi khi tải lên file. Vui lòng thử lại sau.
                        </div>`;
                }
            });
        });
    }
    
    // Xử lý hiển thị/ẩn các phần tùy thuộc vào loại lịch trình
    const setupScheduleTypeHandlers = function() {
        const scheduleTypeRadios = document.querySelectorAll('input[name="schedule_type"]');
        const intervalSection = document.getElementById('interval_section');
        const cronSection = document.getElementById('cron_section');
        
        if (scheduleTypeRadios.length && intervalSection && cronSection) {
            scheduleTypeRadios.forEach(radio => {
                radio.addEventListener('change', function() {
                    // Ẩn tất cả các section trước
                    intervalSection.classList.add('d-none');
                    cronSection.classList.add('d-none');
                    
                    // Hiển thị section phù hợp
                    if (this.value === '1') {
                        intervalSection.classList.remove('d-none');
                    } else if (this.value === '2') {
                        cronSection.classList.remove('d-none');
                    }
                });
            });
        }
    };
    
    // Xử lý hiển thị/ẩn các phần tùy thuộc vào chế độ thực thi
    const setupExecModeHandlers = function() {
        const execModeRadios = document.querySelectorAll('input[name="exec_mode"]');
        const commandSection = document.getElementById('command_section');
        const scriptSection = document.getElementById('script_section');
        
        if (execModeRadios.length && commandSection && scriptSection) {
            execModeRadios.forEach(radio => {
                radio.addEventListener('change', function() {
                    if (this.value === '0') {
                        commandSection.classList.remove('d-none');
                        scriptSection.classList.add('d-none');
                    } else {
                        commandSection.classList.add('d-none');
                        scriptSection.classList.remove('d-none');
                    }
                });
            });
        }
    };
    
    // Xử lý hiển thị/ẩn các phần tùy thuộc vào chế độ script
    const setupScriptModeHandlers = function() {
        const scriptModeRadios = document.querySelectorAll('input[name="script_mode"]');
        const scriptContentSection = document.getElementById('script_content_section');
        const scriptFileSection = document.getElementById('script_file_section');
        
        if (scriptModeRadios.length && scriptContentSection && scriptFileSection) {
            scriptModeRadios.forEach(radio => {
                radio.addEventListener('change', function() {
                    if (this.value === 'content') {
                        scriptContentSection.classList.remove('d-none');
                        scriptFileSection.classList.add('d-none');
                    } else {
                        scriptContentSection.classList.add('d-none');
                        scriptFileSection.classList.remove('d-none');
                    }
                });
            });
        }
    };
    
    // Thiết lập các handler
    setupScheduleTypeHandlers();
    setupExecModeHandlers();
    setupScriptModeHandlers();

    // Xử lý vị trí dropdown trong bảng có cuộn
    const taskTableContainer = document.querySelector('.task-table-container');
    if (taskTableContainer) {
        // Đặt chiều cao động dựa trên kích thước màn hình
        function adjustTableHeight() {
            const windowHeight = window.innerHeight;
            const headerHeight = document.querySelector('header')?.offsetHeight || 60;
            const footerHeight = document.querySelector('footer')?.offsetHeight || 0;
            const cardHeaderHeight = taskTableContainer.closest('.card')?.querySelector('.card-header')?.offsetHeight || 0;
            const offset = 60; // Offset bổ sung cho margins và paddings
            
            const calculatedHeight = windowHeight - headerHeight - footerHeight - cardHeaderHeight - offset;
            const minHeight = 400; // Chiều cao tối thiểu
            const maxHeight = 800; // Chiều cao tối đa
            
            const finalHeight = Math.max(minHeight, Math.min(calculatedHeight, maxHeight));
            taskTableContainer.style.height = `${finalHeight}px`;
        }
        
        // Điều chỉnh chiều cao ban đầu và khi thay đổi kích thước cửa sổ
        adjustTableHeight();
        window.addEventListener('resize', adjustTableHeight);
    }
    
    // Xử lý tất cả các dropdown trong bảng
    const actionDropdowns = document.querySelectorAll('.actions-cell .dropdown');
    
    if (actionDropdowns.length > 0) {
        actionDropdowns.forEach(dropdown => {
            const dropdownToggle = dropdown.querySelector('.dropdown-toggle');
            const dropdownMenu = dropdown.querySelector('.dropdown-menu');
            
            if (dropdownToggle && dropdownMenu) {
                // Lắng nghe sự kiện khi dropdown được hiển thị
                dropdownToggle.addEventListener('click', function(e) {
                    // Ngăn chặn sự kiện mặc định
                    e.stopPropagation();
                    
                    // Đặt lại vị trí của dropdown menu
                    setTimeout(() => {
                        const rect = dropdownToggle.getBoundingClientRect();
                        const tableContainer = dropdown.closest('.task-table-container');
                        const tableRect = tableContainer ? tableContainer.getBoundingClientRect() : document.body.getBoundingClientRect();
                        
                        // Đảm bảo dropdown menu được hiển thị trong viewport
                        const dropdownWidth = 220; // Độ rộng ước tính của dropdown menu
                        
                        // Kiểm tra xem dropdown có bị tràn ra khỏi bảng không
                        const rightEdge = rect.left + dropdownWidth;
                        const viewportWidth = window.innerWidth;
                        
                        if (rightEdge > viewportWidth) {
                            // Nếu tràn ra bên phải viewport, đặt menu ở bên trái
                            dropdownMenu.style.left = 'auto';
                            dropdownMenu.style.right = '0';
                        } else if (rect.left < tableRect.left) {
                            // Nếu tràn ra bên trái bảng, đặt menu ở bên phải
                            dropdownMenu.style.left = '0';
                            dropdownMenu.style.right = 'auto';
                        } else {
                            // Mặc định, đặt menu ở bên phải cùa nút
                            dropdownMenu.style.left = '0';
                            dropdownMenu.style.right = 'auto';
                        }
                        
                        // Xác định vị trí theo chiều dọc
                        const menuHeight = dropdownMenu.offsetHeight;
                        const spaceBelow = window.innerHeight - rect.bottom;
                        
                        if (spaceBelow < menuHeight && rect.top > menuHeight) {
                            // Nếu không đủ không gian bên dưới nhưng đủ không gian bên trên
                            dropdownMenu.style.top = 'auto';
                            dropdownMenu.style.bottom = '100%';
                            dropdownMenu.classList.add('dropdown-menu-up');
                        } else {
                            // Mặc định, hiển thị bên dưới
                            dropdownMenu.style.top = '100%';
                            dropdownMenu.style.bottom = 'auto';
                            dropdownMenu.classList.remove('dropdown-menu-up');
                        }
                        
                        // Đảm bảo menu không bị chìm xuống dưới viewport
                        const maxHeight = window.innerHeight - rect.bottom - 10;
                        if (menuHeight > maxHeight && maxHeight > 100) {
                            dropdownMenu.style.maxHeight = maxHeight + 'px';
                            dropdownMenu.style.overflowY = 'auto';
                        }
                    }, 0);
                });
                
                // Xử lý khi người dùng cuộn bảng
                const tableContainer = dropdown.closest('.task-table-container');
                if (tableContainer) {
                    tableContainer.addEventListener('scroll', function() {
                        // Đóng tất cả các dropdown đang mở khi cuộn
                        const openDropdown = tableContainer.querySelector('.dropdown-menu.show');
                        if (openDropdown) {
                            const bsDropdown = bootstrap.Dropdown.getInstance(openDropdown.previousElementSibling);
                            if (bsDropdown) {
                                bsDropdown.hide();
                            }
                        }
                    });
                }
            }
        });
    }
    
    // Xử lý sự kiện click bên ngoài để đóng dropdown
    document.addEventListener('click', function(e) {
        const openDropdowns = document.querySelectorAll('.dropdown-menu.show');
        openDropdowns.forEach(dropdown => {
            if (!dropdown.contains(e.target)) {
                const bsDropdown = bootstrap.Dropdown.getInstance(dropdown.previousElementSibling);
                if (bsDropdown) {
                    bsDropdown.hide();
                }
            }
        });
    });
});

// Xử lý AJAX form submission (nếu cần trong tương lai)
function submitFormAjax(formElement, successCallback, errorCallback) {
    const formData = new FormData(formElement);
    
    fetch(formElement.action, {
        method: formElement.method,
        body: formData,
        headers: {
            'X-Requested-With': 'XMLHttpRequest'
        }
    })
    .then(response => response.json())
    .then(data => {
        if (data.success) {
            if (successCallback) successCallback(data);
            else showToast(data.message || 'Thao tác thành công!', 'success');
        } else {
            if (errorCallback) errorCallback(data);
            else showToast(data.message || 'Đã xảy ra lỗi!', 'error');
        }
    })
    .catch(error => {
        console.error('Error:', error);
        if (errorCallback) errorCallback({ message: 'Lỗi kết nối máy chủ' });
        else showToast('Lỗi kết nối máy chủ', 'error');
    });
    
    return false;
} 