#include "console.h"
#include "cache.h"
#include "catalog.h"
#include "fs.h"
#include "hash.h"
#include "http.h"
#include "manifest.h"
#include "tunnel.h"

#ifdef _WIN32
#include "signature.h"
#endif

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

static const char* kBanner = R"(88888888888888888  888                     888
  888  888         888                             888                     888
  888  888         888                             888                     888
  888  8888888 .d88888 .d88b. 888  888  88888888b. 888 .d88b.  8888b.  .d88888
  888  888    d88" 888d88""88b888  888  888888 "88b888d88""88b    "88bd88" 888
  888  888    888  888888  888888  888  888888  888888888  888.d888888888  888
  888  888    Y88b 888Y88..88PY88b 888 d88P888  888888Y88..88P888  888Y88b 888
8888888888     "Y88888 "Y88P"  "Y8888888P" 888  888888 "Y88P" "Y888888 "Y88888
)";

static const char* kAppVersion = "1.2";
static const char* kItemId = "vmwareworkstationarchive";

static std::string findManifest() {
#ifdef _WIN32
    const char* sep = "\\";
#else
    const char* sep = "/";
#endif
    std::vector<std::string> paths;
    paths.push_back(fs::joinPath(fs::exeDir(), std::string("manifest") + sep + "vmware.json"));
    paths.push_back(std::string("manifest") + sep + "vmware.json");
    paths.push_back(std::string("..") + sep + "manifest" + sep + "vmware.json");
    for (const std::string& p : paths) {
        if (fs::fileExists(p)) return p;
    }
    return paths[0];
}

static void printSeparator() {
    int w = console::width() - 4;
    if (w < 20) w = 20;
    console::setColor(cDim);
    console::write("  " + std::string((size_t)w, '-') + "\n");
}

static void printEntryLine(size_t idx, const Entry& e) {
    console::cprint(cCyan, "%3lu", (unsigned long)(idx + 1));
    console::cprint(cWhite, ". ");
    console::cprint(cYellow, "VMware %s", e.tag.c_str());
    console::cprint(cDim, " Build ");
    console::cprint(cGray, "%s", e.build.c_str());
    console::cprint(cDim, " - ");
    if (e.sizeMb > 0) {
        console::cprint(cMag, "~%u MB", e.sizeMb);
    } else {
        console::cprint(cMag, "size unknown");
    }
    if (!e.sha256.empty()) {
        console::cprint(cGreen, " [SHA256]");
    } else if (!e.sha1.empty()) {
        console::cprint(cCyan, " [SHA1]");
    }
    console::write("\n");
}

int main() {
    console::init();
    console::clear();
    console::setColor(cRed);
    console::write(kBanner);
    console::write("\n");
    console::cprint(cRed, "  IFDownload v%s\n\n", kAppVersion);
    console::setColor(cGray);

    int tunnelPort = 0;
    if (startTunnel(tunnelPort)) {
        setGlobalProxy("127.0.0.1:" + std::to_string(tunnelPort));
    }

    cache::init();

    std::vector<Entry> manifestEntries = loadManifest(findManifest());
    std::vector<Entry> entries;
    std::string catalogNote;
    console::cprint(cGreen, "  [+] Loading VMWare Version...\n");
    if (!fetchCatalog(manifestEntries, entries, catalogNote)) {
        console::cprint(cRed, "  [!] No versions available.\n");
        console::setColor(cGray);
        stopTunnel();
        console::pause();
        return 1;
    }
    console::cprint(cGreen, "  [+] %lu versions\n\n", (unsigned long)entries.size());

    for (size_t i = 0; i < entries.size(); i++) {
        printEntryLine(i, entries[i]);
    }

    console::write("\n  ");
    printSeparator();
    console::write("\n");
    console::cprint(cWhite, "  Select [1-%lu]: ", (unsigned long)entries.size());
    console::setColor(cGreen);

    std::string input = console::readLine();
    int pick = atoi(input.c_str());
    if (pick < 1 || pick > (int)entries.size()) {
        console::cprint(cRed, "\n  Invalid selection.\n");
        console::setColor(cGray);
        stopTunnel();
        console::pause();
        return 1;
    }

    Entry& sel = entries[(size_t)(pick - 1)];
    console::write("\n  ");
    printSeparator();
    console::cprint(cWhite, "\n  Selected: ");
    console::cprint(cYellow, "VMware Workstation %s Build %s\n", sel.tag.c_str(), sel.build.c_str());
    console::cprint(cGray, "  File:     %s\n", sel.file.c_str());
    if (sel.sizeMb > 0) {
        console::cprint(cGray, "  Size:     ~%u MB\n", sel.sizeMb);
    }
    console::cprint(cGray, "  Hash:     %s\n",
        !sel.sha256.empty() ? "SHA-256 known" : (!sel.sha1.empty() ? "SHA-1 known" : "not published"));

    std::string targetPath = fs::joinPath(fs::currentDir(), sel.file);
    console::cprint(cWhite, "  Output:   ");
    console::cprint(cCyan, "%s\n\n", targetPath.c_str());

    if (cache::lookup(sel)) {
        console::cprint(cGreen, "  [+] sec...\n");
        fs::copyFile(cache::path(sel.file), targetPath);
        console::cprint(cWhite, "  Saved to: ");
        console::cprint(cYellow, "%s\n\n", targetPath.c_str());
        console::setColor(cGray);
        stopTunnel();
        console::pause();
        return 0;
    }

    std::vector<std::string> mirrors;
    mirrors.push_back(std::string("https://archive.org/download/") + kItemId + "/" + sel.path);
    std::string nodeUrl = catalogNodeUrl(sel.path);
    if (!nodeUrl.empty() && nodeUrl != mirrors[0]) {
        mirrors.push_back(nodeUrl);
    }

    int barW = console::width() - 42;
    if (barW < 10) barW = 10;
    if (barW > 50) barW = 50;

    std::string partialPath = cache::partialPath(sel.file);

    for (size_t m = 0; m < mirrors.size(); m++) {
        console::cprint(cGray, "  Source %lu/%lu: ", (unsigned long)(m + 1), (unsigned long)mirrors.size());
        console::cprint(cCyan, "%s\n", mirrors[m].c_str());

        unsigned long long resumeSize = fs::fileSize(partialPath);
        if (resumeSize > 0) {
            console::cprint(cDim, "  Resuming from %.1f MB partial data\n", resumeSize / 1048576.0);
        }

        console::Progress prog;
        prog.start(barW);

        std::unique_ptr<Hasher> h256;
        std::unique_ptr<Hasher> h1;

        auto onChunk = [&](const void* data, size_t len, unsigned long long got, unsigned long long total) {
            if (!data) {
                h256 = sel.sha256.empty() ? nullptr : std::make_unique<Hasher>(HashAlgo::Sha256);
                h1 = sel.sha1.empty() ? nullptr : std::make_unique<Hasher>(HashAlgo::Sha1);
                if (got > 0) {
                    feedFile(partialPath, h256.get(), h1.get(), got);
                }
            } else {
                if (h256) h256->feed(data, len);
                if (h1) h1->feed(data, len);
            }
            prog.update(got, total);
        };

        DownloadResult res = httpDownload(mirrors[m], partialPath, sel.sizeBytes, onChunk);
        prog.finish();

        if (!res.ok) {
            if (res.status > 0) {
                console::cprint(cRed, "  [!] Transfer failed (HTTP %ld)\n\n", res.status);
            } else {
                console::cprint(cRed, "  [!] Connection failed\n\n");
            }
            continue;
        }

        std::string expected;
        std::string actual;
        const char* algoName = "";
        if (!sel.sha256.empty()) {
            algoName = "SHA-256";
            expected = sel.sha256;
            actual = h256 ? h256->finishHex() : "";
        } else if (!sel.sha1.empty()) {
            algoName = "SHA-1";
            expected = sel.sha1;
            actual = h1 ? h1->finishHex() : "";
        }
        h256.reset();
        h1.reset();

        bool integrityOk = true;
        if (!expected.empty()) {
            integrityOk = hashEqual(expected, actual);
        } else if (sel.sizeBytes > 0) {
            integrityOk = (res.resumedBytes + res.received == sel.sizeBytes);
        }

        if (!integrityOk) {
            console::cprint(cRed, "  [!] Integrity check failed, discarding partial data\n\n");
            fs::removeFile(partialPath);
            continue;
        }
        if (!expected.empty()) {
            console::cprint(cGreen, "  [+] %s verified: %s\n", algoName, actual.c_str());
            cache::storeHash(sel.file, sel.sha256.empty() ? "sha1" : "sha256", actual);
        }

        fs::renameFile(partialPath, cache::path(sel.file));
        fs::copyFile(cache::path(sel.file), targetPath);

        console::write("  ");
        printSeparator();
        console::cprint(cGreen, "\n  [+] Download completed\n");
        console::cprint(cWhite, "  Saved to: ");
        console::cprint(cYellow, "%s\n", targetPath.c_str());

#ifdef _WIN32
        bool signedOk = verifyAuthenticode(targetPath);
        console::cprint(cWhite, "  Authenticode: ");
        console::cprint(signedOk ? cGreen : cYellow, "%s\n", signedOk ? "valid signature" : "not signed");
#endif

        console::setColor(cGray);
        stopTunnel();
        console::pause();
        return 0;
    }

    console::cprint(cRed, "  [!] sources failed\n");
    console::setColor(cGray);
    stopTunnel();
    console::pause();
    return 1;
}
