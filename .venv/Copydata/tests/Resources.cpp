#include "Core.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <iostream>
#include <windows.h>
#include <psapi.h>
#include <winioctl.h>
using namespace camera;
int main() {
    auto root = fs::temp_directory_path() / fs::u8path("camera-resources-" + uniqueId());
    try {
        fs::create_directories(root);
        auto file = root / "large.bin";
        HANDLE h =
            CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            throw std::runtime_error("create failed");
        DWORD n = 0;
        if (!DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &n, nullptr)) {
            CloseHandle(h);
            throw std::runtime_error("sparse files unavailable");
        }
        LARGE_INTEGER size;
        size.QuadPart = (1ULL << 32) + 65536;
        if (!SetFilePointerEx(h, size, nullptr, FILE_BEGIN) || !SetEndOfFile(h)) {
            CloseHandle(h);
            throw std::runtime_error("64-bit size failed");
        }
        CloseHandle(h);
        if (fs::file_size(file) != (1ULL << 32) + 65536)
            throw std::runtime_error("size overflow");
        auto before = std::chrono::steady_clock::now();
        auto hash = hashFile(file);
        PROCESS_MEMORY_COUNTERS mem{};
        GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem));
        std::cout << "PASS large_file_bytes=" << fs::file_size(file) << " sha256=" << hash
                  << " peak_working_set=" << mem.PeakWorkingSetSize << " elapsed_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                           before)
                         .count()
                  << '\n';
        Json files = Json::object();
        for (int i = 0; i < 1000; ++i)
            files["job|file" + std::to_string(i)] = {{"size", uint64_t(i)}, {"hash", hash}};
        before = std::chrono::steady_clock::now();
        atomicWrite(root / "benchmark.json", files.dump());
        std::cout << "PASS 1000_metadata_entries bytes=" << fs::file_size(root / "benchmark.json")
                  << " atomic_save_ms="
                  << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                           before)
                         .count()
                  << '\n';
        fs::remove_all(root);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
}
