# 🔧 Dokumentacja sprzętowa — ESP Hamownia

## Schemat blokowy systemu

```
┌─────────────────────────────────────────────────────────┐
│                     ESP32-C3 SuperMini                  │
│                                                         │
│  GPIO0 ←── [R1=100k/R2=33k] ←── BAT_LOGIC (+)         │
│  GPIO1 ←── [R7=100k/R8=33k] ←── BAT_HEATER (+)        │
│  GPIO2 ←── [BUTTON BT2]                                │
│  GPIO3 ──→ [OLED SCL]                                  │
│  GPIO4 ──→ [OLED SDA]                                  │
│  GPIO5 ──→ [LED 330Ω] ──→ GND                         │
│  GPIO6 ──→ [MOSFET GATE] → [HEATER ELEMENT]           │
│  GPIO7 ←── [HX711 DT]                                  │
│  GPIO8 ──→ [HX711 SCK]                                 │
│  GPIO9 ──→ [SD CS]                                     │
│  GPIO10──→ [SD MOSI]                                   │
│  GPIO20──→ [SD CLK]                                    │
│  GPIO21←── [SD MISO]                                   │
└─────────────────────────────────────────────────────────┘
```

## Obliczenie dzielnika napięcia

Dla baterii grzejnika 12V z dzielnikiem R_top=100kΩ, R_bot=33kΩ:

```
V_adc = V_bat × R_bot / (R_top + R_bot)
V_bat = V_adc × (R_top + R_bot) / R_bot
V_bat = V_adc × 133000 / 33000
V_bat = V_adc × 4.0303
```

Maksymalne napięcie wejściowe (przy V_adc = 3.3V):
```
V_bat_max = 3.3 × 4.0303 = 13.3V
```

## Sterowanie grzejnikiem MOSFET

- Bramka zabezpieczona rezystorem pulldown R6=10kΩ do GND
- PWM: 1kHz, 8-bit (255 = 100% mocy)
- Zabezpieczenie: programowy timer max 60s + lockout przy U_bat < 10V

## Lista materiałów (BOM)

| Oznaczenie | Komponent | Wartość | Opis |
|---|---|---|---|
| U1 | ESP32-C3 SuperMini | — | Moduł mikrokontrolera |
| U2 | SSD1306 OLED | 128×64 | Wyświetlacz I2C |
| U3 | HX711 | — | Wzmacniacz tensometru |
| U4 | SD Card Module | SPI | Moduł karty SD |
| Q1 | N-MOSFET | IRLZ44N lub IRF540 | Tranzystor grzejnika |
| R1, R7 | Rezystor | 100kΩ | Górny dzielnik napięcia |
| R2, R8 | Rezystor | 33kΩ | Dolny dzielnik napięcia |
| R3 | Rezystor | 330Ω | Ogranicznik LED |
| R6 | Rezystor | 10kΩ | Pulldown bramki MOSFET |
| LED1 | Dioda LED | Zielona | LED statusu |
| BT2 | Mikroprzycisk | — | Przycisk operacyjny |
| J1 | Złącze | 2-pin | Zasilanie logiki |
| J2 | Złącze | 2-pin | Zasilanie grzejnika |
| J3 | Złącze | 2-pin | Tensometr |
| J4 | Złącze | 2-pin | Element grzejny |
