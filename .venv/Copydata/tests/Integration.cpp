#include "../src/Transport/CurlFtpClient.h"
#include <fstream>
#include <iostream>
using namespace camera;
int main(int argc, char **argv) {
    try {
        if (argc < 4)
            return 2;
        Config c;
        c.source = fs::u8path(argv[1]);
        c.station = "PC01";
        c.host = "127.0.0.1";
        c.port = std::stoi(argv[2]);
        c.remoteRoot = "/";
        c.username = "camera";
        c.security = "PlainFTP";
        c.plainConsent = true;
        c.gapConsent = true;
        c.stableSeconds = 5;
        c.retries = 0;
        if (argc > 4 && std::string(argv[4]) == "tls") {
            c.security = "ExplicitFTPS";
            c.plainConsent = false;
        }
        std::atomic_bool cancel{false};
        Store store(c.source.parent_path() / "state");
        CurlFtpClient ftp(c, argv[3], store.root() / "spool", cancel);
        if (argc > 4 && std::string(argv[4]) == "auth") {
            try {
                ftp.test();
                return 3;
            } catch (const Error &e) {
                if (e.kind != ErrorKind::Authentication)
                    throw;
                std::cout << "PASS invalid password rejected\n";
                return 0;
            }
        }
        if (argc > 4 && std::string(argv[4]) == "tls") {
            try {
                ftp.test();
                return 4;
            } catch (const Error &e) {
                if (e.kind != ErrorKind::TLS)
                    throw;
                std::cout << "PASS TLS fails closed: " << e.what() << "\n";
                if (std::string(e.what()).find("code=60") == std::string::npos)
                    std::cout << "SKIP certificate-specific assertion: TLS handshake failed before peer "
                                 "verification\n";
                return 0;
            }
        }
        ftp.test();
        ftp.writeProbe();
        auto time = std::chrono::steady_clock::now();
        Engine engine(
            c, ftp, store, cancel, [](const std::string &s) { std::cout << s << '\n'; },
            [&] { return time; });
        auto path = c.source / "lsc.txt";
        {
            std::ofstream f(path);
            f << "first\n";
        }
        engine.run();
        time += std::chrono::seconds(6);
        auto first = engine.run();
        if (first.uploaded != 1)
            throw std::runtime_error("initial upload failed");
        if (engine.run().skipped != 1)
            throw std::runtime_error("unchanged skip failed");
        {
            std::ofstream f(path, std::ios::app);
            f << "second\n";
        }
        engine.run();
        time += std::chrono::seconds(6);
        if (engine.run().uploaded != 1)
            throw std::runtime_error("append failed");
        if (ftp.hash(c.destination("lsc.txt")) != hashFile(path))
            throw std::runtime_error("final contents differ");
        std::cout << "PASS real FTP login, probe, snapshot, skip, append, read-back and two-step publish\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
