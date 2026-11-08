#include "http.h"
#include "hash.h"
#include "manifest.h"
#include "signature.h"
#include "cache.h"
#include "tunnel.h"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>
#include <conio.h>

enum : WORD {
    cRed    = FOREGROUND_RED | FOREGROUND_INTENSITY,
    cDkRed  = FOREGROUND_RED,
    cGreen  = FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    cYellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    cWhite  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    cGray   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    cCyan   = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    cMag    = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    cDim    = FOREGROUND_INTENSITY
};

static HANDLE hCon;

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static void col(WORD c) {
    SetConsoleTextAttribute(hCon, c);
}

static void writeOutput(const std::wstring& text) {
    DWORD written = 0;
    if (!WriteConsoleW(hCon, text.c_str(), (DWORD)text.size(), &written, nullptr)) {
        std::string u8 = narrow(text);
        WriteFile(hCon, u8.c_str(), (DWORD)u8.size(), &written, nullptr);
    }
}

static void cprint(WORD c, const wchar_t* fmt, ...) {
    col(c);
    wchar_t buf[2048];
    va_list a;
    va_start(a, fmt);
    vswprintf(buf, sizeof(buf) / sizeof(wchar_t), fmt, a);
    va_end(a);
    writeOutput(buf);
}

static int conWidth() {
    CONSOLE_SCREEN_BUFFER_INFO ci;
    if (GetConsoleScreenBufferInfo(hCon, &ci)) {
        return ci.srWindow.Right - ci.srWindow.Left + 1;
    }
    return 80;
}

static void clearScreen() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hCon, &csbi)) {
        writeOutput(L"\x1b[2J\x1b[H");
        return;
    }
    DWORD count = csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written = 0;
    COORD homeCoords = {0, 0};
    FillConsoleOutputCharacterW(hCon, L' ', count, homeCoords, &written);
    FillConsoleOutputAttribute(hCon, csbi.wAttributes, count, homeCoords, &written);
    SetConsoleCursorPosition(hCon, homeCoords);
}

static void pausePrompt() {
    cprint(cDim, L"\n  Press any key to continue...");
    _getch();
    writeOutput(L"\n");
}

static std::wstring currentWorkingDirW() {
    wchar_t buf[MAX_PATH];
    if (GetCurrentDirectoryW(MAX_PATH, buf) > 0) return buf;
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    wchar_t* last = wcsrchr(buf, L'\\');
    if (last) *last = L'\0';
    return buf;
}

static std::wstring findManifestW() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    wchar_t* last = wcsrchr(buf, L'\\');
    if (last) *last = L'\0';
    std::wstring exeDir = buf;

    std::wstring paths[] = {
        exeDir + L"\\manifest\\vmware.json",
        L"manifest\\vmware.json",
        L"..\\manifest\\vmware.json"
    };
    for (auto& p : paths) {
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES) return p;
    }
    return paths[0];
}

#else
#include <unistd.h>
#include <termios.h>

enum ColorCode {
    cRed = 1, cDkRed, cGreen, cYellow, cWhite, cGray, cCyan, cMag, cDim
};

static void col(ColorCode c) {
    switch (c) {
        case cRed:    fputs("\033[1;31m", stdout); break;
        case cDkRed:  fputs("\033[0;31m", stdout); break;
        case cGreen:  fputs("\033[1;32m", stdout); break;
        case cYellow: fputs("\033[1;33m", stdout); break;
        case cWhite:  fputs("\033[1;37m", stdout); break;
        case cGray:   fputs("\033[0;37m", stdout); break;
        case cCyan:   fputs("\033[1;36m", stdout); break;
        case cMag:    fputs("\033[1;35m", stdout); break;
        case cDim:    fputs("\033[0;90m", stdout); break;
    }
}

static void writeOutputA(const std::string& text) {
    fwrite(text.data(), 1, text.size(), stdout);
    fflush(stdout);
}

static void cprintA(ColorCode c, const char* fmt, ...) {
    col(c);
    char buf[2048];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    writeOutputA(buf);
}

static int conWidth() {
    return 80;
}

static void clearScreen() {
    writeOutputA("\x1b[2J\x1b[H");
}

static void pausePrompt() {
    cprintA(cDim, "\n  Press any key to continue...");
    fflush(stdout);
    termios oldt, newt;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    writeOutputA("\n");
}

static std::string currentWorkingDirA() {
    char buf[1024];
    if (getcwd(buf, sizeof(buf))) return buf;
    return ".";
}

static std::string findManifestA() {
    std::string paths[] = {
        "manifest/vmware.json",
        "../manifest/vmware.json",
        "/usr/share/IFDownload/manifest/vmware.json"
    };
    for (auto& p : paths) {
        FILE* fp = fopen(p.c_str(), "rb");
        if (fp) { fclose(fp); return p; }
    }
    return paths[0];
}

#endif

static std::string urlEncode(const std::string& str) {
    std::string encoded;
    char hex[4];
    for (unsigned char c : str) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += c;
        } else {
            snprintf(hex, sizeof(hex), "%%%02X", c);
            encoded += hex;
        }
    }
    return encoded;
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    hCon = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode = 0;
    if (GetConsoleMode(hCon, &mode)) {
        SetConsoleMode(hCon, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }

    clearScreen();
    col(cDkRed);
    writeOutput(LR"(
  ██▓ ███▄    █   █████▒▄▄▄       ███▄    █  ▄████▄  ▄▄▄█████▓ ██▓ ███▄    █   ▄████ 
 ▓██▒ ██ ▀█   █ ▓██   ▒▒████▄     ██ ▀█   █ ▒██▀ ▀█  ▓  ██▒ ▓▒▓██▒ ██ ▀█   █  ██▒ ▀█▒
 ▒██▒▓██  ▀█ ██▒▒████ ░▒██  ▀█▄  ▓██  ▀█ ██▒▒▓█    ▄ ▒ ▓██░ ▒░▒██▒▓██  ▀█ ██▒▒██░▄▄▄░
 ░██░▓██▒  ▐▌██▒░▓█▒  ░░██▄▄▄▄██ ▓██▒  ▐▌██▒▒▓▓▄ ▄██▒░ ▓██▓ ░ ░██░▓██▒  ▐▌██▒░▓█  ██▓
 ░██░▒██░   ▓██░░▒█░    ▓█   ▓██▒▒██░   ▓██░▒ ▓███▀ ░  ▒██▒ ░ ░██░▒██░   ▓██░░▒▓███▀▒
 ░▓  ░ ▒░   ▒ ▒  ▒ ░    ▒▒   ▓▒█░░ ▒░   ▒ ▒ ░ ░▒ ▒  ░  ▒ ░░   ░▓  ░ ▒░   ▒ ▒  ░▒   ▒ 
  ▒ ░░ ░░   ░ ▒░ ░       ▒   ▒▒ ░░ ░░   ░ ▒░  ░  ▒       ░     ▒ ░░ ░░   ░ ▒░  ░   ░ 
  ▒ ░   ░   ░ ░  ░ ░     ░   ▒      ░   ░ ░ ░          ░       ▒ ░   ░   ░ ░ ░ ░   ░ 
  ░           ░            ░  ░         ░ ░ ░                    ░           ░       ░ 
                                          ░                                           
)");
    writeOutput(L"\n");
    col(cGray);

    cprint(cRed, L"  IFDownload v1.0\n\n");

    int tunnelPort = 0;
    if (startTunnel(tunnelPort)) {
        wchar_t pBuf[32];
        swprintf(pBuf, sizeof(pBuf) / sizeof(wchar_t), L"127.0.0.1:%d", tunnelPort);
        setGlobalProxyW(pBuf);
    } else {
        std::wstring localProxyW = scanLocalProxyW();
        if (!localProxyW.empty()) {
            setGlobalProxyW(localProxyW);
        }
    }

    int w = conWidth() - 4;
    writeOutput(L"  ");
    col(cDim);
    std::wstring line(w, L'─');
    writeOutput(line);
    writeOutput(L"\n\n");
    col(cGray);

    std::wstring manifestPathW = findManifestW();
    auto entries = loadManifest(manifestPathW);
    if (entries.empty()) {
        cprint(cRed, L"  Manifest file not found or corrupted: %ls\n", manifestPathW.c_str());
        pausePrompt();
        return 1;
    }

    int count = (int)entries.size();
    for (int i = 0; i < count; i++) {
        auto& e = entries[i];
        writeOutput(L"    ");
        cprint(cCyan, L"%2d", i + 1);
        cprint(cWhite, L". ");
        cprint(cYellow, L"VMware %ls", widen(e.tag).c_str());
        cprint(cDim, L" Build ");
        cprint(cGray, L"%ls", widen(e.build).c_str());
        cprint(cDim, L" - ");
        cprint(cMag, L"~%u MB", e.sizeMb);
        if (!e.sha256.empty()) cprint(cGreen, L" [SHA256]");
        writeOutput(L"\n");
    }

    writeOutput(L"\n  ");
    col(cDim);
    writeOutput(line);
    writeOutput(L"\n\n");
    cprint(cWhite, L"  Select [1-%d]: ", count);
    col(cGreen);

    wchar_t inp[64] = {};
    DWORD charsRead = 0;
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    if (!ReadConsoleW(hIn, inp, 63, &charsRead, nullptr) || charsRead == 0) {
        char ansiInp[64] = {};
        if (fgets(ansiInp, sizeof(ansiInp), stdin)) {
            MultiByteToWideChar(CP_UTF8, 0, ansiInp, -1, inp, 64);
        }
    }
    int pick = _wtoi(inp);
    if (pick < 1 || pick > count) {
        cprint(cRed, L"\n  Invalid selection.\n");
        pausePrompt();
        return 1;
    }

    Entry& sel = entries[pick - 1];
    writeOutput(L"\n  ");
    col(cDim);
    writeOutput(line);
    writeOutput(L"\n");
    cprint(cWhite, L"\n  Selected: ");
    cprint(cYellow, L"VMware Workstation %ls Build %ls\n", widen(sel.tag).c_str(), widen(sel.build).c_str());
    cprint(cGray, L"  File:     %ls\n", widen(sel.file).c_str());
    cprint(cGray, L"  Size:     ~%u MB\n\n", sel.sizeMb);

    std::wstring targetPathW = currentWorkingDirW() + L"\\" + widen(sel.file);
    std::wstring cachedPathW = cachePathW(sel.file);

    cprint(cWhite, L"  Output:   ");
    cprint(cCyan, L"%ls\n\n", targetPathW.c_str());

    if (cacheHitW(sel.file, sel.sizeMb)) {
        CopyFileW(cachedPathW.c_str(), targetPathW.c_str(), FALSE);
        writeOutput(L"\n  ");
        col(cDim);
        writeOutput(line);
        writeOutput(L"\n");
        cprint(cGreen, L"\n  [+] Download completed (from cache)\n");
        cprint(cWhite, L"  Saved to: ");
        cprint(cYellow, L"%ls\n\n", targetPathW.c_str());
        col(cGray);
        pausePrompt();
        return 0;
    }

    cacheInit();

    std::string directUrl = archiveUrl(sel);
    std::vector<std::string> mirrors;
    mirrors.push_back(directUrl);
    mirrors.push_back("https://ia800100.us.archive.org/31/items/vmwareworkstationarchive/" + sel.path);
    mirrors.push_back("https://ia600100.us.archive.org/items/vmwareworkstationarchive/" + sel.path);
    mirrors.push_back("https://ia801501.us.archive.org/items/vmwareworkstationarchive/" + sel.path);

    int barW = conWidth() - 42;
    if (barW < 10) barW = 10;
    if (barW > 50) barW = 50;

    int mirrorIdx = 0;
    int mirrorCount = (int)mirrors.size();
    for (auto& mirrorUrl : mirrors) {
        mirrorIdx++;

        Sha256 sha;
        sha.init();
        LARGE_INTEGER freq, tStart, tNow;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&tStart);
        DWORD lastPct = (DWORD)-1;
        LARGE_INTEGER tLastDraw = tStart;

        auto onChunk = [&](const void* data, size_t len, ULONGLONG got, ULONGLONG total) {
            sha.feed(data, len);
            DWORD pct = total > 0 ? (DWORD)(got * 100 / total) : 0;
            QueryPerformanceCounter(&tNow);
            double elapsed = (double)(tNow.QuadPart - tLastDraw.QuadPart) / freq.QuadPart;
            if (pct == lastPct && elapsed < 0.5 && total > 0) return;
            lastPct = pct;
            tLastDraw = tNow;
            double sec = (double)(tNow.QuadPart - tStart.QuadPart) / freq.QuadPart;
            double speed = sec > 0.1 ? got / sec / 1048576.0 : 0.0;

            int filled = total > 0 ? (int)(pct * barW / 100) : 0;
            writeOutput(L"\r  ");
            col(cYellow);
            wchar_t numBuf[32];
            swprintf(numBuf, sizeof(numBuf) / sizeof(wchar_t), L"%3lu%%", pct);
            writeOutput(numBuf);
            col(cWhite);
            writeOutput(L" [");
            col(cRed);
            std::wstring arrows(filled, L'>');
            writeOutput(arrows);
            col(cDim);
            std::wstring dots(barW - filled, L'·');
            writeOutput(dots);
            col(cWhite);
            writeOutput(L"] ");
            col(cGray);
            wchar_t statBuf[128];
            if (total > 0) swprintf(statBuf, sizeof(statBuf) / sizeof(wchar_t), L"%.1f/%.1f MB", got / 1048576.0, total / 1048576.0);
            else swprintf(statBuf, sizeof(statBuf) / sizeof(wchar_t), L"%.1f MB", got / 1048576.0);
            writeOutput(statBuf);
            writeOutput(L" @ ");
            col(cCyan);
            swprintf(statBuf, sizeof(statBuf) / sizeof(wchar_t), L"%.1f MB/s", speed);
            writeOutput(statBuf);
            col(cGray);
            writeOutput(L"   ");
        };

        bool ok = httpDownloadW(mirrorUrl, cachedPathW, onChunk);

        if (ok) {
            writeOutput(L"\n\n");
            std::string fileHash = sha.finish();
            sha.close();
            CopyFileW(cachedPathW.c_str(), targetPathW.c_str(), FALSE);

            writeOutput(L"  ");
            col(cDim);
            writeOutput(line);
            writeOutput(L"\n");
            cprint(cGreen, L"\n  [+] Download completed\n");
            cprint(cWhite, L"  Saved to: ");
            cprint(cYellow, L"%ls\n\n", targetPathW.c_str());
            col(cGray);
            stopTunnel();
            pausePrompt();
            return 0;
        }
        sha.close();
        _wremove(cachedPathW.c_str());
    }

    cprint(cRed, L"\n  [!] Download failed. All mirrors are unreachable.\n");
    col(cGray);
    stopTunnel();
    pausePrompt();

#else
    clearScreen();
    col(cDkRed);
    writeOutputA(R"(
  ██▓ ███▄    █   █████▒▄▄▄       ███▄    █  ▄████▄  ▄▄▄█████▓ ██▓ ███▄    █   ▄████ 
 ▓██▒ ██ ▀█   █ ▓██   ▒▒████▄     ██ ▀█   █ ▒██▀ ▀█  ▓  ██▒ ▓▒▓██▒ ██ ▀█   █  ██▒ ▀█▒
 ▒██▒▓██  ▀█ ██▒▒████ ░▒██  ▀█▄  ▓██  ▀█ ██▒▒▓█    ▄ ▒ ▓██░ ▒░▒██▒▓██  ▀█ ██▒▒██░▄▄▄░
 ░██░▓██▒  ▐▌██▒░▓█▒  ░░██▄▄▄▄██ ▓██▒  ▐▌██▒▒▓▓▄ ▄██▒░ ▓██▓ ░ ░██░▓██▒  ▐▌██▒░▓█  ██▓
 ░██░▒██░   ▓██░░▒█░    ▓█   ▓██▒▒██░   ▓██░▒ ▓███▀ ░  ▒██▒ ░ ░██░▒██░   ▓██░░▒▓███▀▒
 ░▓  ░ ▒░   ▒ ▒  ▒ ░    ▒▒   ▓▒█░░ ▒░   ▒ ▒ ░ ░▒ ▒  ░  ▒ ░░   ░▓  ░ ▒░   ▒ ▒  ░▒   ▒ 
  ▒ ░░ ░░   ░ ▒░ ░       ▒   ▒▒ ░░ ░░   ░ ▒░  ░  ▒       ░     ▒ ░░ ░░   ░ ▒░  ░   ░ 
  ▒ ░   ░   ░ ░  ░ ░     ░   ▒      ░   ░ ░ ░          ░       ▒ ░   ░   ░ ░ ░ ░   ░ 
  ░           ░            ░  ░         ░ ░ ░                    ░           ░       ░ 
                                          ░                                           
)");
    writeOutputA("\n");
    col(cGray);

    cprintA(cRed, "  IFDownload v1.0\n\n");

    int tunnelPort = 0;
    if (startTunnel(tunnelPort)) {
        setGlobalProxy("127.0.0.1:" + std::to_string(tunnelPort));
    } else {
        std::string localProxy = scanLocalProxy();
        if (!localProxy.empty()) {
            setGlobalProxy(localProxy);
        }
    }

    std::string line(76, '-');
    writeOutputA("  ");
    col(cDim);
    writeOutputA(line + "\n\n");
    col(cGray);

    std::string manifestPath = findManifestA();
    auto entries = loadManifest(manifestPath);
    if (entries.empty()) {
        cprintA(cRed, "  Manifest file not found or corrupted: %s\n", manifestPath.c_str());
        pausePrompt();
        return 1;
    }

    int count = (int)entries.size();
    for (int i = 0; i < count; i++) {
        auto& e = entries[i];
        writeOutputA("    ");
        cprintA(cCyan, "%2d", i + 1);
        cprintA(cWhite, ". ");
        cprintA(cYellow, "VMware %s", e.tag.c_str());
        cprintA(cDim, " Build ");
        cprintA(cGray, "%s", e.build.c_str());
        cprintA(cDim, " - ");
        cprintA(cMag, "~%u MB", e.sizeMb);
        if (!e.sha256.empty()) cprintA(cGreen, " [SHA256]");
        writeOutputA("\n");
    }

    writeOutputA("\n  ");
    col(cDim);
    writeOutputA(line + "\n\n");
    cprintA(cWhite, "  Select [1-%d]: ", count);
    col(cGreen);

    char inp[64] = {};
    if (!fgets(inp, sizeof(inp), stdin)) return 1;
    int pick = atoi(inp);
    if (pick < 1 || pick > count) {
        cprintA(cRed, "\n  Invalid selection.\n");
        pausePrompt();
        return 1;
    }

    Entry& sel = entries[pick - 1];
    writeOutputA("\n  ");
    col(cDim);
    writeOutputA(line + "\n");
    cprintA(cWhite, "\n  Selected: ");
    cprintA(cYellow, "VMware Workstation %s Build %s\n", sel.tag.c_str(), sel.build.c_str());
    cprintA(cGray, "  File:     %s\n", sel.file.c_str());
    cprintA(cGray, "  Size:     ~%u MB\n\n", sel.sizeMb);

    std::string targetPath = currentWorkingDirA() + "/" + sel.file;
    std::string cachedPath = cachePath(sel.file);

    cprintA(cWhite, "  Output:   ");
    cprintA(cCyan, "%s\n\n", targetPath.c_str());

    if (cacheHit(sel.file, sel.sizeMb)) {
        FILE* src = fopen(cachedPath.c_str(), "rb");
        FILE* dst = fopen(targetPath.c_str(), "wb");
        if (src && dst) {
            char buf[65536];
            size_t n = 0;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
            fclose(src); fclose(dst);
        }
        writeOutputA("\n  ");
        col(cDim);
        writeOutputA(line + "\n");
        cprintA(cGreen, "\n  [+] Download completed (from cache)\n");
        cprintA(cWhite, "  Saved to: ");
        cprintA(cYellow, "%s\n\n", targetPath.c_str());
        col(cGray);
        pausePrompt();
        return 0;
    }

    cacheInit();

    std::string directUrl = archiveUrl(sel);
    std::vector<std::string> mirrors;
    mirrors.push_back(directUrl);
    mirrors.push_back("https://ia800100.us.archive.org/31/items/vmwareworkstationarchive/" + sel.path);
    mirrors.push_back("https://ia600100.us.archive.org/items/vmwareworkstationarchive/" + sel.path);
    mirrors.push_back("https://ia801501.us.archive.org/items/vmwareworkstationarchive/" + sel.path);

    int barW = 38;

    for (auto& mirrorUrl : mirrors) {

        Sha256 sha;
        sha.init();
        DWORD lastPct = (DWORD)-1;

        auto onChunk = [&](const void* data, size_t len, ULONGLONG got, ULONGLONG total) {
            sha.feed(data, len);
            DWORD pct = total > 0 ? (DWORD)(got * 100 / total) : 0;
            if (pct == lastPct && total > 0) return;
            lastPct = pct;

            int filled = total > 0 ? (int)(pct * barW / 100) : 0;
            writeOutputA("\r  ");
            col(cYellow);
            char numBuf[32];
            snprintf(numBuf, sizeof(numBuf), "%3u%%", pct);
            writeOutputA(numBuf);
            col(cWhite);
            writeOutputA(" [");
            col(cRed);
            std::string arrows(filled, '>');
            writeOutputA(arrows);
            col(cDim);
            std::string dots(barW - filled, '.');
            writeOutputA(dots);
            col(cWhite);
            writeOutputA("] ");
            col(cGray);
            char statBuf[128];
            if (total > 0) snprintf(statBuf, sizeof(statBuf), "%.1f/%.1f MB", got / 1048576.0, total / 1048576.0);
            else snprintf(statBuf, sizeof(statBuf), "%.1f MB", got / 1048576.0);
            writeOutputA(statBuf);
            col(cGray);
            writeOutputA("   ");
        };

        bool ok = httpDownload(mirrorUrl, cachedPath, onChunk);

        if (ok) {
            writeOutputA("\n\n");
            std::string fileHash = sha.finish();
            sha.close();

            FILE* src = fopen(cachedPath.c_str(), "rb");
            FILE* dst = fopen(targetPath.c_str(), "wb");
            if (src && dst) {
                char buf[65536];
                size_t n = 0;
                while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
                fclose(src); fclose(dst);
            }

            writeOutputA("  ");
            col(cDim);
            writeOutputA(line + "\n");
            cprintA(cGreen, "\n  [+] Download completed\n");
            cprintA(cWhite, "  Saved to: ");
            cprintA(cYellow, "%s\n\n", targetPath.c_str());
            col(cGray);
            stopTunnel();
            pausePrompt();
            return 0;
        }
        sha.close();
        remove(cachedPath.c_str());
    }

    cprintA(cRed, "\n  [!] Download failed. All mirrors are unreachable.\n");
    col(cGray);
    stopTunnel();
    pausePrompt();
#endif

    stopTunnel();
    return 1;
}
