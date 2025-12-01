#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <conio.h>
#include <fstream>
#include <deque>
#include <filesystem>
#include <atomic>
#include <cstdint>
#include <cstring> // dla memcpy

namespace fs = std::filesystem;

// --- KONFIGURACJA ---
struct SerialPortConfig {
    std::string portName;
    int baudRate = CBR_115200;
};

// --- ZMIENNE GLOBALNE ---
HANDLE hSerial = nullptr;
std::atomic<bool> running = true;

// --- POMOCNICZE ---
inline std::string last_error_message(DWORD err) {
    LPVOID msg_buf = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg_buf, 0, nullptr);
    std::string message = msg_buf ? (LPSTR)msg_buf : "Unknown error";
    if (msg_buf) LocalFree(msg_buf);
    return message;
}

inline std::string get_filename(std::string& filepath) {
    size_t separator = filepath.find_last_of("\\/");
    return (separator == std::string::npos) ? filepath : filepath.substr(separator + 1);
}

// --- KLASA PARSUJĄCA DANE ---
class FrameParser {
    enum class State {
        READ_FILENAME_LEN,
        READ_FILENAME,
        READ_PAYLOAD_LEN,
        READ_PAYLOAD
    };

    State state = State::READ_FILENAME_LEN;
    std::deque<char> buffer; // Główny bufor strumieniowy

    // Zmienne tymczasowe do trzymania stanu ramki
    int64_t temp_filename_len = 0;
    std::string temp_filename;
    int64_t temp_payload_len = 0;

public:
    // Wrzucamy surowe dane z ReadFile do środka
    void append_data(const char* data, size_t size) {
        buffer.insert(buffer.end(), data, data + size);
        process(); // Próbujemy parsować po dodaniu nowych danych
    }

private:
    // Funkcja pomocnicza do pobierania int64 z przodu kolejki
    bool try_read_int64(int64_t& out_value) {
        if (buffer.size() < sizeof(int64_t)) return false;

        // Kopiujemy bajty do zmiennej (zakładamy Little Endian, standard na x86/Windows)
        // Robimy to ręcznie lub przez vector, żeby deque było ciągłe w pamięci dla memcpy
        std::vector<char> raw(sizeof(int64_t));
        for (size_t i = 0; i < sizeof(int64_t); ++i) raw[i] = buffer[i];

        std::memcpy(&out_value, raw.data(), sizeof(int64_t));

        // Usuwamy zużyte bajty
        buffer.erase(buffer.begin(), buffer.begin() + sizeof(int64_t));
        return true;
    }

    // Funkcja pomocnicza do pobierania N bajtów (string/blob)
    bool try_read_bytes(size_t length, std::vector<char>& out_bytes) {
        if (buffer.size() < length) return false;

        out_bytes.resize(length);
        for (size_t i = 0; i < length; ++i) {
            out_bytes[i] = buffer.front();
            buffer.pop_front();
        }
        return true;
    }

    void handle_complete_frame(const std::string& name, const std::vector<char>& payload) {
        if (name.empty()) {
            // WIADOMOŚĆ TEKSTOWA
            std::string text(payload.begin(), payload.end());
            std::cout << "\n[MSG] >> " << text << "\nKomenda (T/F/Q): ";
        }
        else {
            // PLIK
            fs::create_directories("received_files");
            fs::path path = fs::path("received_files") / name;

            std::ofstream outfile(path, std::ios::binary);
            if (outfile.write(payload.data(), payload.size())) {
                std::cout << "\n[FILE] Otrzymano plik: " << name << " (" << payload.size() << " B)\nKomenda (T/F/Q): ";
            }
            else {
                std::cerr << "\n[ERROR] Blad zapisu pliku: " << name << "\n";
            }
        }
    }

    void process() {
        while (true) {
            switch (state) {
            case State::READ_FILENAME_LEN:
                if (!try_read_int64(temp_filename_len)) return; // Czekamy na więcej danych

                // Sanity check
                if (temp_filename_len < 0 || temp_filename_len > 1024) {
                    // Tutaj można dodać logikę resetu, jeśli otrzymamy śmieci
                    // Na razie ufamy, że długość jest OK
                }

                if (temp_filename_len == 0) {
                    temp_filename = "";
                    state = State::READ_PAYLOAD_LEN; // Pomijamy czytanie nazwy
                }
                else {
                    state = State::READ_FILENAME;
                }
                break;

            case State::READ_FILENAME:
            {
                std::vector<char> name_buf;
                if (!try_read_bytes(static_cast<size_t>(temp_filename_len), name_buf)) return;
                temp_filename.assign(name_buf.begin(), name_buf.end());
                state = State::READ_PAYLOAD_LEN;
            }
            break;

            case State::READ_PAYLOAD_LEN:
                if (!try_read_int64(temp_payload_len)) return;
                state = State::READ_PAYLOAD;
                break;

            case State::READ_PAYLOAD:
            {
                std::vector<char> payload_buf;
                if (!try_read_bytes(static_cast<size_t>(temp_payload_len), payload_buf)) return;

                // Mamy całą ramkę!
                handle_complete_frame(temp_filename, payload_buf);

                // Reset maszyny stanów
                state = State::READ_FILENAME_LEN;
                temp_filename.clear();
                temp_filename_len = 0;
                temp_payload_len = 0;
            }
            break;
            }
        }
    }
};

// --- OBSŁUGA PORTU ---

void read_serial_port_config(SerialPortConfig& config) {
    const char* defaultPort = "COM1";
    std::string portNameStr;
    std::cout << "Podaj port COM (domyslnie " << defaultPort << "): ";
    std::getline(std::cin, portNameStr);
    if (portNameStr.empty()) portNameStr = defaultPort;

    std::cout << "Podaj baudrate (domyslnie 115200): ";
    std::string baudStr;
    std::getline(std::cin, baudStr);
    config.portName = portNameStr;
    if (!baudStr.empty()) config.baudRate = std::stoi(baudStr);
}

void open_serial_port(SerialPortConfig& config) {
    std::string portPath = "\\\\.\\" + config.portName;
    hSerial = CreateFileA(portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hSerial == INVALID_HANDLE_VALUE) throw std::runtime_error("Nie mozna otworzyc portu.");

    DCB dcb = { 0 };
    dcb.DCBlength = sizeof(dcb);
    GetCommState(hSerial, &dcb);
    dcb.BaudRate = config.baudRate;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    SetCommTimeouts(hSerial, &timeouts);

    std::cout << "Port " << config.portName << " otwarty.\n";
}

void close_serial_port() {
    if (!hSerial) return;
    running = false;
    CancelIoEx(hSerial, NULL);
    CloseHandle(hSerial);
    hSerial = NULL;
}

// --- NOWY READER LOOP ---
void reader_loop() {
    FrameParser parser;

    // Alokujemy jeden statyczny bufor do odczytu z API Windowsa
    // Nie musimy go niszczyć i tworzyć od nowa.
    const size_t RAW_BUF_SIZE = 1024;
    char raw_buffer[RAW_BUF_SIZE];

    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    while (running && hSerial) {
        DWORD bytesRead = 0;

        // Rozpoczynamy operację asynchroniczną
        BOOL ok = ReadFile(hSerial, raw_buffer, RAW_BUF_SIZE, &bytesRead, &ov);

        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            // Czekamy na dane (max 100ms, żeby móc sprawdzić flagę 'running')
            DWORD wait = WaitForSingleObject(ov.hEvent, 100);
            if (wait == WAIT_OBJECT_0) {
                GetOverlappedResult(hSerial, &ov, &bytesRead, FALSE);
            }
            else {
                // Timeout - brak danych w tym cyklu, lecimy dalej
                continue;
            }
        }
        else if (!ok) {
            // Prawdziwy błąd
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (bytesRead > 0) {
            // Przekazujemy to co przyszło do parsera
            // Parser zajmuje się sklejaniem kawałków w całość
            parser.append_data(raw_buffer, bytesRead);
        }

        ResetEvent(ov.hEvent); // Ważne przy reuse struktury OVERLAPPED w pętli
    }
    CloseHandle(ov.hEvent);
}

// --- PISANIE ---
// (Reszta kodu pisania bez większych zmian, tylko dopasowanie do int64_t dla spójności)

bool write_overlapped(const char* buf, size_t size) {
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD written = 0;
    if (!WriteFile(hSerial, buf, (DWORD)size, &written, &ov) && GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(ov.hEvent, INFINITE);
        GetOverlappedResult(hSerial, &ov, &written, FALSE);
    }
    CloseHandle(ov.hEvent);
    return true;
}

void send_frame(const std::string& filename, const char* data, size_t size) {
    int64_t fn_len = filename.size();
    int64_t pl_len = size;

    write_overlapped((char*)&fn_len, sizeof(fn_len));
    if (fn_len > 0) write_overlapped(filename.data(), filename.size());
    write_overlapped((char*)&pl_len, sizeof(pl_len));
    write_overlapped(data, size);
}

void writer_loop() {
    std::cout << "------------------------------------------------\n";
    std::cout << " T - Wyslij Tekst | F - Wyslij Plik | Q - Wyjscie\n";
    std::cout << "------------------------------------------------\n";

    while (running) {
        char cmd = _getch();
        cmd = tolower(cmd);

        if (cmd == 'q') {
            close_serial_port();
            break;
        }
        else if (cmd == 't') {
            std::cout << "Wpisz tekst: ";
            std::string text;
            std::getline(std::cin, text);
            send_frame("", text.data(), text.size());
            std::cout << "Wyslanio tekst.\nKomenda (T/F/Q): ";
        }
        else if (cmd == 'f') {
            std::cout << "Sciezka pliku: ";
            std::string path;
            std::getline(std::cin, path);
            std::ifstream f(path, std::ios::binary | std::ios::ate);
            if (f) {
                size_t size = f.tellg();
                std::vector<char> buf(size);
                f.seekg(0);
                f.read(buf.data(), size);
                send_frame(get_filename(path), buf.data(), size);
                std::cout << "Wyslano plik.\nKomenda (T/F/Q): ";
            }
            else {
                std::cout << "Blad pliku.\n";
            }
        }
    }
}

int main() {
    SerialPortConfig config;
    try {
        read_serial_port_config(config);
        open_serial_port(config);
        std::thread(reader_loop).detach();
        writer_loop();
    }
    catch (std::exception& e) {
        std::cerr << e.what() << "\n";
    }
    return 0;
}
