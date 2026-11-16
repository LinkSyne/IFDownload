#include "console.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <string>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>

enum : WORD {
    aRed    = FOREGROUND_RED | FOREGROUND_INTENSITY,
    aDkRed  = FOREGROUND_RED,
    aGreen  = FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    aYellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    aWhite  = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    aGray   = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
    aCyan   = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    aMag    = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    aDim    = FOREGROUND_INTENSITY
};

static HANDLE hCon = nullptr;

static std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

namespace console {

void init() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    hCon = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hCon && hCon != INVALID_HANDLE_VALUE && GetConsoleMode(hCon, &mode)) {
        SetConsoleMode(hCon, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
}

static WORD attrOf(ConsoleColor c) {
    switch (c) {
        case cRed:    return aRed;
        case cDkRed:  return aDkRed;
        case cGreen:  return aGreen;
        case cYellow: return aYellow;
        case cWhite:  return aWhite;
        case cGray:   return aGray;
        case cCyan:   return aCyan;
        case cMag:    return aMag;
        case cDim:    return aDim;
    }
    return aGray;
}

void setColor(ConsoleColor c) {
    if (hCon && hCon != INVALID_HANDLE_VALUE) {
        SetConsoleTextAttribute(hCon, attrOf(c));
    }
}

void write(const std::string& utf8Text) {
    if (!hCon || hCon == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    std::wstring w = widen(utf8Text);
    if (!w.empty() && WriteConsoleW(hCon, w.c_str(), (DWORD)w.size(), &written, nullptr)) {
        return;
    }
    WriteFile(hCon, utf8Text.data(), (DWORD)utf8Text.size(), &written, nullptr);
}

void cprint(ConsoleColor c, const char* fmt, ...) {
    char buf[4096];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    setColor(c);
    write(buf);
}

int width() {
    if (hCon && hCon != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO ci;
        if (GetConsoleScreenBufferInfo(hCon, &ci)) {
            int w = ci.srWindow.Right - ci.srWindow.Left + 1;
            if (w > 0) return w;
        }
    }
    return 80;
}

void clear() {
    if (!hCon || hCon == INVALID_HANDLE_VALUE) return;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (!GetConsoleScreenBufferInfo(hCon, &csbi)) return;
    DWORD count = (DWORD)csbi.dwSize.X * csbi.dwSize.Y;
    DWORD written = 0;
    COORD home = {0, 0};
    FillConsoleOutputCharacterW(hCon, L' ', count, home, &written);
    FillConsoleOutputAttribute(hCon, csbi.wAttributes, count, home, &written);
    SetConsoleCursorPosition(hCon, home);
}

void pause() {
    cprint(cDim, "\n  Press any key to continue...");
    if (hCon && hCon != INVALID_HANDLE_VALUE) {
        HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(hIn, &mode)) {
            SetConsoleMode(hIn, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));
            DWORD rd = 0;
            INPUT_RECORD rec = {};
            ReadConsoleInputW(hIn, &rec, 1, &rd);
            SetConsoleMode(hIn, mode);
        }
    }
    write("\n");
}

std::string readLine() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    wchar_t wbuf[512] = {};
    DWORD rd = 0;
    if (hCon && hCon != INVALID_HANDLE_VALUE && hIn && hIn != INVALID_HANDLE_VALUE &&
        ReadConsoleW(hIn, wbuf, 511, &rd, nullptr) && rd > 0) {
        std::string s = narrow(std::wstring(wbuf, rd));
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        return s;
    }
    char buf[512] = {};
    if (!fgets(buf, sizeof(buf), stdin)) return {};
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}

#else

#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

namespace console {

void init() {
}

void setColor(ConsoleColor c) {
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

void write(const std::string& utf8Text) {
    fwrite(utf8Text.data(), 1, utf8Text.size(), stdout);
    fflush(stdout);
}

void cprint(ConsoleColor c, const char* fmt, ...) {
    char buf[4096];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt, a);
    va_end(a);
    setColor(c);
    write(buf);
}

int width() {
    winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

void clear() {
    write("\033[2J\033[H");
}

void pause() {
    cprint(cDim, "\n  Press any key to continue...");
    fflush(stdout);
    termios oldt, newt;
    if (tcgetattr(STDIN_FILENO, &oldt) == 0) {
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        getchar();
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    } else {
        getchar();
    }
    write("\n");
}

std::string readLine() {
    char buf[512] = {};
    if (!fgets(buf, sizeof(buf), stdin)) return {};
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

}

#endif

namespace console {

static double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void Progress::start(int barWidth) {
    barWidth = barWidth < 10 ? 10 : (barWidth > 50 ? 50 : barWidth);
    startSec = steadySeconds();
    lastDraw = startSec;
    lastPct = 0;
    drawn = false;
}

void Progress::update(unsigned long long got, unsigned long long total) {
    double now = steadySeconds();
    unsigned pct = total > 0 ? (unsigned)(got * 100 / total) : 0;
    if (drawn && pct == lastPct && total > 0 && now - lastDraw < 0.2) return;
    lastPct = pct;
    lastDraw = now;
    drawn = true;

    double sec = now - startSec;
    double speed = sec > 0.2 ? got / sec / 1048576.0 : 0.0;
    int filled = total > 0 ? (int)(pct * barWidth / 100) : 0;

    char stat[160];
    setColor(cDim);
    write("\r  ");
    setColor(cYellow);
    snprintf(stat, sizeof(stat), "%3u%%", pct);
    write(stat);
    setColor(cWhite);
    write(" [");
    setColor(cRed);
    write(std::string((size_t)filled, '>'));
    setColor(cDim);
    std::string dots;
    for (int i = 0; i < barWidth - filled; i++) dots += "\xc2\xb7";
    write(dots);
    setColor(cWhite);
    write("] ");
    setColor(cGray);
    if (total > 0) {
        snprintf(stat, sizeof(stat), "%.1f/%.1f MB @ %.1f MB/s",
            got / 1048576.0, total / 1048576.0, speed);
    } else {
        snprintf(stat, sizeof(stat), "%.1f MB @ %.1f MB/s", got / 1048576.0, speed);
    }
    write(stat);
    write("   ");
}

void Progress::finish() {
    if (!drawn) return;
    write("\n");
    drawn = false;
}

}
