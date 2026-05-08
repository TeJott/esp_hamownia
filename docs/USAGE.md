# 📖 Instrukcja obsługi — ESP Hamownia

## Pierwsze uruchomienie

1. **Naładuj obie baterie** — logiczną (LiPo 3.7V) i grzejnika (12V+)
2. **Włącz urządzenie** — LED wykona szybkie mrugnięcia (boot sequence)
3. **Poczekaj na połączenie Wi-Fi** — OK: LED wolne bicie serca | brak sieci: tryb AP
4. **Podłącz się do sieci** `Hamownia` (hasło: `hamownia123`) jeśli nie ma połączenia z routerem
5. **Otwórz przeglądarkę** → `http://192.168.4.1` (AP) lub IP przydzielone przez router

## Procedura pomiaru

### Krok 1 — Zerowanie (Tare)
- Naciśnij przycisk **krótko** lub kliknij **TARE** w web UI
- LED: szybkie podwójne mrugnięcie (potwierdzenie)
- Odczekaj chwilę aż wartość ustabilizuje się na 0.00 kg

### Krok 2 — Zapalnik przez strone WEB (opcjonalne)
- Ustaw czas grzania i moc PWM w web UI
- Kliknij **START HEATER** lub przytrzymaj przycisk (long press)
- LED: szybkie mrugnięcia — odliczanie 
- Zapalnik automatycznie wyłącza się po upływie czasu

### Krok 3 — Nagrywanie
- Kliknij **START RECORDING** w web UI
- LED: równomierne mrugnięcia co 250ms
- Dane zapisywane co 100ms do CSV na karcie SD
- Kliknij **STOP RECORDING** lub ponownie naciśnij przycisk

### Krok 4 — Pobieranie danych
- W web UI przejdź do sekcji **Recordings**
- Pobierz plik `/test_1.csv`, `/test_2.csv` lub `/test_3.csv`
- Otwórz w Excelu / LibreOffice / Python do analizy

## Wzorce LED statusu

| Wzorzec LED | Stan |
|---|---|
| Szybkie mrugnięcia (100ms) | Boot / Countdown |
| Wolne bicie serca (200/800ms) | Gotowy (Idle) |
| Podwójne krótkie | Tare wykonane |
| Równomierne 250/250ms | Nagrywanie |
| Przeważnie ON (150/50ms) | Grzejnik aktywny |
| Potrójne mrugnięcia + pauza | Błąd |
| Długie-krótkie | Niskie napięcie baterii |
| Powolne podwójne + długa pauza | Tryb AP |

## Ostrzeżenia i błędy

| Komunikat OLED | Przyczyna | Rozwiązanie |
|---|---|---|
| `BAT LOW` | Bateria logiki < 3.3V | Naładuj baterię |
| `HEATER LOCKED` | Bateria grzejnika < 10V | Naładuj baterię 12V |
| `SD ERROR` | Brak karty SD | Włóż kartę SD |
| `HX711 ERROR` | Brak tensometru | Sprawdź podłączenie |
