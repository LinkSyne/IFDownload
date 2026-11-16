#pragma once
#include <string>

enum ConsoleColor {
    cRed,
    cDkRed,
    cGreen,
    cYellow,
    cWhite,
    cGray,
    cCyan,
    cMag,
    cDim
};

namespace console {

void init();
void setColor(ConsoleColor c);
void write(const std::string& utf8Text);
void cprint(ConsoleColor c, const char* fmt, ...);
int width();
void clear();
void pause();
std::string readLine();

class Progress {
public:
    void start(int barWidth);
    void update(unsigned long long got, unsigned long long total);
    void finish();

private:
    int barWidth = 10;
    double startSec = 0;
    double lastDraw = 0;
    unsigned lastPct = 0;
    bool drawn = false;
};

}
