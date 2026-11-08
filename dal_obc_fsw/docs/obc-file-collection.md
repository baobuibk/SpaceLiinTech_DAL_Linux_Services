# Thu thập file trên OBC bằng DALTUI và command

Trang này hướng dẫn lấy file **đã được lưu trên OBC** về Pi/PC qua RS-422, hoặc thao tác ngay trên OBC qua console socket. Áp dụng cho FSW và DALTUI đã cập nhật **ICD v1.2, Data Transfer `TYPE=0x03`**.

Có hai bước khác nhau:

1. **Tạo/thu thập dữ liệu vào OBC:** chụp camera, ghi housekeeping hoặc nhận object từ EXP. File gốc được lưu trên OBC.
2. **Tải file khỏi OBC:** List → Select → Download → kiểm tra CRC32 → Deselect. File tải về nằm trên máy chạy DALTUI hoặc PC command client.

`dat_lst` không chụp ảnh, không khởi động bộ thu thập EXP và không tự tải file. Không cần dừng FSW để tải; FSW phải đang chạy.

## 1. Kiểm tra file nguồn trên OBC

Trên terminal OBC, dùng tài khoản root:

```sh
systemctl start dal_obc_fsw
ls -l /home/bee/data/logs/hk/
ls -l /home/bee/data/images/exp/
```

Đường dẫn mặc định là `/home/bee/data`; thay bằng `DAL_DATA_DIR` nếu đã cấu hình root khác. Một thư mục có thể chưa tồn tại nếu chưa có file được tạo.

| Data type | Tên trong DALTUI | Các folder dưới root dữ liệu OBC |
| --- | --- | --- |
| `0` | `HK (housekeeping)` | `logs/hk/` |
| `1` | `SCI1 (images)` | `images/usb/`, `images/mipi/`, `images/exp/` |
| `2` | `SCI2 (LWL/PC logs)` | `logs/lwl/`, `logs/pc/` |

Ảnh nhiệt EXP dạng `.bin` vẫn thuộc **type 1**, còn log LWL và PC dạng `.bin` thuộc **type 2**. File được phân loại theo folder, không theo phần mở rộng. Các folder `data/lev/` và `data/other/` không còn được đưa vào danh sách TYPE `03`; file gốc không bị di chuyển hay xóa. Sau khi cập nhật FSW, chạy List lại vì type/index của log đã thay đổi. Xem [tên file và folder chi tiết](flight-command-register.md#file-types-and-storage-folders-on-the-obc).

### Nếu ảnh EXP chưa xuất hiện trên OBC

Kiểm tra collector và hàng đợi EXP:

```sh
dal_flight_console exp_csp_snapshot 6
journalctl -u dal_obc_fsw -n 100 --no-pager | grep EXP-DATA
pid=$(systemctl show dal_obc_fsw -p MainPID --value)
tr '\0' '\n' < /proc/"$pid"/environ | grep DAL_EXP_DATA_
```

Nếu process đang có `DAL_EXP_DATA_ENABLE=0`, sửa giá trị thành `1` trong `/etc/default/dal-obc-fsw`, rồi restart FSW để áp dụng. Không restart giữa một phiên tải file.

```sh
systemctl restart dal_obc_fsw
```

Chờ log `saved .../images/exp/...` rồi mới lập danh sách file trên OBC. `pending` trên EXP hoặc `waiting for fsw` không có nghĩa file đã nằm trên OBC. `DAL_EXP_DATA_DIR`, nếu đặt riêng, cần trỏ cùng root với `DAL_DATA_DIR` để các file collector lưu xuất hiện trong danh sách TYPE `03`.

## 2. Tải bằng DALTUI trên Pi

### Mở giao diện và chọn folder nhận

Ví dụ đã giải nén gói vào `$HOME/daltui-pi-arm64` và adapter RS-422 là `/dev/ttyUSB0`:

```sh
mkdir -p "$HOME/dal-downloads"
DAL_TUI_DOWNLOAD_DIR="$HOME/dal-downloads" \
TERM=xterm-256color "$HOME/daltui-pi-arm64/daltui" \
  --rs422 /dev/ttyUSB0 --baud 921600 --poll
```

Thay đường dẫn chương trình và cổng adapter theo Pi thực tế. User chạy DALTUI cần quyền đọc/ghi serial và ghi folder nhận.

Nếu không đặt `DAL_TUI_DOWNLOAD_DIR`, DALTUI lưu vào `./downloads` tính từ thư mục làm việc khi mở chương trình. `--poll` đọc housekeeping định kỳ; các nút tải file vẫn hoạt động trong control-only hoặc `--manual`.

### Thao tác OBC → Data transfer

Chuyển sang TAB **OBC**, dùng `,` / `.` chọn **Data transfer**. Dùng `↑` / `↓` chọn dòng và `Enter` để EDIT/SELECT/RUN.

| Bước | Thao tác ở Control | Kiểm tra ở Status |
| ---: | --- | --- |
| 1 | **Mark filter [EDIT]** = `255`, rồi **List files [RUN]** | Số lượng `HK`, `SCI1`, `SCI2`; filter đã gửi |
| 2 | Chọn **Data type [SELECT]** | Dùng số lượng của đúng loại đã chọn |
| 3 | Nhập **File index [EDIT]**, bắt đầu từ `0`; **Chunk size [EDIT]** = `4096 B` | Các giá trị EDIT chỉ là draft, chưa đổi metadata thực |
| 4 | **Select file [RUN]** | Tên file, type/index, kích thước, chunk size/count và CRC32 dự kiến |
| 5 | **Download file [RUN]** | `Received` tăng; file đang tải có đuôi `.part` |
| 6 | Chờ hoàn tất | **DONE: saved, CRC32 verified** và đường dẫn **Local file** |
| 7 | Tùy chọn: **Mark value [EDIT]** = `1`, rồi **Mark selected [RUN]** | ACK xác nhận mark được áp dụng |
| 8 | **Deselect file [RUN]** | Selection được giải phóng; file đã tải vẫn còn trên Pi |

Ví dụ `SCI1 = 5`: chọn `SCI1 (images)`, index hợp lệ là `0..4`. `List files` trả số lượng, không trả danh sách tên. Muốn tìm đúng ảnh, lần lượt đổi index và chạy **Select file**, đọc tên/kích thước trong Status trước khi tải.

Tên trong metadata tối đa 20 byte nên có thể bị cắt ngắn; nội dung file tải về vẫn đầy đủ. Index được tính riêng theo type và phụ thuộc danh sách vừa lập. Chạy List lại hoặc xóa file có thể làm index thay đổi.

### Tạm dừng, đọc từng chunk và kiểm tra file

- **Read next chunk [RUN]:** đọc một chunk kế tiếp, ghi nối tiếp vào file `.part`. Không nhập chunk index riêng trong DALTUI.
- **Pause download [RUN]:** dừng sau chunk đang chờ; **Download file** tiếp tục phần còn lại trong phiên hiện tại.
- File chỉ được công nhận hoàn tất sau khi khớp kích thước và CRC32. File rỗng có 0 chunk và được lưu/kiểm tra mà không gửi `dat_get`.
- Nếu lỗi, đổi kết nối hoặc đóng DALTUI: List/Select lại rồi tải lại. Không có resume `.part` qua phiên mới.

Trên Pi, kiểm tra folder đã cấu hình:

```sh
ls -lt "$HOME/dal-downloads"
```

DALTUI tạo tên nhận riêng gồm type/index và tên metadata, không ghi đè file nhận đã có. Tải xong không tự xóa file gốc trên OBC. Định dạng vẫn giữ nguyên: ảnh nhiệt `.bin` chưa tự chuyển thành PNG/JPEG.

## 3. Chạy DALTUI ngay trên OBC

Chạy bằng root, dùng đường dẫn đầy đủ:

```sh
systemctl start dal_obc_fsw
mkdir -p /home/root/dal-downloads
DAL_TUI_DOWNLOAD_DIR=/home/root/dal-downloads \
TERM=xterm-256color /home/root/tools/bin/daltui \
  --console /run/dal/dal_obc_fsw/console.sock --poll
```

Thực hiện cùng các bước ở mục 2. Trong trường hợp này **cả file gốc và bản tải đều ở OBC**; bản tải nằm trong `/home/root/dal-downloads`. Muốn lưu trên Pi, chạy DALTUI trên Pi qua RS-422.

## 4. Thao tác từng lệnh bằng console trên OBC

Các lệnh sau chạy ở shell OBC. Ví dụ chọn ảnh type `1`, index `0`, chunk `4096 B`:

```sh
dal_flight_console dat_lst 255
dal_flight_console dat_sel 1 0 4096
```

Đọc metadata trả về trước khi tiếp tục. Để xem payload theo JSON:

```sh
dal_flight_console --json dat_sel 1 0 4096
```

Metadata `payload_hex` chứa type, index, chunk size, tên, kích thước, CRC32 và chunk count. Xem [bảng offset metadata](icd-v1p2-console-mapping.md#selection-metadata-0301).

Đọc từng chunk, bắt đầu từ `0`:

```sh
dal_flight_console --json dat_get 0
```

Tiếp tục `dat_get 1`, `dat_get 2`, ... đến `chunk_count - 1`; chỉ gửi các index có thật. File rỗng không có chunk để đọc.

**Console trả dữ liệu dạng text/JSON, không tự ghép và lưu file nhị phân.** Redirect `dal_flight_console dat_get 0 > image.bin` sẽ ghi phần text trả lời, không tạo ảnh đúng. Client tự động phải giải mã `payload_hex` của từng TM thành byte, ghép đúng thứ tự, kiểm tra tổng kích thước và CRC32. Dùng DALTUI hoặc PC REPL ở mục 5 để tải tự động; `dat_download` không phải lệnh của `dal_flight_console`.

Sau khi đã tải đủ và xác minh file, có thể đánh dấu, rồi kết thúc:

```sh
# Optional: mark the currently selected file as 1.
dal_flight_console dat_mrk 1

# Release the selected file.
dal_flight_console dat_des
```

`dat_mark` là alias của `dat_mrk`. Không gửi lệnh List/Select từ console khi DALTUI đang tải file khác: selection của FSW được dùng chung giữa các client.

## 5. Tải tự động bằng command client qua RS-422 trên Pi/PC

Đây là Python REPL đi kèm repository, không phải `dal_flight_console` và không được đóng gói trong tar DALTUI. Cần folder `tool/pc_client` với script và package `bee_dal_client` bên cạnh.

Ví dụ đã đặt folder đó ở `$HOME/dal-pc-client`. Cài dependency vào virtual environment:

```sh
python3 -m venv "$HOME/dal-client-venv"
"$HOME/dal-client-venv/bin/python" -m pip install \
  -r "$HOME/dal-pc-client/requirements.txt"
```

Mở client từ một **folder nhận mới**, vì client này dùng tên metadata làm tên file và có thể ghi đè file cùng tên trong thư mục hiện tại:

```sh
mkdir -p "$HOME/dal-downloads"
receive_dir=$(mktemp -d "$HOME/dal-downloads/session.XXXXXX")
cd "$receive_dir"
"$HOME/dal-client-venv/bin/python" \
  "$HOME/dal-pc-client/bee_dal_repl.py" /dev/ttyUSB0 --baud 921600
```

Đóng DALTUI đang dùng cùng cổng trước khi mở client. Tại prompt `bee-dal>`, thực hiện:

```text
ping
dat_lst 255
dat_sel 1 0 4096
dat_download
```

`dat_download` là tiện ích phía PC: lặp `dat_get`, ghép payload, tính CRC32 và ghi file vào thư mục làm việc. Nó không thêm command/frame mới vào ICD.

Kiểm tra đủ các điều kiện:

- Không có `size mismatch` hoặc `download aborted`.
- Dòng CRC32 hiện **OK**, khớp CRC32 từ `dat_sel`.
- Có dòng `saved -> ...` chỉ đường dẫn file nhận.

**Khác với DALTUI, PC REPL hiện vẫn ghi file khi CRC mismatch.** Vì vậy riêng dòng `saved` chưa đủ để kết luận tải thành công. Nếu mismatch, không đánh dấu file là đã nhận; List/Select và tải lại trong folder nhận mới. REPL ghép toàn bộ file trong RAM; ưu tiên DALTUI khi tải file lớn.

Sau khi kiểm tra thành công:

```text
dat_mrk 1
dat_des
quit
```

Mark là tùy chọn. Để tải file tiếp theo, có thể giữ cùng danh sách và chọn index khác; tạo folder nhận mới nếu tên metadata trùng/cùng tiền tố 20 byte.

## 6. Mark, lọc và xóa file

| Giá trị | Dùng cho `dat_lst` | Dùng cho `dat_mrk` |
| --- | --- | --- |
| `0` | Chỉ file chưa đánh dấu | Gỡ mark |
| `1..254` | Chỉ file có đúng mark này | Gán nhãn tương ứng |
| `255` | Tất cả file | Không hợp lệ trong triển khai hiện tại |

Quy ước vận hành có thể dùng mark `1` cho file đã nhận và kiểm tra. Đây là nhãn do người vận hành gán, không phải bằng chứng CRC tự động do OBC kiểm tra.

**Xóa là bước riêng, không bắt buộc để tải.** Trên DALTUI, Select lại đúng file, kiểm tra Status rồi chọn **Delete selected file [!]** và xác nhận `Y`. Với command, `dat_del <type> <index>` xóa ngay theo index của danh sách hiện tại; không có hộp xác nhận:

```sh
# Only after listing/selecting and verifying this exact target:
dal_flight_console dat_del 1 0
```

Lệnh trên xóa file ảnh index `0` trên OBC, không phải file nhận trên Pi. Sau khi xóa cần lập danh sách lại trước khi chọn mục tiếp theo. FSW từ chối xóa file HK CSV của ngày hiện tại.

## 7. Khi chưa tải được

| Hiện tượng | Cách kiểm tra/xử lý |
| --- | --- |
| `SCI1 = 0` sau Capture | Kiểm tra capture/EXP collector đã lưu file trên OBC; kiểm tra đúng root/folder; List lại |
| Select báo lỗi | Kiểm tra type/index và số lượng; List lại nếu file đã thay đổi; dùng FSW và client v1.2 đồng bộ |
| Tên ảnh bị cắt | Metadata chỉ có STR(20); đối chiếu type/index, kích thước và CRC32 |
| File vẫn là `.part` | Chưa hoàn tất hoặc xác minh thất bại; xem State, dung lượng và quyền ghi folder nhận |
| CRC mismatch | Dùng duy nhất một client; chọn file đã ngừng ghi; List/Select và tải lại |
| Không tìm thấy file trên Pi | Xem `Local file`; xác định DALTUI đang chạy trên Pi hay OBC và folder làm việc lúc mở |
| `.bin` không mở bằng trình xem ảnh | Transfer giữ nguyên dữ liệu; cần decoder đúng encoding/geometry của ảnh nhiệt |

## Tham chiếu

- [Data Transfer v1.2: TC/TM, CRC và ACK package](icd-v1p2-console-mapping.md#data-transfer-v12-type-0x03).
- [Flight command register và folder nguồn](flight-command-register.md#file-types-and-storage-folders-on-the-obc).
- [Chạy DALTUI trên host, OBC và RS-422](daltui-running.md).
- [Xem log flight software](flight-software-logs.md).
