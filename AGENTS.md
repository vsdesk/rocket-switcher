# Сборка для агента

Этот файл в репозитории. Каталог `.cursor/` в git не входит и на другую машину не приезжает.

Агент сам собирает и гоняет тесты. Пользователю отдаёт одну команду или путь, без чеклиста «сделай сам». Номер пакета не выдумывать: его читает скрипт из первой строки `packaging/deb/changelog`.

## Linux

`cmake`, сборка, `ctest`. После правок C++, CMake, `packaging/deb/` или демона/GUI — `./scripts/build-deb.sh` из корня репозитория.

Пользователю: `sudo dpkg -i <пакет>_latest.deb && sudo apt-get -f install -y`, при необходимости перезапуск `rocket-switcher-daemon` и `sg input`.

## macOS и Windows

Не запускать `scripts/build-deb.sh`, `dpkg`, `rpmbuild` и Docker-сборку `.rpm`. Демон evdev не собирать: `-DBUILD_DAEMON=OFF -DBUILD_FCITX=OFF`.

Qt 6.8, архивы `qtbase` и `qttools` (`macdeployqt` / `windeployqt`). Корень Qt передать в `CMAKE_PREFIX_PATH`. Если Qt нет и сеть есть, поставить его самому, не списком шагов для пользователя.

Тесты ядра на macOS:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DBUILD_GUI=OFF -DBUILD_DAEMON=OFF -DBUILD_FCITX=OFF
cmake --build build
ctest --test-dir build --output-on-failure
```

Тесты ядра в PowerShell:

```powershell
cmake -B build -DBUILD_TESTING=ON -DBUILD_GUI=OFF -DBUILD_DAEMON=OFF -DBUILD_FCITX=OFF
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Пакет на macOS после правок C++/CMake/GUI (`$QT_ROOT_DIR` — корень Qt):

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR" -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
  -DBUILD_TESTING=OFF -DBUILD_FCITX=OFF -DBUILD_DAEMON=OFF
cmake --build build
./scripts/package-macos.sh
```

`scripts/macos-sign.sh` не запускать, пока пользователь сам не дал сертификат и ключ нотаризации. Отдать путь `dist/rocket-switcher_*_macos.dmg` (рядом лежит такой же zip). В релиз GitHub по тегу `v*` попадают оба файла, даже без подписи.

Пакет в PowerShell (Inno Setup 6 установлен):

```powershell
cmake -B build -DCMAKE_PREFIX_PATH="$env:QT_ROOT_DIR" -DBUILD_TESTING=OFF -DBUILD_FCITX=OFF -DBUILD_DAEMON=OFF
cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File scripts/package-windows.ps1
```

Без Inno Setup всё равно собрать Release и zip, если есть `windeployqt`. Без Qt собрать хотя бы тесты ядра и сказать, что окна настроек нет. Отдать путь `dist/rocket-switcher_*_windows-x64.exe` либо zip, если установщика нет. `sudo dpkg` и перезапуск демона не предлагать: демона в этих сборках нет.
