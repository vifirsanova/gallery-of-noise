# Voice Board — Bio Modulation + Noise (Autonomous Embedded C++17 Instrument)

Автономный перформативный биомузыкальный инструмент реального времени на **C++17** для **Raspberry Pi 4 (2 ГБ ОЗУ, Raspberry Pi OS Lite без X11/Wayland и без браузера)** и разработки на **Arch Linux**.

Система работает **полностью автономно** как единый нативный бинарник (`app`), выводя интерфейс напрямую на малый экран инструмента (**3.5" `480×320` SPI TFT** через `/dev/fb0` `RGB565`, **4.3"–5" DSI/HDMI** через `SDL2 KMSDRM` или окно `SDL2` на десктопе) и параллельно на вспомогательный **I2C OLED `128×64` (SSD1306)**.

---

## 1. Почему НЕ Qt, а кастомный Embedded Canvas + `SDL2 (KMSDRM)` + `/dev/fb0`?

При проектировании автономного UI для **Raspberry Pi 4 (2 ГБ ОЗУ)** с малым экраном (`480×320` / `800×480`) и жёстким аудио-буфером **ALSA 128 сэмплов @ 48 кГц (~2.67 мс)** рассматривались три подхода:

| Критерий | Qt 6 (`QtWidgets` / `QtQuick QML`) | Dear ImGui + OpenGL ES | **Выбранное решение: C++17 Embedded `Canvas` + `SDL2` / `/dev/fb0`** |
| :--- | :--- | :--- | :--- |
| **Работа на RPi OS Lite (без X11/Wayland)** | Требует тяжёлой настройки `eglfs` / `linuxfb`, шрифтов `fontconfig`, `udev`/`libinput` | Требует EGL/GLES контекста (не работает напрямую на дешёвых SPI `/dev/fb0` TFT) | **Из коробки**: `SDL2 KMSDRM` для DSI/HDMI **и** прямой `mmap` `/dev/fb0` (`16-bit RGB565` / `32-bit XRGB8888`) для SPI-дисплеев |
| **Потребление ОЗУ и зависимостей** | ~120–200 МБ зависимостей, QML JIT-компилятор в ОЗУ (критично при 2 ГБ RAM + OpenCV в Этапе 2) | ~25 МБ + Mesa/GLES | **< 4 МБ ОЗУ**, **0 внешних шрифтов/ассетов**, `SDL2` опционален (~2 МБ) |
| **Риск аудио-xrun (ALSA 128 frames)** | Шейдерный пайплайн QML и блокировки драйвера V3D могут вызывать джиттер прерываний USB/I2S | Умеренный (зависит от `eglSwapBuffers`) | **Минимальный**: детерминированный софтверный блит буфера `480×320` (600 КБ) за `< 0.4 мс` на 1 ядре Cortex-A72 @ 30 FPS |
| **Эргономика на малом экране 3.5"–5"** | Десктопные виджеты Qt мелкие и нечитаемые на сцене | Отладочный вид (мелкие слайдеры для мыши) | **Аппаратный страничный дизайн (Elektron / Dirtywave M8 / Organelle)**: крупные 270° энкодеры, 2D XY-пэды, контрастный растровый шрифт |
| **Время холодного старта с SD-карты** | 3–6 с (инициализация Qt/QML/шрифтов) | 1–2 с | **< 0.3 с** до появления звука и картинки |
| **Сборка CMake** | Требует `MOC`, `RCC`, `UIC` и десятков `-dev` пакетов | Требует OpenGL/GLFW/SDL | Чистый стандартный **C++17**, компилируется за секунды |

---

## 2. Архитектура малого экрана (`480×320`, 5 аппаратных страниц)

Вместо перегруженного «одноэкранного» дашборда интерфейс спроектирован по канонам сценических синтезаторов (базовое логическое разрешение **`480×320`**, идеально кратное 3.5" SPI TFT `480×320`, чисто масштабируемое на 5" `800×480` и `960×640` на Arch Linux):

- **Верхняя постоянная панель**: крупный индикатор активного пресета (`VOICE`..`DRIFT`), 5 вкладок страниц (`1:PERF`, `2:VOX`, `3:DRN`, `4:SEQ`, `5:BIO`), аппаратные светодиоды трактов (`M` Mic, `D` Drone, `F` Synth Voice, `B` Bypass, `!` Clip) и кнопка справки `?`.
- **Страница `1:PERF` (Сценический перформанс)**: два крупных 2D векторных XY-пэда (`VOICE` и `DRONE`) с одновременным отображением базовой точки и живого био-модулированного курсора, быстрые индикаторы био-сигналов/мастера и 16-шаговая лента шумового секвенсора.
- **Страница `2:VOX` (Цепочка голоса)**: переупорядочиваемая цепочка из 11 эффектов (`PAN`, `HPF`, `COMP`, `FOLD`, `BITCRUSH`, `RINGMOD`, `CHORUS`, `REVERB`, `SHIMMER`, `DELAY`, `LPF`) и 4 крупных 270° энкодера (`Axis X`, `Axis Y`, `Param 1`, `Param 2`) с бейджами био-источников и полосами глубины модуляции.
- **Страница `3:DRN` (Дрон 55 Гц + Мастер и Обратная связь)**: матрица из 7 эффектов дрона, энкодеры `DRN X`/`DRN Y` и 4 мастер-энкодера (`VOICE OUT`, `DRONE VOL`, `FEEDBACK`, `MASTER LPF`).
- **Страница `4:SEQ` (Шумовой синтезатор и 4×16 секвенсор)**: полная полиритмическая сетка **4 дорожки × 16 шагов** с индикацией **Parameter Locks** (янтарные точки на шагах) и 15 параметров синтеза активной дорожки (`White/Pink/Brown`, SVF `Cutoff/Res`, `AD Env`, `LFO 1`, `LFO 2`, `Pan/Gain/BPM`).
- **Страница `5:BIO` (Био-телеметрия и здоровье системы)**: 3 живых осциллографа (`BREATH`, `HEART PULSE + BPM`, `EYE GAZE X`), 2D-радар взгляда `EYE 2D TRACKER` и диагностика реального времени (`ALSA Hz/frames/latency`, счётчик `XRUNS`, активный MIDI-порт, статус `OLED I2C 0x3C`).
- **Нижняя постоянная панель**: индикатор режимов `KNOB:XY/PARAM`, `BIO:BRTH/HART/EYEX/EYEY` и **12-сегментный аппаратный стерео VU-метр**.

Скриншоты всех страниц сохранены в `renders/screens/`:
- `renders/screens/page1_perform.png`
- `renders/screens/page2_voice_fx.png`
- `renders/screens/page3_drone_master.png`
- `renders/screens/page4_noise_seq.png`
- `renders/screens/page5_bio_sys.png`
- `renders/screens/overlay_help.png`

---

## 3. Модульная структура CMake

```text
.
├── CMakeLists.txt
├── include/
│   ├── core/          # Портируемое RT-safe C++17 DSP ядро (0 внешних зависимостей)
│   ├── audio/         # Полнодуплексный ALSA 48 кГц + симулированный RT-бэкенд
│   ├── ui/            # Автономный Embedded UI 480x320 (Canvas, InstrumentUi, SDL2, fbdev)
│   └── app/           # RtMidi контроллер Akai + драйвер 128x64 I2C SSD1306 OLED
├── src/
│   ├── core/          # 12 эффектов, PolyBLEP дрон, 4-голосный шумовой синт, пресеты
│   ├── audio/         # AlsaBackend.cpp, SimulatedBackend.cpp
│   ├── ui/            # Canvas.cpp, InstrumentUi.cpp, Sdl2Backend.cpp, FbdevBackend.cpp
│   └── app/           # main.cpp (автономный инструмент), render_wav.cpp (офлайн WAV CLI)
├── tests/             # Юнит-тесты ядра (test_core.cpp) и интеграционные тесты (test_integration.cpp)
└── renders/           # Стерео 24-бит 48 кГц WAV-рендеры 7 пресетов + PNG-скриншоты экранов
```

Ключевые гарантии реального времени (`core`):
- `FullState` и `Command` — **trivially copyable** структуры фиксированного размера на базе `enum` и `std::array` (**0 `std::map` / `std::string` / `malloc` / `mutex`** в аудио-потоке).
- Обмен между потоками UI / MIDI / OLED и аудио-колбэком идёт исключительно через lock-free очередь **`SpscRingBuffer<Command, 1024>`** и wait-free снапшот **`SeqLockSnapshot<FullState>`**.
- Все модулируемые параметры проходят через однополюсный сглаживатель **`OnePoleSmoother` (~30 мс)** для устранения щелчков при скачках MIDI CC и био-сенсоров.

---

## 4. Сборка и запуск

### 4.1. Arch Linux (рабочая станция разработчика)
```bash
sudo pacman -S --needed base-devel cmake ninja alsa-lib rtmidi sdl2 catch2

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Запуск всех юнит- и интеграционных тестов
ctest --test-dir build --output-on-failure -V

# Запуск автономного инструмента в окне SDL2 (960x640 = 2x от нативных 480x320) с демо-голосом:
./build/app --demo --scale 2

# Запуск с реальной полнодуплексной звуковой картой ALSA (48 кГц, буфер 128 сэмплов):
./build/app --alsa --device hw:0,0 --sr 48000 --period 128 --scale 2
```

### 4.2. Raspberry Pi 4 (Raspberry Pi OS Lite — без X11/Wayland/браузера)
```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build pkg-config \
    libasound2-dev librtmidi-dev libsdl2-dev i2c-tools

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Варианты автономного запуска на Raspberry Pi OS Lite:
1. **SPI TFT экран 3.5" `480×320` (`/dev/fb0` или `/dev/fb1`, `RGB565`, вообще без GPU/SDL)**:
   ```bash
   ./build/app --alsa --device plughw:1,0 --period 128 --fb /dev/fb1
   ```
2. **DSI / HDMI экран 4.3"–5" через аппаратный `SDL2 KMSDRM` прямо из голой консоли**:
   ```bash
   SDL_VIDEODRIVER=kmsdrm ./build/app --alsa --device plughw:1,0 --period 128 --fullscreen
   ```
3. **Ультра-компактный режим только с `128×64` I2C OLED (`0x3C`) и MIDI-контроллером Akai**:
   ```bash
   ./build/app --alsa --device plughw:1,0 --period 128 --headless
   ```

Автозапуск при подаче питания (`/etc/systemd/system/voiceboard.service`):
```ini
[Unit]
Description=Voice Board Autonomous Biomusical Instrument
After=sound.target

[Service]
Type=simple
User=pi
SupplementaryGroups=audio video input i2c render
WorkingDirectory=/home/pi/voiceboard
Environment=SDL_VIDEODRIVER=kmsdrm
ExecStart=/home/pi/voiceboard/build/app --alsa --device plughw:1,0 --period 128 --fullscreen
CPUSchedulingPolicy=fifo
CPUSchedulingPriority=70
LimitRTPRIO=95
LimitMEMLOCK=infinity
Restart=on-failure

[Install]
WantedBy=multi-user.target
```

### 4.3. Офлайн-генерация эталонных WAV (24-bit, 48 kHz, Stereo) и экспорт экранов
```bash
# Сгенерировать 4-секундные стерео WAV для всех 7 пресетов
./build/render_wav --all-presets --out-dir renders --seconds 4.0

# Экспортировать все 5 аппаратных экранов 480x320 в PNG
./build/app --export-screens renders/screens
```

---

## 5. Управление на сцене (Клавиатура, Тачскрин и Akai MIDI)

### Навигация и системные клавиши
- **`1` .. `5`** (или тап по верхним вкладкам): переключение аппаратных экранов (`PERFORM`, `VOICE FX`, `DRONE+MST`, `NOISE SEQ`, `BIO & SYS`).
- **`F1` .. `F7`** (или тап по окну пресета / **Akai Пэды 1–7**): пресеты `VOICE`, `WALL`, `AMBIENT`, `NOISE`, `CLEAN`, `BREATH`, `DRIFT`.
- **`I` / `?`**: открыть/закрыть шпаргалку управления на экране.
- **`Tab`** (или тап по `KNOB:XY` внизу): переключение режима ручек `XY` ↔ `PARAM`.
- **`M` / `N` / `L,P,O` / `B`**: вкл/выкл микрофона, дрона, тестового синтетического голоса и глобального байпаса.

### Цепочки эффектов, Мастер и Био-модуляция
- **Голос (`VOICE`)**: `E` / `D` — выбор эффекта (`Ctrl+E/D` — сдвиг порядка в цепи); `Q/W` и `A/S` — изменение `X`/`Y` или параметров `Param 1`/`Param 2`; `Shift + E/D/Q/A` — привязка био-источника.
- **Дрон (`DRONE`)**: `Y` / `H` — выбор эффекта (`Shift+Y/H` — вкл/выкл, `Ctrl+Y/H` — сдвиг); `Z/X` и `C/V` — оси `X`/`Y` дрона; `Shift + Z/X/C` — привязка био-источника.
- **Мастер (`MASTER`)**: `R` / `T` — выбор параметра (`voiceOut`, `droneVol`, `fb`, `lpf`), малые `f` / `g` — уменьшить/увеличить на 5% диапазона, `Shift+F` — привязка био-модуляции.
- **Био-источники**: `Alt+1..4` (`BREATH`, `HEART`, `EYEX`, `EYEY`), `Alt+Up/Down` — глубина модуляции.

### Шумовой секвенсор и Akai MIDI (Parameter Locks)
- **`Space` или `Shift+G` (`G`)** / **Akai Пэд 8**: запуск / остановка 16-шагового секвенсора.
- **`,` / `.`**: выбор шага `1..16`; **`[` / `]`** (или **MIDI Program Change 0..3**): выбор дорожки `1..4`; **`Enter`**: вкл/выкл триггера выбранного шага.
- **Parameter Locks**: на экране `4:SEQ` перетаскивание любого параметра синтезатора (или удержание шагового пэда Akai + вращение ручки CC 1–8) записывает индивидуальный лок параметра в выбранный шаг (отображается янтарной точкой на ячейке шага).
