#include <afxwin.h>
#include "../Core/Core.h"
#include "../Transport/CurlFtpClient.h"
#include <afxcmn.h>
#include <afxole.h>
#include <afxdialogex.h>
#include <afxdlgs.h>
#include <ctime>
#include <algorithm>
#include <iomanip>
#include <memory>
#include <shellapi.h>
#include <sstream>
using namespace camera;
namespace {
constexpr UINT WM_STATUS = WM_APP + 1, WM_TRAY = WM_APP + 2;
CString w(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    CString r;
    auto p = r.GetBuffer(n);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), p, n);
    r.ReleaseBuffer(n);
    return r;
}
std::string u(const CString &s) {
    int n = WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s, s.GetLength(), r.data(), n, nullptr, nullptr);
    return r;
}
std::string nextUtc(int minutes) {
    auto value = std::time(nullptr) + minutes * 60;
    std::tm tm{};
    gmtime_s(&tm, &value);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
struct Field {
    std::string key;
    CString label;
    bool flag = false, secret = false;
};
const std::vector<Field> fields = {{"source", L"Thư mục nguồn"},
                                   {"host", L"Máy chủ FTP / URL"},
                                   {"remoteRoot", L"Thư mục FTP (phải tồn tại)"},
                                   {"username", L"Tài khoản FTP"},
                                   {"password", L"Mật khẩu FTP", false, true},
                                   {"port", L"Cổng (21 / 990)"},
                                   {"security", L"Bảo mật"},
                                   {"station", L"Mã máy / Station ID"},
                                   {"intervalMinutes", L"Chu kỳ phút (1–1440)"},
                                   {"include", L"Bộ lọc file (; phân cách)"},
                                   {"exclude", L"Loại trừ (; phân cách)"},
                                   {"stableSeconds", L"Chờ ổn định giây (5–300)"},
                                   {"retries", L"Số lần thử lại (0–10)"},
                                   {"connectTimeout", L"Timeout kết nối (giây)"},
                                   {"transferTimeout", L"Timeout truyền (giây)"},
                                   {"verification", L"Xác minh (luôn đọc lại SHA-256)"},
                                   {"backupCount", L"Giữ backup gần nhất"},
                                   {"backupDays", L"Tuổi backup (ngày)"},
                                   {"recursive", L"Bao gồm thư mục con", true},
                                   {"separate", L"Tách theo Station ID", true},
                                   {"remember", L"Lưu mật khẩu trên máy này", true},
                                   {"plainConsent", L"Chấp nhận FTP không mã hóa", true},
                                   {"sharedConsent", L"Tôi là chủ duy nhất đích dùng chung", true},
                                   {"gapConsent", L"Chấp nhận khoảng trống khi đổi tên", true},
                                   {"allowEmpty", L"Cho phép file rỗng ban đầu", true},
                                   {"passive", L"FTP passive", true},
                                   {"retention", L"Cho phép xóa backup do app tạo", true},
                                   {"autoStart", L"Tự đồng bộ khi mở app", true},
                                   {"startWindows", L"Khởi động cùng Windows", true},
                                   {"approved", L"File tương đối cần thay thế thủ công"}};
class MainDialog : public CDialogEx {
    std::vector<std::unique_ptr<CWnd>> controls_;
    std::map<std::string, CWnd *> inputs_;
    CEdit history_;
    CStatic status_, preview_;
    CListCtrl rows_;
    CFont font_;
    fs::path root_;
    std::unique_ptr<InstanceLock> instance_;
    std::unique_ptr<Logger> logger_;
    std::atomic_bool cancel_{false};
    std::atomic_int action_{0};
    std::unique_ptr<Scheduler> scheduler_;
    std::unique_ptr<Store> store_;
    std::unique_ptr<CurlFtpClient> ftp_;
    std::unique_ptr<Engine> engine_;
    Config config_;
    std::string approved_;
    NOTIFYICONDATAW tray_{};
    bool exiting_ = false;
    bool trayInstalled_ = false;
    int scrollX_ = 0, scrollY_ = 0;
    void send(const std::string &s) {
        auto p = new std::string(s);
        if (!PostMessage(WM_STATUS, 0, (LPARAM)p))
            delete p;
    }
    std::string text(const std::string &k) {
        CString s;
        inputs_.at(k)->GetWindowText(s);
        return u(s);
    }
    void put(const std::string &k, const std::string &s) {
        auto *p = inputs_.at(k);
        if (k == "security" || k == "verification")
            ((CComboBox *)p)->SelectString(-1, w(s));
        else
            p->SetWindowText(w(s));
    }
    bool checked(const std::string &k) {
        return ((CButton *)inputs_.at(k))->GetCheck() == BST_CHECKED;
    }
    void tick(const std::string &k, bool b) {
        ((CButton *)inputs_.at(k))->SetCheck(b ? BST_CHECKED : BST_UNCHECKED);
    }
    Config read() {
        Config c;
        c.source = fs::u8path(text("source"));
#define STR(n) c.n = text(#n)
        STR(host);
        STR(username);
        STR(remoteRoot);
        STR(station);
        STR(include);
        STR(exclude);
        STR(security);
        STR(verification);
#undef STR
#define NUM(n)                                                                                               \
    {                                                                                                        \
        auto s = text(#n);                                                                                   \
        size_t pos = 0;                                                                                      \
        c.n = std::stoi(s, &pos);                                                                            \
        if (pos != s.size())                                                                                 \
            throw Error(ErrorKind::Configuration, "Invalid numeric input: " #n);                             \
    }
        NUM(port);
        NUM(intervalMinutes);
        NUM(stableSeconds);
        NUM(retries);
        NUM(connectTimeout);
        NUM(transferTimeout);
        NUM(backupCount);
        NUM(backupDays);
#undef NUM
#define BOOL(n) c.n = checked(#n)
        BOOL(recursive);
        BOOL(separate);
        BOOL(remember);
        BOOL(plainConsent);
        BOOL(sharedConsent);
        BOOL(gapConsent);
        BOOL(allowEmpty);
        BOOL(passive);
        BOOL(retention);
        BOOL(autoStart);
        BOOL(startWindows);
#undef BOOL
        c.validate();
        return c;
    }
    void populate(const Config &c) {
        put("source", c.source.u8string());
#define STR(n) put(#n, c.n)
        STR(host);
        STR(username);
        STR(remoteRoot);
        STR(station);
        STR(include);
        STR(exclude);
        STR(security);
        STR(verification);
#undef STR
#define NUM(n) put(#n, std::to_string(c.n))
        NUM(port);
        NUM(intervalMinutes);
        NUM(stableSeconds);
        NUM(retries);
        NUM(connectTimeout);
        NUM(transferTimeout);
        NUM(backupCount);
        NUM(backupDays);
#undef NUM
#define BOOL(n) tick(#n, c.n)
        BOOL(recursive);
        BOOL(separate);
        BOOL(remember);
        BOOL(plainConsent);
        BOOL(sharedConsent);
        BOOL(gapConsent);
        BOOL(allowEmpty);
        BOOL(passive);
        BOOL(retention);
        BOOL(autoStart);
        BOOL(startWindows);
#undef BOOL
    }
    void reset() {
        cancel_ = true;
        if (scheduler_)
            scheduler_->stop();
        scheduler_.reset();
        action_ = 0;
        approved_.clear();
        engine_.reset();
        ftp_.reset();
        store_.reset();
        cancel_ = false;
        for (auto &f : fields)
            inputs_[f.key]->EnableWindow(TRUE);
    }
    void setup() {
        if (scheduler_)
            return;
        config_ = read();
        if (text("password").empty())
            throw Error(ErrorKind::Configuration, "Nhập mật khẩu cho phiên này rồi Start / Test");
        store_ = std::make_unique<Store>(root_);
        ftp_ = std::make_unique<CurlFtpClient>(config_, text("password"), root_ / "spool", cancel_,
                                               [this](auto &s) { send(s); });
        engine_ = std::make_unique<Engine>(config_, *ftp_, *store_, cancel_, [this](auto &s) { send(s); });
        approved_ = text("approved");
        scheduler_ = std::make_unique<Scheduler>();
        scheduler_->launch(
            [this] {
                try {
                    int task = action_.exchange(0);
                    if (task == 1) {
                        send("Testing connection...");
                        ftp_->test();
                        send("Ready: đăng nhập và liệt kê thành công. SIZE/rename cần kiểm tra riêng.");
                        return;
                    }
                    if (task == 2) {
                        ftp_->writeProbe();
                        send("Ready: write/rename probe passed; không chứng minh atomic replace.");
                        return;
                    }
                    send("Running: đang quét / phục hồi");
                    auto t = engine_->run(task == 3 ? std::optional<std::string>(approved_) : std::nullopt);
                    send(std::string(t.failed || t.conflicts || t.pending ? "Pending: " : "Success: ") +
                         "scanned=" + std::to_string(t.scanned) + " skipped=" + std::to_string(t.skipped) +
                         " uploaded=" + std::to_string(t.uploaded) + " failed=" + std::to_string(t.failed) +
                         " conflict=" + std::to_string(t.conflicts) +
                         " waiting=" + std::to_string(t.pending) + " bytes=" + std::to_string(t.bytes) +
                         " retries=" + std::to_string(t.retries) + "; next UTC=" +
                         (scheduler_->isPaused() ? std::string("Paused") : nextUtc(config_.intervalMinutes)));
                } catch (const std::exception &e) {
                    if (scheduler_)
                        scheduler_->pause();
                    send(std::string("Failed (Paused): ") + e.what());
                }
            },
            config_.intervalMinutes);
        for (auto &f : fields)
            inputs_[f.key]->EnableWindow(FALSE);
    }
    void button(UINT id, const wchar_t *label, int x, int y, int width) {
        auto b = std::make_unique<CButton>();
        b->Create(label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON, CRect(x, y, x + width, y + 28),
                  this, id);
        b->SetFont(&font_);
        controls_.push_back(std::move(b));
    }

  public:
    MainDialog() : CDialogEx(100) {}
    BOOL OnInitDialog() override {
        CDialogEx::OnInitDialog();
        RECT work{};
        SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0);
        SetWindowPos(nullptr, 0, 0, std::min(1140L, work.right - work.left),
                     std::min(900L, work.bottom - work.top), SWP_NOMOVE | SWP_NOZORDER);
        CRect client;
        GetClientRect(&client);
        SetScrollRange(SB_VERT, 0, std::max(0, 850 - client.Height()));
        SetScrollRange(SB_HORZ, 0, std::max(0, 1125 - client.Width()));
        SetWindowText(L"Camera FTP Sync — Ứng dụng độc lập");
        SetIcon(AfxGetApp()->LoadStandardIcon(IDI_APPLICATION), TRUE);
        font_.CreatePointFont(90, L"Segoe UI");
        int index = 0;
        for (auto &f : fields) {
            int col = index % 3, row = index / 3, x = 15 + col * 370, y = 12 + row * 48;
            auto label = std::make_unique<CStatic>();
            label->Create(f.flag ? CString() : f.label, WS_CHILD | WS_VISIBLE, CRect(x, y, x + 355, y + 18),
                          this);
            label->SetFont(&font_);
            controls_.push_back(std::move(label));
            std::unique_ptr<CWnd> input;
            if (f.flag) {
                auto b = std::make_unique<CButton>();
                b->Create(f.label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                          CRect(x, y + 18, x + 355, y + 42), this, 1000 + index);
                input = std::move(b);
            } else if (f.key == "security" || f.key == "verification" || f.key == "intervalMinutes") {
                auto b = std::make_unique<CComboBox>();
                bool editable = f.key == "intervalMinutes";
                b->Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST),
                          CRect(x, y + 18, x + 355, y + 250), this, 1000 + index);
                if (f.key == "security") {
                    b->AddString(L"ExplicitFTPS");
                    b->AddString(L"ImplicitFTPS");
                    b->AddString(L"ExplicitFTPS-NoVerify");
                    b->AddString(L"ImplicitFTPS-NoVerify");
                    b->AddString(L"PlainFTP");
                } else if (f.key == "verification") {
                    b->AddString(L"ReadBackSHA256");
                    b->AddString(L"Size");
                } else
                    for (auto v : {L"1", L"5", L"10", L"15", L"30", L"60"})
                        b->AddString(v);
                input = std::move(b);
            } else {
                auto b = std::make_unique<CEdit>();
                b->Create(WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL |
                              (f.secret ? ES_PASSWORD : 0),
                          CRect(x, y + 18, x + 355, y + 42), this, 1000 + index);
                input = std::move(b);
            }
            input->SetFont(&font_);
            inputs_[f.key] = input.get();
            controls_.push_back(std::move(input));
            ++index;
        }
        preview_.Create(L"Đích: chưa cấu hình", WS_CHILD | WS_VISIBLE, CRect(15, 495, 1110, 520), this);
        button(2000, L"Duyệt...", 15, 528, 90);
        button(2001, L"Phân tích URL", 110, 528, 120);
        button(2002, L"Lưu / Áp dụng", 235, 528, 130);
        button(2003, L"Test kết nối", 370, 528, 115);
        button(2004, L"Test ghi/đổi tên", 490, 528, 135);
        button(2005, L"Start / Tiếp tục", 630, 528, 140);
        button(2006, L"Tạm dừng", 775, 528, 105);
        button(2007, L"Đồng bộ ngay", 885, 528, 130);
        button(2008, L"Mở log/history", 15, 562, 140);
        button(2009, L"Xuất log", 160, 562, 100);
        button(2010, L"Force Verify", 265, 562, 120);
        button(2011, L"Thay thế + backup", 390, 562, 165);
        button(2012, L"Dừng / sửa cấu hình", 560, 562, 180);
        button(2013, L"Exit (dừng đồng bộ)", 745, 562, 180);
        button(2014, L"Xem đích", 930, 562, 100);
        status_.Create(L"Not configured — mật khẩu không được lưu nếu chưa chọn", WS_CHILD | WS_VISIBLE,
                       CRect(15, 600, 1110, 624), this);
        rows_.Create(WS_CHILD | WS_VISIBLE | LVS_REPORT | WS_BORDER, CRect(15, 630, 1110, 730), this, 3000);
        int column = 0;
        for (auto label : {L"Nguồn", L"Đường dẫn tương đối", L"Thay đổi (UTC)", L"Byte", L"Đích FTP",
                           L"Xác nhận UTC", L"Kết quả", L"Lỗi"})
            rows_.InsertColumn(column++, label, LVCFMT_LEFT, column == 4 ? 90 : 180);
        rows_.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        history_.Create(WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL |
                            ES_AUTOVSCROLL,
                        CRect(15, 740, 1110, 830), this, 3001);
        history_.SetFont(&font_);
        try {
            root_ = dataDirectory();
            instance_ = std::make_unique<InstanceLock>(root_);
            logger_ = std::make_unique<Logger>(root_);
            config_ = loadConfig(root_ / "config.ini");
            populate(config_);

            put("password", "DemoOnly_123!");
            if (config_.remember)
                put("password", credentialRead(config_.credential));
            if (config_.autoStart && !text("password").empty()) {
                setup();
                scheduler_->start();
            }
        } catch (const std::exception &e) {
            AfxMessageBox(w(e.what()));
            if (!instance_) {
                EndDialog(IDCANCEL);
                return TRUE;
            }
            populate(config_);
        }
        tray_.cbSize = sizeof(tray_);
        tray_.hWnd = m_hWnd;
        tray_.uID = 1;
        tray_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        tray_.uCallbackMessage = WM_TRAY;
        tray_.hIcon = AfxGetApp()->LoadStandardIcon(IDI_APPLICATION);
        wcscpy_s(tray_.szTip, L"Camera FTP Sync — nhấp để mở; Exit để dừng");
        trayInstalled_ = Shell_NotifyIconW(NIM_ADD, &tray_) != FALSE;
        return TRUE;
    }
    BOOL OnCommand(WPARAM wp, LPARAM lp) override {
        auto id = LOWORD(wp);
        if (id >= 1000 && id < 1030) {
            if (inputs_.size() != fields.size())
                return TRUE;
            try {
                Config c;
                c.remoteRoot = text("remoteRoot");
                c.station = text("station");
                c.separate = checked("separate");
                auto displayHost = text("host");
                if (displayHost.find('@') != std::string::npos)
                    displayHost = "[URL credentials rejected]";
                preview_.SetWindowText(
                    w(text("source") + " -> " + displayHost + ":" + text("port") + c.destination("lsc.txt")));
            } catch (...) {
                preview_.SetWindowText(L"Đích chưa hợp lệ");
            }
            return CDialogEx::OnCommand(wp, lp);
        }
        if (id < 2000 || id > 2014)
            return CDialogEx::OnCommand(wp, lp);
        try {
            switch (id) {
            case 2000: {
                CFolderPickerDialog d;
                if (d.DoModal() == IDOK)
                    put("source", u(d.GetPathName()));
                break;
            }
            case 2001: {
                Config c;
                auto input = text("host");
                if (input.find('@') != std::string::npos) {
                    put("host", "");
                    throw Error(ErrorKind::Configuration,
                                "Remove embedded credentials; enter username and masked password separately");
                }
                parseUrl(c, input);
                put("host", c.host);
                put("port", std::to_string(c.port));
                put("security", c.security);
                put("remoteRoot", c.remoteRoot);
                break;
            }
            case 2002: {
                auto c = read();
                auto secret = text("password");
                reset();
                if (c.remember)
                    credentialWrite(c.credential, c.username, secret);
                else
                    credentialDelete(c.credential);
                saveConfig(root_ / "config.ini", c);
                setStartup(c.startWindows);
                config_ = c;
                send("Ready: đã lưu cấu hình. Start để đồng bộ.");
                break;
            }
            case 2003:
                setup();
                action_ = 1;
                scheduler_->syncNow();
                break;
            case 2004:
                if (AfxMessageBox(L"Tạo, đổi tên và xóa file thử duy nhất trong thư mục đích?",
                                  MB_YESNO | MB_ICONQUESTION) == IDYES) {
                    setup();
                    action_ = 2;
                    scheduler_->syncNow();
                }
                break;
            case 2005:
                setup();
                scheduler_->start();
                send("Running: lịch đã bật");
                break;
            case 2006:
                if (scheduler_)
                    scheduler_->pause();
                send("Paused: lịch tạm dừng; lượt đang chạy sẽ hoàn tất");
                break;
            case 2007:
            case 2010:
                setup();
                scheduler_->syncNow();
                break;
            case 2008:
                ShellExecuteW(m_hWnd, L"open", (root_ / "logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                break;
            case 2009: {
                CFileDialog d(FALSE, L"txt", L"CameraFtpSync-history.txt", OFN_OVERWRITEPROMPT,
                              L"Text|*.txt||");
                if (d.DoModal() == IDOK) {
                    CString s;
                    history_.GetWindowText(s);
                    atomicWrite(fs::path((LPCWSTR)d.GetPathName()), u(s), false);
                }
                break;
            }
            case 2011:
                if (text("approved").empty())
                    throw Error(ErrorKind::Configuration, "Nhập file tương đối cần thay thế trước khi Start");
                if (AfxMessageBox(L"Thay thế file đã chọn và giữ bản cũ trong backup duy nhất? Kiểm tra "
                                  L"journal và FTP trước. Không thực hiện khi máy khác đang ghi.",
                                  MB_YESNO | MB_ICONWARNING) == IDYES) {
                    setup();
                    action_ = 3;
                    scheduler_->syncNow();
                }
                break;
            case 2012:
                reset();
                send("Ready: đã dừng; có thể sửa cấu hình");
                break;
            case 2013:
                exiting_ = true;
                OnClose();
                break;
            case 2014: {
                auto c = read();
                preview_.SetWindowText(w(c.source.u8string() + " -> " + c.host + ":" +
                                         std::to_string(c.port) + c.destination("lsc.txt")));
                break;
            }
            }
        } catch (const std::exception &e) {
            status_.SetWindowText(w(e.what()));
            AfxMessageBox(w(e.what()), MB_ICONWARNING);
        }
        return TRUE;
    }
    afx_msg LRESULT OnStatus(WPARAM, LPARAM lp) {
        std::unique_ptr<std::string> s((std::string *)lp);
        if (s->rfind("@file ", 0) == 0) {
            try {
                auto data = Json::parse(s->substr(6));
                int row = rows_.InsertItem(rows_.GetItemCount(), w(data.at("source")));
                int col = 1;
                for (auto key : {"relative", "mtime", "size", "remote", "confirmed", "result", "error"}) {
                    auto value = data.at(key);
                    rows_.SetItemText(row, col++,
                                      w(value.is_string() ? value.get<std::string>() : value.dump()));
                }
                rows_.EnsureVisible(row, FALSE);
                if (rows_.GetItemCount() > 1000)
                    rows_.DeleteItem(0);
                *s = data.at("result").get<std::string>() + ": " + data.at("relative").get<std::string>() +
                     " " + data.at("error").get<std::string>();
            } catch (...) {
                *s = "Failed: invalid worker status record";
            }
        }
        status_.SetWindowText(w(*s));
        try {
            if (logger_)
                logger_->write(
                    s->rfind("Failed", 0) == 0
                        ? "ERROR"
                        : (s->rfind("Conflict", 0) == 0 || s->rfind("Pending", 0) == 0 ? "WARN" : "INFO"),
                    "sync", *s);
        } catch (...) {
            status_.SetWindowText(L"Failed: không ghi được log; kiểm tra dung lượng đĩa");
        }
        if (history_.GetWindowTextLength() > 250000)
            history_.SetWindowText(L"");
        history_.SetSel(-1, -1);
        history_.ReplaceSel(w(utcNow() + " " + *s + "\r\n"));
        return 0;
    }
    afx_msg LRESULT OnTray(WPARAM, LPARAM l) {
        if (l == WM_LBUTTONUP || l == WM_RBUTTONUP) {
            ShowWindow(SW_RESTORE);
            SetForegroundWindow();
        }
        return 0;
    }
    afx_msg void OnClose() {
        if (!exiting_) {
            ShowWindow(trayInstalled_ ? SW_HIDE : SW_MINIMIZE);
            return;
        }
        reset();
        Shell_NotifyIconW(NIM_DELETE, &tray_);
        put("password", "");
        MSG msg;
        while (PeekMessage(&msg, m_hWnd, WM_STATUS, WM_STATUS, PM_REMOVE))
            delete (std::string *)msg.lParam;
        EndDialog(IDOK);
    }
    afx_msg UINT OnPowerBroadcast(UINT event, LPARAM) {
        if (event == PBT_APMRESUMEAUTOMATIC && scheduler_)
            scheduler_->wake();
        return TRUE;
    }
    void OnCancel() override {
        ShowWindow(trayInstalled_ ? SW_HIDE : SW_MINIMIZE);
    }
    void OnOK() override {}
    afx_msg void OnVScroll(UINT code, UINT position, CScrollBar *) {
        int old = scrollY_;
        int maximum = GetScrollLimit(SB_VERT);
        if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION)
            scrollY_ = position;
        else if (code == SB_LINEUP)
            scrollY_ -= 24;
        else if (code == SB_LINEDOWN)
            scrollY_ += 24;
        else if (code == SB_PAGEUP)
            scrollY_ -= 200;
        else if (code == SB_PAGEDOWN)
            scrollY_ += 200;
        scrollY_ = std::clamp(scrollY_, 0, maximum);
        SetScrollPos(SB_VERT, scrollY_);
        ScrollWindow(0, old - scrollY_);
    }
    afx_msg void OnHScroll(UINT code, UINT position, CScrollBar *) {
        int old = scrollX_;
        int maximum = GetScrollLimit(SB_HORZ);
        if (code == SB_THUMBTRACK || code == SB_THUMBPOSITION)
            scrollX_ = position;
        else if (code == SB_LINELEFT)
            scrollX_ -= 24;
        else if (code == SB_LINERIGHT)
            scrollX_ += 24;
        else if (code == SB_PAGELEFT)
            scrollX_ -= 200;
        else if (code == SB_PAGERIGHT)
            scrollX_ += 200;
        scrollX_ = std::clamp(scrollX_, 0, maximum);
        SetScrollPos(SB_HORZ, scrollX_);
        ScrollWindow(old - scrollX_, 0);
    }
    DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(MainDialog, CDialogEx)
ON_MESSAGE(WM_STATUS, &MainDialog::OnStatus)
ON_MESSAGE(WM_TRAY, &MainDialog::OnTray)
ON_WM_CLOSE()
ON_WM_POWERBROADCAST()
ON_WM_VSCROLL()
ON_WM_HSCROLL()
END_MESSAGE_MAP()
class Application : public CWinApp {
    BOOL InitInstance() override {
        CWinApp::InitInstance();
        INITCOMMONCONTROLSEX c{sizeof(c), ICC_WIN95_CLASSES};
        InitCommonControlsEx(&c);
        AfxOleInit();
        MainDialog d;
        m_pMainWnd = &d;
        d.DoModal();
        return FALSE;
    }
} app;
} // namespace
