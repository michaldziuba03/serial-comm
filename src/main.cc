#include <windows.h>
#include <iostream>
#include <fstream>
#include <string>
#include <fcntl.h>
#include <stdio.h>
#include <io.h>

std::string readFile(std::string filepath)
{
    std::ifstream f(filepath);
    std::string c;

    if (f) {
        c.assign((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        f.close();
    }
    else {
        throw std::exception("Unable to read");
    }

    return c;
}

bool readSerialPort(void* commHandle,
    unsigned long* numberOfBytesRead,
    unsigned long bufferSize, void* buffer)
{
    COMSTAT comstat;
    unsigned long errors, numberOfBytesToRead;
    ClearCommError(commHandle, &errors, &comstat);
    std::cout << "Stat.cbInQue = " << comstat.cbInQue;
    if (comstat.cbInQue > 0) {
        if (comstat.cbInQue > bufferSize)
            numberOfBytesToRead = bufferSize;
        else
            numberOfBytesToRead = comstat.cbInQue;
        if (ReadFile(commHandle, buffer, numberOfBytesToRead,
            numberOfBytesRead, NULL))
            std::cout << " bajty" << std::endl;
        else
            std::cout << "Błąd odczytu! " << std::endl;
    }
    else
        *numberOfBytesRead = 0;
    return true;
}

bool writeSerialPort(void* commHandle, unsigned long numBytes,
    void* buffer)
{
    unsigned long lpEvtMask, numberOfBytesWritten;
    WriteFile(commHandle, buffer, numBytes,
        &numberOfBytesWritten, NULL);
    WaitCommEvent(commHandle, &lpEvtMask, NULL);

    std::cout << "Written: " << numberOfBytesWritten << "\n";

    return true;
}

int main() {
    //_setmode(_fileno(stdout), _O_U16TEXT);
    std::string portName = "COM1";
    std::string text = "Hello Wolrd\n"; //readFile("art.txt");

    HANDLE hSerial = CreateFileA(
        portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "Nie można otworzyć portu " << portName << ". Kod błędu: " << GetLastError() << "\n";
        return 1;
    }

    DCB dcb{};
    dcb.DCBlength = sizeof(DCB);
    if (!GetCommState(hSerial, &dcb)) {
        std::cerr << "GetCommState failed. Kod: " << GetLastError() << "\n";
        CloseHandle(hSerial);
        return 1;
    }

    dcb.DCBlength = sizeof(dcb);
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = ODDPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = true;
    dcb.fDsrSensitivity = false;
    dcb.fParity = true;
    dcb.fOutX = false;
    dcb.fInX = false;
    dcb.fNull = false;
    dcb.fAbortOnError = true;
    dcb.fOutxCtsFlow = false;
    dcb.fOutxDsrFlow = false;
    dcb.fDtrControl = DTR_CONTROL_DISABLE;
    dcb.fDsrSensitivity = false;
    dcb.fRtsControl = RTS_CONTROL_DISABLE;
    dcb.fOutxCtsFlow = false;
    dcb.fOutxCtsFlow = false;

    SetCommState(hSerial, &dcb);

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 10000;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 1000;

    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutConstant = 5000;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "SetCommTimeouts failed. Kod: " << GetLastError() << "\n";
        CloseHandle(hSerial);
        return 1;
    }

    writeSerialPort(hSerial, text.length(), (void*)text.c_str());

    char buff[24000] = {0};
    DWORD readBytes = 0;
    if (!readSerialPort(hSerial, &readBytes, text.size(), buff)) {
        std::cerr << "reading serial port failed\n";
    }

    for (int i = 0; i < readBytes; ++i) {
        std::cout << buff[i];
    }

    CloseHandle(hSerial);
    return 0;
}
