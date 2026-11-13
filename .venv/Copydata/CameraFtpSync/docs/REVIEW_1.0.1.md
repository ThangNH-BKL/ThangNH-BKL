# Rà soát và bản sửa 1.0.1

Đã thực hiện hai lượt rà soát tĩnh độc lập bằng model `gpt-6-astra`, mức suy luận `xhigh`. Tiến trình chính sửa mã, build và chạy kiểm thử; agent rà soát không tự nhận đã chạy MFC hoặc kiểm thử runtime. Không phát hiện thêm lỗi P1 trong lượt kiểm tra lại các bản sửa. Đây không phải chứng nhận an toàn cho máy chủ production.

Các lỗi đã sửa:

- GUI xóa lệnh thay thế thủ công còn chờ sau khi dừng/join worker, tránh mang quyền chấp nhận từ cấu hình cũ sang đích mới.
- Sửa biểu thức điều kiện `CString`, thêm header OLE trực tiếp, giới hạn tùy chọn compiler C++ cho C++, và thêm đường dẫn include cho resource manifest. Các thay đổi này xử lý các điểm có thể làm MSVC/MFC build thất bại; vẫn phải xác nhận bằng compiler thật trên máy có MFC.
- Tạo thư mục con trong namespace cho từng file trước khi truy vấn/upload. Kiểm thử FTP đầu tiên không chạy probe trước để tránh che lỗi tạo namespace.
- Lỗi STOR thực sự chỉ ghi được một phần không khóa cả job: giữ file tạm dưới tên UUID cũ, ghi nhận giao dịch đã bỏ dở, thử lại bằng UUID mới. Không ghi đè/xóa dữ liệu tạm không chắc chắn. Cần dọn các artifact được ghi nhận này bằng quy trình giám sát khi không còn giao dịch hoạt động.
- Khi rename thứ hai bị từ chối hoặc phát sinh ngoại lệ, đối chiếu journal ngay và khôi phục bản rollback có hash đúng trước khi trả lỗi, nếu có thể. Mất kết nối, lỗi tài khoản hoặc hủy giữa publish vẫn có thể cần phục hồi sau; không có cam kết atomic FTP.
- Mọi chế độ, kể cả cấu hình cũ `Size`, đều xác minh SHA-256 của staging trước publish. File cùng kích thước nhưng sai nội dung không được đưa ra tên final.
- Kiểm tra checkpoint trước bước nhận file giống nhau. Source và remote cùng bị reset/thay đổi không được âm thầm ghi đè lịch sử đã xác nhận.
- Journal kiểm tra phase, tên temp/rollback theo ID, namespace, key và tính bất biến của thông tin giao dịch giữa các bản ghi. Retention từ chối đường dẫn backup không đúng sở hữu dù hash trùng.
- File state phiên bản mới hơn không bị xem là hỏng để tự hạ xuống backup cũ. INI có `SettingsVersion=1`; vẫn đọc cấu hình 1.0.0 chưa có version, từ chối version chưa hỗ trợ.
- Định danh job phiên bản 2 dùng JSON chuẩn hóa, bao gồm username và chế độ bảo mật. Không dùng chung checkpoint khi đổi account/jail trên cùng host và port.
- Nâng libcurl lên 8.23.0, pin SHA-256 và chỉ build FTP/FTPS. Giấy phép và inventory đã cập nhật. Xem THIRD_PARTY.md để biết nguồn chính thức và giới hạn đánh giá bảo mật.

## Nâng cấp từ 1.0.0

1. Giữ riêng bản cũ và sao lưu config/state/journal khi ứng dụng đã dừng.
2. Xử lý các giao dịch 1.0.0 chưa kết thúc bằng bản cũ và đúng cấu hình trước khi nâng cấp. Nếu còn giao dịch cũ, bản mới sẽ dừng an toàn vì job identity khác; không sửa/xóa journal để vượt qua chặn này.
3. Cấu hình INI cũ vẫn đọc được. Với checkpoint cũ đã hoàn tất, bản mới chỉ nhận remote khi nội dung snapshot local chứng minh giống hệt. Nếu local đã thay đổi, có thể cần quy trình xử lý conflict và backup thủ công; không tự chuyển quyền sở hữu từ account cũ sang account mới.
4. Chạy `BUILD_EXE.cmd` trong thư mục source mới để build Release x64. Không dùng các `.exe` kiểm thử như ứng dụng desktop.

Kết quả thực tế nằm trong `TEST_REPORT.md` và `test-results/`. Chưa có bản GUI chạy được được xác nhận trong môi trường phát triển này: thiếu VS2022/MFC. Hai nhánh kiểm tra Credential Manager và xác minh chứng chỉ FTPS còn bị giới hạn bởi phiên đăng nhập/Schannel. Đọc giới hạn trước khi triển khai.
