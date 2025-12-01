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

const size_t CHUNK_SIZE = 512;

struct Chunk {
    size_t size = 0;
    char* bytes = nullptr;

    Chunk() {
        bytes = new char[CHUNK_SIZE];
        size = 0;
    }

    ~Chunk() { delete[] bytes; }

    inline char* data() { return bytes; }
    inline size_t capacity() { return CHUNK_SIZE; }
};

struct SerialPortConfig {
    std::string portName;
    int baudRate = CBR_115200;
};

HANDLE hSerial = nullptr;
std::atomic<bool> running = true;

inline std::string last_error_message(DWORD err) {
    LPVOID msg_buf = nullptr;
    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&msg_buf, 0, nullptr);

    std::string message;
    if (msg_buf) {
        message = (LPSTR)msg_buf;
        LocalFree(msg_buf);
    }
    else {
        message = "Unknown error";
    }
    return message;
}

inline std::string get_filename(std::string& filepath) {
    size_t separator = filepath.find_last_of("\\/");
    std::string filename = (separator == std::string::npos)
        ? filepath
        : filepath.substr(separator + 1);
    return filename;
}

void read_serial_port_config(SerialPortConfig& config) {
    const char* defaultPort = "COM1";
    std::string portNameStr;
    std::string baudRateStr;

    std::cout << "Podaj port COM (domyslnie " << defaultPort << "): ";
    std::getline(std::cin, portNameStr);
    if (portNameStr.empty()) portNameStr = defaultPort;

    int baudRate = CBR_115200;
    std::cout << "Podaj baudrate (domyslnie " << baudRate << "): ";
    std::getline(std::cin, baudRateStr);
    if (!baudRateStr.empty()) {
        try {
            baudRate = std::stoi(baudRateStr);
            if (baudRate <= 0) throw std::exception();
        }
        catch (...) {
            std::cout << " [!] Bledny baudrate. Uzywam domyslnej wartosci.\n";
        }
    }

    config.portName = portNameStr;
    config.baudRate = baudRate;
}

void open_serial_port(SerialPortConfig& config) {
    std::string portPath = "\\\\.\\" + config.portName;
    hSerial = CreateFileA(
        portPath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, NULL);

    if (hSerial == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Nie mozna otworzyc portu " + config.portName);

    SetupComm(hSerial, 32768, 32768);

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams))
        throw std::runtime_error("Blad pobierania stanu portu.");

    dcbSerialParams.fOutxCtsFlow = TRUE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_HANDSHAKE;
    dcbSerialParams.fOutX = FALSE;
    dcbSerialParams.fInX = FALSE;

    dcbSerialParams.BaudRate = config.baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    SetCommState(hSerial, &dcbSerialParams);
        //throw std::runtime_error("Blad ustawiania parametrow portu.");

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    SetCommTimeouts(hSerial, &timeouts);

    std::cout << "Port " << config.portName << " otwarty @ " << config.baudRate
        << ".\n";
}

void close_serial_port()
{
    if (!hSerial)
        return;

    std::cout << "[INFO] Zamykanie portu COM...\n";

    running = false;

    CancelIo(hSerial);
    CancelIoEx(hSerial, NULL);

    EscapeCommFunction(hSerial, CLRRTS);
    EscapeCommFunction(hSerial, CLRDTR);

    SetCommMask(hSerial, 0);

    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    PurgeComm(
        hSerial,
        PURGE_RXABORT | PURGE_TXABORT | PURGE_RXCLEAR | PURGE_TXCLEAR
    );

    CloseHandle(hSerial);
    hSerial = NULL;

    std::cout << "[INFO] Port COM zamknięty poprawnie.\n";
}

std::vector<char> read_file(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Nie można otworzyć pliku: " + filepath);

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<char> buffer(size);
    if (!file.read(buffer.data(), size))
        throw std::runtime_error("Błąd podczas odczytu pliku: " + filepath);

    return buffer;
}

bool write_overlapped(const char* buf, size_t size) {
    OVERLAPPED ov = { 0 };
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) throw std::runtime_error("CreateEvent() failed");

    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(hSerial, buf, (DWORD)size, &bytesWritten, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD wait = WaitForSingleObject(ov.hEvent, INFINITE);
        if (wait == WAIT_OBJECT_0)
            GetOverlappedResult(hSerial, &ov, &bytesWritten, FALSE);
    }
    else if (!ok) {
        DWORD err = GetLastError();
        CloseHandle(ov.hEvent);
        throw std::runtime_error(last_error_message(err));
    }

    CloseHandle(ov.hEvent);
    return true;
}

void write_file_async(const std::string& filename, const char* buf, size_t size) {
    int64_t filename_size = filename.size();
    int64_t payload_size = size;

    write_overlapped(reinterpret_cast<const char*>(&filename_size),
        sizeof(filename_size));
    write_overlapped(filename.data(), filename.size());
    write_overlapped(reinterpret_cast<const char*>(&payload_size),
        sizeof(payload_size));

    size_t sent = 0;
    while (sent < size && running) {
        size_t chunk = min(CHUNK_SIZE, size - sent);
        write_overlapped(buf + sent, chunk);
        sent += chunk;
    }
}

void write_text_async(const std::string& text) {
    int64_t filename_size = 0;
    int64_t payload_size = text.size();
    write_overlapped(reinterpret_cast<const char*>(&filename_size),
        sizeof(filename_size));
    write_overlapped(reinterpret_cast<const char*>(&payload_size),
        sizeof(payload_size));
    write_overlapped(text.data(), text.size());
}

void writer_loop() {
    std::cout << "------------------------------------------------\n";
    std::cout << " T - Wyslij Tekst | F - Wyslij Plik | Q - Wyjscie\n";
    std::cout << "------------------------------------------------\n";

    while (running) {
        std::cout << "Komenda (T/F/Q): ";
        char cmd = _getch();
        cmd = tolower(cmd);
        std::cout << cmd << "\n";

        if (cmd == 'q') {
            std::cout << "Zamykanie portu szeregowego...\n";
            close_serial_port();
            break;
        }
        else if (cmd == 't') {
            std::cout << "Wpisz tekst: ";
            std::string text;
            std::getline(std::cin, text);
            write_text_async(text);
        }
        else if (cmd == 'f') {
            std::cout << "Podaj sciezke do pliku: ";
            std::string filepath;
            std::getline(std::cin, filepath);

            try {
                std::string filename = get_filename(filepath);
                std::vector<char> buffer = read_file(filepath);
                write_file_async(filename, buffer.data(), buffer.size());
            }
            catch (const std::runtime_error& e) {
                std::cerr << "[WARN] " << e.what() << '\n';
            }
        }
    }
}

void reader_loop() {
    std::deque<Chunk> buffers;
    size_t total_read = 0;

    while (running) {
        if (!running || !hSerial)
            break;

        Chunk chunk;
        memset(chunk.data(), 0, chunk.capacity());

        OVERLAPPED ov = { 0 };
        ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        DWORD bytesRead = 0;

        BOOL ok = ReadFile(hSerial, chunk.data(), (DWORD)chunk.capacity(),
            &bytesRead, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD wait = WaitForSingleObject(ov.hEvent, 100); // 100ms
            if (wait == WAIT_OBJECT_0)
                GetOverlappedResult(hSerial, &ov, &bytesRead, FALSE);
        }

        if (bytesRead > 0) {
            total_read += bytesRead;
            buffers.push_back(std::move(chunk));
            std::cout << "Buffers: " << buffers.size()
                << "; Bytes: " << total_read << "\n";
        }
        else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        CloseHandle(ov.hEvent);
    }
}

void start_reader() {
    std::thread(reader_loop).detach();
}

int main() {
    std::cout << "=== Komunikator Szeregowy ===\n";
    SerialPortConfig config;

    try {
        read_serial_port_config(config);
        open_serial_port(config);
        start_reader();
        writer_loop();
    }
    catch (const std::runtime_error& e) {
        std::cerr << "[FATAL] " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    catch (...) {
        std::cerr << "[FATAL] Nieznany wyjątek!\n";
        return EXIT_FAILURE;
    }

    return 0;
}
