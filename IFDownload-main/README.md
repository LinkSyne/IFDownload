# IFDownload

<p align="center">
  <b>Консольный загрузчик архивных версий VMware Workstation: живой каталог с archive.org, докачка и проверка целостности.</b>
</p>

```text
88888888888888888  888                             888                     888
  888  888         888                             888                     888
  888  888         888                             888                     888
  888  8888888 .d88888 .d88b. 888  888  88888888b. 888 .d88b.  8888b.  .d88888
  888  888    d88" 888d88""88b888  888  888888 "88b888d88""88b    "88bd88" 888
  888  888    888  888888  888888  888  888888  888888888  888.d888888888  888
  888  888    Y88b 888Y88..88PY88b 888 d88P888  888888Y88..88P888  888Y88b 888
8888888888     "Y88888 "Y88P"  "Y8888888P" 888  888888 "Y88P" "Y888888 "Y88888
```

Баннер рендерится в терминале красным цветом (шрифт Colossal).

---

## О проекте

**IFDownload** — утилита на C++17 для скачивания старых версий VMware Workstation, которые больше не выкладываются на официальном сайте Broadcom/VMware. Единственный источник файлов — [Internet Archive](https://archive.org/details/vmwareworkstationarchive).

### Возможности

- **Живой каталог версий**: список берётся из metadata API archive.org при каждом запуске, поэтому новые файлы в айтеме подхватываются автоматически без обновления сборки. Ответ API кэшируется на 24 часа; при недоступности сети используется устаревший кэш, затем локальный `manifest/vmware.json`.
- **Проверка целостности**: SHA-256 из манифеста или SHA-1 из metadata API archive.org считается прямо во время загрузки и сравнивается с эталоном. Файл с несовпавшим хэшем удаляется, загрузка идёт с другого источника.
- **Докачка**: недокачанные данные сохраняются в кэше и докачиваются через HTTP Range с того же места — включая смену источника посреди загрузки.
- **TLS включён всегда**: сертификаты серверов проверяются (WinHTTP без игнорирования ошибок сертификатов; curl с `VERIFYPEER=1`, `VERIFYHOST=2`).
- **Локальный TLS-туннель**: собственный CONNECT-прокси на `127.0.0.1`, который фрагментирует TLS ClientHello (2 байта + остаток) — помогает на сетях с DPI-фильтрацией. Никаких сторонних серверов, весь трафик идёт напрямую в Internet Archive.
- **Прокси без сюрпризов**: используется только туннель самой программы или переменные окружения (`HTTPS_PROXY`, `HTTP_PROXY`, `ALL_PROXY`). Скрытое сканирование локальных портов отсутствует.
- **Кэш с проверкой**: скачанные файлы лежат в системном кэше, рядом сохраняется эталонный хэш. Повторный запуск не перекачивает файл повторно; кэш без хэша отбрасывается.
- **Authenticode (Windows)**: после загрузки установщик проверяется через WinVerifyTrust, результат выводится в консоль.

---

## Сборка из исходников

### Windows (MSVC или MinGW)

Через CMake:
```cmd
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64
cmake --build . --config Release
```

Через MinGW:
```bash
x86_64-w64-mingw32-g++ -std=c++17 -O2 -Isrc -Ithird src/*.cpp \
  -lwinhttp -lbcrypt -lwintrust -lshell32 -lole32 -luuid -lws2_32 \
  -static -static-libgcc -static-libstdc++ \
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

Или прямая компиляция:
```bash
g++ -std=c++17 -O2 -Isrc -Ithird src/*.cpp -lcurl -lssl -lcrypto -lpthread -o ifd
```

---

## Структура проекта

```text
IFDownload/
├── CMakeLists.txt                  # Кроссплатформенный CMake
├── mingw-toolchain.cmake           # Тулчейн для кросс-сборки Windows
├── src/
│   ├── main.cpp                    # Точка входа, поток загрузки, проверка целостности
│   ├── console.h / console.cpp     # Терминал: цвета, ввод, прогресс-бар (Win/Linux)
│   ├── fs.h / fs.cpp               # Файлы и пути UTF-8 на обеих платформах
│   ├── http.h / http.cpp           # Сеть: WinHTTP / libcurl, TLS, докачка
│   ├── hash.h / hash.cpp           # SHA-256 и SHA-1 (BCrypt / OpenSSL EVP)
│   ├── catalog.h / catalog.cpp     # Живой каталог версий с archive.org
│   ├── manifest.h / manifest.cpp   # Локальный манифест (офлайн-фолбэк)
│   ├── cache.h / cache.cpp         # Кэш с sidecar-файлом хэша
│   ├── signature.h / signature.cpp # Authenticode (WinVerifyTrust, только Windows)
│   └── tunnel.h / tunnel.cpp       # Локальный CONNECT-туннель с фрагментацией ClientHello
├── manifest/
│   └── vmware.json                 # Офлайн-фолбэк каталога
├── third/
│   └── json.hpp                    # Автономный JSON-парсер (minijson)
└── ../.github/workflows/           # CI: сборка и проверка ссылок
```

---

## Лицензия

Распространяется под лицензией [MIT](LICENSE).
