# IFDownload

<p align="center">
  <b>Консольный загрузчик архивных версий VMware Workstation с автоматическим обходом блокировок и проверкой целостности.</b>
</p>

---

## О проекте

**IFDownload** — высокопроизводительная утилита системного уровня на C++17 для скачивания старых и архивных версий VMware Workstation, которые больше недоступны на официальном сайте Broadcom/VMware.

### Ключевые возможности

16 версий в манифесте**: от VMware 11.x до 17.6.4 (с точными номерами билдов и размерами).
Встроенный обход блокировок**: многоканальный роутинг через защищенные CDN-шлюзы и датацентры Internet Archive — качает в РФ без необходимости сторонних VPN.
Автодетект локальных прокси**: автоматически подхватывает запущенные клиенты Clash (`7890`), Xray/v2ray (`10809`), NekoRay (`2080`) или Tor (`9050`).
Кроссплатформенность**: нативная поддержка **Windows** (WinHTTP, BCrypt, WinTrust, UTF-16) и **Linux** (libcurl, OpenSSL, ANSI terminal).
Контроль целостности**: проверка SHA-256 хэша на лету и валидация подписи Authenticode / структуры PE.
Локальный кэш**: сохранение скачанных файлов в системный кэш с автоматической проверкой от повреждений.


```text
  ██▓ ███▄    █   █████▒▄▄▄       ███▄    █  ▄████▄  ▄▄▄█████▓ ██▓ ███▄    █   ▄████ 
 ▓██▒ ██ ▀█   █ ▓██   ▒▒████▄     ██ ▀█   █ ▒██▀ ▀█  ▓  ██▒ ▓▒▓██▒ ██ ▀█   █  ██▒ ▀█▒
 ▒██▒▓██  ▀█ ██▒▒████ ░▒██  ▀█▄  ▓██  ▀█ ██▒▒▓█    ▄ ▒ ▓██░ ▒░▒██▒▓██  ▀█ ██▒▒██░▄▄▄░
 ░██░▓██▒  ▐▌██▒░▓█▒  ░░██▄▄▄▄██ ▓██▒  ▐▌██▒▒▓▓▄ ▄██▒░ ▓██▓ ░ ░██░▓██▒  ▐▌██▒░▓█  ██▓
 ░██░▒██░   ▓██░░▒█░    ▓█   ▓██▒▒██░   ▓██░▒ ▓███▀ ░  ▒██▒ ░ ░██░▒██░   ▓██░░▒▓███▀▒
 ░▓  ░ ▒░   ▒ ▒  ▒ ░    ▒▒   ▓▒█░░ ▒░   ▒ ▒ ░ ░▒ ▒  ░  ▒ ░░   ░▓  ░ ▒░   ▒ ▒  ░▒   ▒ 
  ▒ ░░ ░░   ░ ▒░ ░       ▒   ▒▒ ░░ ░░   ░ ▒░  ░  ▒       ░     ▒ ░░ ░░   ░ ▒░  ░   ░ 
  ▒ ░   ░   ░ ░  ░ ░     ░   ▒      ░   ░ ░ ░          ░       ▒ ░   ░   ░ ░ ░ ░   ░ 
  ░           ░            ░  ░         ░ ░ ░                    ░           ░       ░ 

  Загрузчик VMWare старых версий которые сейчас недоступны все в одном
  IFDownload v1.0

  ────────────────────────────────────────────────────────────────────────────

     1. VMware 17.6.4 Build 24832109 - ~406 MB
     2. VMware 17.6.3 Build 24583834 - ~401 MB
     3. VMware 17.6.2 Build 24409262 - ~448 MB
     4. VMware 17.6.1 Build 24319023 - ~448 MB
     5. VMware 17.6.0 Build 24238078 - ~448 MB [SHA256]
     ...
    16. VMware 11.1.3 Build 3206955 - ~340 MB
```

---

## Скачивание и запуск

Готовые бинарники находятся прямо в корне репозитория:

- **Windows**: `ifd.exe`
- **Linux**: `./ifd`

### Запуск на Linux:
```bash
chmod +x ./ifd
./ifd
```

### Запуск на Windows:
```cmd
ifd.exe
```

---

## Сборка из исходников

### Windows (MSVC или MinGW)

Через CMake:
```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Или через bat-файл:
```cmd
build.bat
```

Через MinGW:
```bash
x86_64-w64-mingw32-g++ -std=c++17 -O2 -static -static-libgcc -static-libstdc++ \
  -Isrc -Ithird src/*.cpp \
  -lwinhttp -lbcrypt -lshell32 -lwintrust -lole32 -luuid -lws2_32 \
  -o ifd.exe
```

### Linux (Ubuntu / Debian / Arch / Fedora)

Зависимости:
```bash
# Ubuntu / Debian
sudo apt-get install g++ cmake libcurl4-openssl-dev libssl-dev

# Arch Linux
sudo pacman -S gcc cmake curl openssl
```

Сборка через CMake:
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Или прямая компиляция через g++:
```bash
g++ -std=c++17 -O2 -Isrc -Ithird src/*.cpp -lcurl -lssl -lcrypto -lpthread -o ifd
```

---

## Структура проекта

```text
IFDownload/
├── ifd                             # Нативный исполняемый файл Linux
├── ifd.exe                         # Исполняемый файл Windows (x64)
├── CMakeLists.txt                  # Кроссплатформенный CMake
├── build.bat                       # Скрипт сборки Windows
├── src/
│   ├── main.cpp                    # Точка входа, рендеринг, прогресс-бар
│   ├── http.h / http.cpp           # Сетевой клиент (WinHTTP / libcurl)
│   ├── hash.h / hash.cpp           # SHA-256 (BCrypt / OpenSSL)
│   ├── manifest.h / manifest.cpp   # Парсинг манифеста версий
│   ├── signature.h / signature.cpp # Проверка подписей PE / Authenticode
│   └── cache.h / cache.cpp         # Локальное кэширование
├── manifest/
│   └── vmware.json                 # Каталог версий, билдов и SHA-256
├── third/
│   └── json.hpp                    # Автономный JSON-парсер
└── docs/
    ├── build.yml                   # GitHub Actions CI для сборки
    └── linkcheck.yml               # Проверка доступности зеркал
```

---

## Лицензия

Распространяется под лицензией [MIT](LICENSE).
