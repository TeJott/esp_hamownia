# 🔥 ESP Hamownia — Silnikowa Podłogowa Hamownia na ESP32-C3

> Przenośna hamownia silnikowa z podgrzewaniem opon, odczytem siły przez ogniwo tensometryczne HX711, wyświetlaczem OLED, kartą SD i interfejsem webowym przez Wi-Fi.

---

## 📸 Zdjęcia projektu

### Schemat PCB
> 📷 *Wstaw tutaj zdjęcie schematu PCB (np. z KiCad)*

```
docs/images/schematic.png
```

![Schemat PCB](docs/images/schematic.png)

### Płytka PCB — widok 3D
> 📷 *Wstaw tutaj render 3D płytki z KiCad*

![PCB 3D](docs/images/pcb_3d.png)

### Zmontowane urządzenie
> 📷 *Wstaw tutaj zdjęcie gotowego urządzenia*

![Gotowe urządzenie](docs/images/device_assembled.jpg)

### Panel webowy (Web UI)
> 📷 *Screenshot interfejsu webowego*

![Web UI](docs/images/webui_screenshot.png)

---

## 🎬 Pokaz działania

> 📹 *Wstaw tutaj link do wideo demonstracyjnego (YouTube/GitHub)*

[![Demo hamowni](docs/images/video_thumbnail.jpg)](https://www.youtube.com/watch?v=TWOJ_LINK)

---

## 🧰 Opis projektu

ESP Hamownia to autonomiczne urządzenie do pomiaru siły napędowej silnika elektrycznego lub spalinowego na stanowisku podłogowym. Urządzenie:

- **Mierzy siłę** (w kg) za pomocą tensometru i wzmacniacza HX711
- **Podgrzewa opony** sterowanym grzejnikiem PWM (MOSFET) przed testem
- **Zapisuje wyniki** na karcie SD jako pliki CSV
- **Wyświetla dane** na ekranie OLED 128×64
- **Udostępnia panel webowy** przez Wi-Fi (STA lub AP fallback)
- **Monitoruje napięcia** dwóch niezależnych baterii (logiczna + grzejnik)

---

## 🔧 Specyfikacja sprzętowa

| Parametr | Wartość |
|---|---|
| Mikrokontroler | ESP32-C3 SuperMini |
| Czujnik siły | Tensometr + HX711 (Gain 128) |
| Wyświetlacz | OLED SSD1306, 128×64, I2C 0x3C |
| Pamięć | Karta SD (SPI) |
| Sterowanie grzejnikiem | MOSFET PWM, 1kHz, 8-bit |
| Przycisk | 1× fizyczny (GPIO2, active LOW) |
| LED statusu | 1× zielona (active LOW, GPIO5) |
| Zasilanie logiki | LiPo/Li-Ion (monitoring przez ADC) |
| Zasilanie grzejnika | 12V+ (monitoring przez ADC) |
| Interfejs webowy | HTTP (port 80) + WebSocket (port 81) |

---

## 📌 Mapa pinów GPIO

| GPIO | Nazwa | Funkcja | Opis |
|---|---|---|---|
| GPIO0 | BAT_ADC | ADC | Pomiar napięcia baterii logicznej (dzielnik R1=100kΩ, R2=33kΩ) |
| GPIO1 | BAT_ADC2 | ADC | Pomiar napięcia baterii grzejnika (dzielnik R7=100kΩ, R8=33kΩ) |
| GPIO2 | BT2 | INPUT_PULLUP | Przycisk fizyczny (active LOW) |
| GPIO3 | OLED_SCL | I2C Clock | Zegar I2C dla OLED |
| GPIO4 | OLED_SDA | I2C Data | Dane I2C dla OLED |
| GPIO5 | STATUS_LED | OUTPUT | Dioda statusu (active LOW, 330Ω do 3.3V) |
| GPIO6 | HEATER_EN | OUTPUT (PWM) | Bramka MOSFET grzejnika (LOW=OFF, pulldown R6=10kΩ) |
| GPIO7 | HX711_DT | INPUT | Linia danych tensometru |
| GPIO8 | HX711_SCK | OUTPUT | Linia zegara tensometru |
| GPIO9 | SD_CS | OUTPUT | Chip Select karty SD |
| GPIO10 | SD_MOSI | SPI MOSI | Dane do karty SD |
| GPIO20 | SD_CLK | SPI CLK | Zegar SPI karty SD |
| GPIO21 | SD_MISO | SPI MISO | Dane z karty SD |

---

## 🏗️ Architektura oprogramowania

Firmware zbudowany modularnie — każdy podsystem to osobny plik nagłówkowy:

```
src/
├── main.cpp              # Główna pętla, inicjalizacja, koordynacja
├── config.h              # Centralna konfiguracja (piny, stałe, Wi-Fi)
├── app_state.h           # Globalny stan aplikacji (maszyna stanów)
├── battery_service.h     # Pomiar i monitoring napięcia baterii
├── button_service.h      # Obsługa przycisku (debounce, long-press)
├── display_service.h     # Wyświetlacz OLED (SSD1306, ekrany UI)
├── heater_service.h      # Sterowanie grzejnikiem PWM + bezpieczeństwo
├── hx711_service.h       # Odczyt tensometru HX711, filtrowanie
├── led_service.h         # Wzorce mrugania LED statusu
├── storage_service.h     # Zapis/odczyt CSV na karcie SD, metadata JSON
└── web_service.h         # Serwer HTTP + WebSocket, panel webowy
```

### Maszyna stanów

```
BOOT → IDLE → [TARE] → COUNTDOWN → HEATING → RECORDING → IDLE
                                                    ↓
                                              [ERROR / LOW_BAT]
```

---

## 📡 Interfejs webowy

Po uruchomieniu, urządzenie łączy się z siecią Wi-Fi (lub tworzy sieć AP `Hamownia`).

- **Adres:** `http://<IP>` lub `http://192.168.4.1` (tryb AP)
- **Port WebSocket:** `81` (dane live co 500ms)
- **Funkcje panelu:**
  - Podgląd live siły [kg]
  - Sterowanie grzejnikiem (czas, moc PWM)
  - Tare (zerowanie wagi)
  - Start/Stop nagrywania
  - Pobieranie plików CSV z karty SD
  - Podgląd napięcia obu baterii

---

## 💾 Format danych CSV

Pliki zapisywane są na karcie SD jako `/test_1.csv`, `/test_2.csv`, `/test_3.csv` (rotacja 3 plików).

```csv
time_ms,force_kg,heater_active,bat_logic_V,bat_heater_V
0,0.00,0,3.85,12.40
100,0.12,0,3.85,12.39
200,1.54,1,3.84,12.35
...
```

---

## 🔋 Monitoring baterii

Urządzenie monitoruje dwa niezależne źródła zasilania:

| Źródło | Próg ostrzeżenia | Akcja przy niskim napięciu |
|---|---|---|
| Bateria logiki (3.3V system) | < 3.3V | Ostrzeżenie LED + OLED |
| Bateria grzejnika (12V+) | < 10.0V | Blokada grzejnika |

**Korekcja ADC ESP32-C3:**
- GPIO0 (logika): korekta `−0.33V` (ADC czyta za nisko)
- GPIO1 (grzejnik): korekta `−0.36V` (ADC czyta za wysoko)

---

## 🛠️ Konfiguracja i kalibracja

### 1. Wi-Fi
Edytuj `src/config.h`:
```cpp
#define WIFI_SSID     "NazwaSieci"
#define WIFI_PASSWORD "TwojeHaslo"
```
> ⚠️ Nie commituj pliku z prawdziwym hasłem! Dodaj `config_local.h` do `.gitignore`.

### 2. Kalibracja belki tensometrycznej (HX711)

Biblioteka `bogde/HX711` używa konwencji **dzielnika**:

```
get_units() = (read_average() - tare_offset) / SCALE
SCALE = surowe_jednostki_ADC / znana_masa
```

#### Metoda A: Przez stronę WWW (zalecana)

1. Uruchom urządzenie i otwórz panel webowy
2. Kliknij **TARE** przy nieobciążonej belce
3. Połóż **znany ciężarek** (np. odważnik 1 kg, 2 kg, 5 kg)
4. W polu "Znana masa (kg)" wpisz wagę ciężarka
5. Kliknij **Kalibruj** — nowy współczynnik zapisze się w NVS (przetrwa restart)
6. Sprawdź, czy odczyt na wyświetlaczu zgadza się z ciężarkiem

#### Metoda B: Ręcznie w `src/config.h`

1. Ustaw `HX711_SCALE` na `1.0f` i wgraj firmware
2. Przez Serial Monitor (115200 baud) obserwuj odczyty
3. Połóż znany ciężarek i odczytaj surową wartość
4. Oblicz: `SCALE = odczyt / masa_w_kg`
5. Wpisz obliczoną wartość do `config.h`:

```cpp
// Belka 20 kg z HX711 (gain=128, VCC=3.3V)
// Typowa wartość SCALE: 200 000 – 500 000
#define HX711_SCALE  430000.0f   // <- wpisz swoją wartość
```

#### Przykład kalibracji

| Ciężarek | Surowy odczyt | SCALE |
|---|---|---|
| 1.0 kg | 428 000 | 428 000 |
| 2.0 kg | 860 000 | 430 000 |
| 5.0 kg | 2 140 000 | 428 000 |

**Średnia:** 428 667 → ustaw `HX711_SCALE 428667.0f`

#### Kolory przewodów belki tensometrycznej

| Kolor | Sygnał | Podłączenie do HX711 |
|---|---|---|
| Czerwony | E+ (wzbudzenie +) | E+ |
| Czarny | E- (wzbudzenie -) | E- |
| Zielony | A+ (sygnał +) | A+ |
| Biały | A- (sygnał -) | A- |

> Jeśli po kalibracji odczyty są **ujemne** przy prawidłowym obciążeniu, zamień przewody **E+ z E-** miejscami.

#### Najczęstsze problemy

| Problem | Rozwiązanie |
|---|---|
| Odczyt nie zmienia się | Sprawdź zasilanie HX711 (VCC=3.3V, GND) |
| Odczyt skacze | Dodaj kondensator 100 nF między VCC a GND HX711 |
| Nie wraca do 0 po zdjęciu ciężaru | Belka przeciążona mechanicznie lub histereza |
| `HX711 BRAK` w Serial Monitorze | Sprawdź przewody DT (GPIO7) i SCK (GPIO8) |

### 3. Parametry grzejnika
```cpp
#define HEATER_DEFAULT_DUTY         50    // % PWM (0-100)
#define HEATER_DEFAULT_DURATION_MS  5000  // czas grzania [ms]
#define HEATER_DEFAULT_COUNTDOWN_S  5     // odliczanie przed testem [s]
```

---

## 📦 Wymagania i środowisko

- **PlatformIO** (VS Code + PlatformIO IDE)
- **Board:** `seeed_xiao_esp32c3` lub zgodna z ESP32-C3
- **Framework:** Arduino

### Biblioteki (zdefiniowane w `platformio.ini`):
- `adafruit/Adafruit SSD1306`
- `bogde/HX711` lub `olkal/HX711_ADC`
- `bblanchon/ArduinoJson`
- `links2004/WebSockets`
- `ESP Async WebServer` lub `ESPAsyncWebServer`

---

## 🚀 Szybki start

```bash
# 1. Sklonuj repozytorium
git clone https://github.com/TeJott/esp_hamownia.git
cd esp_hamownia

# 2. Otwórz w VS Code z PlatformIO
code .

# 3. Skonfiguruj Wi-Fi i kalibrację w src/config.h

# 4. Wgraj firmware
pio run --target upload

# 5. Monitor Serial
pio device monitor --baud 115200
```

---

## 🔒 Bezpieczeństwo

- Grzejnik ma **maksymalny czas pracy 60s** (sprzętowy limit `HEATER_MAX_DURATION_MS`)
- Grzejnik jest **blokowany** przy napięciu baterii < 10V
- MOSFET bramka ma **pulldown 10kΩ** — przy resecie ESP grzejnik pozostaje wyłączony
- Zapis na SD ograniczony do **3 plików** (rotacja, brak przepełnienia karty)

---

## 📁 Struktura repozytorium

```
esp_hamownia/
├── src/                  # Kod źródłowy firmware
├── docs/
│   ├── images/           # Zdjęcia, screenshoty, schematy
│   └── schematic/        # Projekt KiCad (schemat + PCB)
├── platformio.ini        # Konfiguracja PlatformIO
├── .gitignore
└── README.md
```

---

## 📝 Licencja

MIT License — swobodne użycie, modyfikacja i dystrybucja.

---

*Projekt stworzony przez [@TeJott](https://github.com/TeJott)*
