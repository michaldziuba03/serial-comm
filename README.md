# **🔌 Asynchroniczny Komunikator Szeregowy (WinAPI)**

> ⚠️ Projekt zaliczeniowy, kod służy jedynie celom demonstracyjnym i zawiera uproszczenia.

Prosta aplikacja konsolowa (CLI) w języku C++ do dwukierunkowej komunikacji przez port szeregowy (UART/RS-232/COM) na systemie Windows. Program umożliwia przesyłanie wiadomości tekstowych oraz plików binarnych.

Projekt demonstruje wykorzystanie **asynchronicznego wejścia/wyjścia (Overlapped I/O)** oraz parsowanie strumieniowe oparte na **Maszynie Stanów**.

## **✨ Główne funkcjonalności**

* **Asynchroniczność:** Wykorzystanie OVERLAPPED I/O w WinAPI zapobiega blokowaniu wątku głównego podczas operacji na wolnym porcie szeregowym.  
* **Transfer Plików:** Wysyłanie i odbieranie plików z wizualizacją postępu (Progress Bar).  
* **Wiadomości Tekstowe:** Czat w czasie rzeczywistym.  
* **Dynamiczne Parsowanie (Framing):** Prosty protokół ramkowy obsługiwany przez maszynę stanów, odporny na fragmentację danych (częste zjawisko w transmisji szeregowej).  
* **Efektywne zarządzanie pamięcią:** Buforowanie oparte na std::deque (brak alokacji dla każdego odczytu).

## **🛠️ Architektura i Technologia**

Projekt został napisany w C++ z wykorzystaniem natywnego API Windows (\<windows.h\>).

### **1. Protokół Ramki (Framing)**

Strumień danych jest dzielony na ramki o następującej strukturze:

```
[ 8 bajtów  ] [ N bajtów ]  [ 8 bajtów  ] [ X bajtów ]  
| Dł. Nazwy | |   Nazwa  |  | Dł. Danych| |   Dane   |  
| (int64_t) | |  (string)|  | (int64_t) | |  (binary)|
```

* **Wiadomość tekstowa:** Dł. Nazwy = 0, Nazwa jest pusta, Dane zawierają tekst.  
* **Plik:** Dł. Nazwy > 0, Nazwa zawiera nazwę pliku, Dane to zawartość pliku.

### **2. Maszyna Stanów (State Machine)**

Odbiór danych (FrameParser) nie polega na sztywnych blokach. Zamiast tego, odebrane bajty trafiają do bufora a parser przechodzi przez stany:

1. `READ_FILENAME_LEN` – Czeka na 8 bajtów nagłówka.  
2. `READ_FILENAME` – Czeka na nazwę pliku (jeśli występuje).  
3. `READ_PAYLOAD_LEN` – Czeka na rozmiar danych właściwych.  
4. `READ_PAYLOAD` – Pobiera dane partiami i wizualizuje postęp.

Dzięki temu rozwiązaniu, program działa poprawnie nawet, gdy ReadFile zwróci 1 bajt lub 1000 bajtów w jednym cyklu.

### **Konfiguracja**

Po uruchomieniu program zapyta o parametry połączenia:

```
Podaj port COM (domyslnie COM1): COM3  
Podaj baudrate (domyslnie 115200): 115200
```

*Domyślne ustawienia:* 8 bitów danych, 1 bit stopu, brak parzystości (8N1), Flow Control: RTS/CTS.

### **Sterowanie**

Menu główne obsługiwane jest klawiszami:

* \[T\] \- Wyślij wiadomość tekstową.  
* \[F\] \- Wyślij plik (należy podać ścieżkę absolutną lub względną).  
* \[Q\] \- Zakończ program i zamknij port.

Pliki odebrane zapisywane są automatycznie w folderze received_files w katalogu programu.

## **⚠️ Ograniczenia**

* Projekt edukacyjny: Protokół nie zawiera sumy kontrolnej (CRC) ani mechanizmu retransmisji (ACK/NACK).  
* Bardzo duże pliki mogą gubić pojedyncze bajty ze względu na specyfikę transmisji szeregowej bez korekcji błędów.  
* Program zakłada architekturę Little Endian (standard na x86/Windows).
