#include <arpa/inet.h>
#include <cctype>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

const char* SERIAL_PORT = "/dev/ttyACM0";
const int BAUDRATE = B115200;

const char* PI5_IP = "10.120.2.207";   // CHANGE THIS
const int UDP_PORT = 9000;


// --------------------------------------------------
// Configure serial port
// --------------------------------------------------
bool configureSerial(int fd)
{
    struct termios tty{};

    if (tcgetattr(fd, &tty) != 0)
    {
        perror("tcgetattr");
        return false;
    }

    cfsetispeed(&tty, BAUDRATE);
    cfsetospeed(&tty, BAUDRATE);

    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(INLCR | ICRNL);

    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0)
    {
        perror("tcsetattr");
        return false;
    }

    return true;
}


// --------------------------------------------------
// Send command to logger
// --------------------------------------------------
bool sendCommand(int fd, const std::string& command)
{
    std::string cmd = command + "\r\n";

    ssize_t n = write(fd, cmd.c_str(), cmd.size());

    if (n < 0)
    {
        perror("write");
        return false;
    }

    std::this_thread::sleep_for(
        std::chrono::milliseconds(200));

    return true;
}


// --------------------------------------------------
// Main
// --------------------------------------------------
int main()
{
    std::cout << "Starting logger acquisition...\n";


    // ------------------------------------------------
    // Open serial port
    // ------------------------------------------------
    int serialFd = open(
        SERIAL_PORT,
        O_RDWR | O_NOCTTY | O_SYNC);

    if (serialFd < 0)
    {
        perror("Cannot open serial port");
        return 1;
    }

    if (!configureSerial(serialFd))
    {
        close(serialFd);
        return 1;
    }

    std::cout << "Serial port opened: "
              << SERIAL_PORT << "\n";


    // ------------------------------------------------
    // Create UDP socket
    // ------------------------------------------------
    int udpFd = socket(AF_INET, SOCK_DGRAM, 0);

    if (udpFd < 0)
    {
        perror("socket");

        close(serialFd);

        return 1;
    }

    sockaddr_in pi5Address{};

    pi5Address.sin_family = AF_INET;
    pi5Address.sin_port = htons(UDP_PORT);

    if (inet_pton(
            AF_INET,
            PI5_IP,
            &pi5Address.sin_addr) <= 0)
    {
        std::cerr
            << "Invalid Pi 5 IP address\n";

        close(udpFd);
        close(serialFd);

        return 1;
    }

    std::cout
        << "UDP destination: "
        << PI5_IP
        << ":"
        << UDP_PORT
        << "\n";


    // ------------------------------------------------
    // Wait for logger to initialize
    // ------------------------------------------------
    std::this_thread::sleep_for(
        std::chrono::seconds(2));


    // ------------------------------------------------
    // Authentication
    // ------------------------------------------------
    const std::string PASSWORD = "!CT2MC!";

    std::cout
        << "Authenticating...\n";

    sendCommand(
        serialFd,
        PASSWORD);

    std::this_thread::sleep_for(
        std::chrono::seconds(1));


    // ------------------------------------------------
    // Clear input buffer
    // ------------------------------------------------
    char temp[512];

    while (read(
        serialFd,
        temp,
        sizeof(temp)) > 0)
    {
        // Discard old data
    }


    // ------------------------------------------------
    // Start READ
    // ------------------------------------------------
    std::cout
        << "Starting READ...\n";

    sendCommand(
        serialFd,
        PASSWORD);

    sendCommand(
        serialFd,
        "READ");


    // ------------------------------------------------
    // Open local data file
    // ------------------------------------------------
    std::ofstream outputFile(
        "DATAFILE_dump.csv",
        std::ios::out | std::ios::app);

    if (!outputFile.is_open())
    {
        std::cerr
            << "Cannot open output file\n";

        close(udpFd);
        close(serialFd);

        return 1;
    }


    // ------------------------------------------------
    // Read data
    // ------------------------------------------------
    std::string line;

    char c;

    while (true)
    {
        ssize_t n =
            read(
                serialFd,
                &c,
                1);

        if (n <= 0)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5));

            continue;
        }


        // Build complete line
        if (c == '\n')
        {
            if (line.empty())
                continue;


            // Remove CR if present
            if (!line.empty() &&
                line.back() == '\r')
            {
                line.pop_back();
            }


            // Display
            std::cout
                << line
                << std::endl;


            // ----------------------------------------
            // Save exact logger record
            // ----------------------------------------
            outputFile
                << line
                << "\n";

            outputFile.flush();


            // ----------------------------------------
            // Send exact record to Pi 5
            // ----------------------------------------
            ssize_t sent =
                sendto(
                    udpFd,
                    line.c_str(),
                    line.size(),
                    0,
                    reinterpret_cast<sockaddr*>(
                        &pi5Address),
                    sizeof(pi5Address));


            if (sent < 0)
            {
                perror("UDP send");
            }


            // ----------------------------------------
            // Convert line to uppercase
            // ----------------------------------------
            std::string upper = line;

            for (char& ch : upper)
            {
                ch = static_cast<char>(
                    std::toupper(
                        static_cast<unsigned char>(ch)));
            }


            // ----------------------------------------
            // Detect READ DONE
            // ----------------------------------------
            if (upper.find("READ DONE") !=
                std::string::npos)
            {
                std::cout
                    << "READ completed.\n";

                break;
            }


            // ----------------------------------------
            // Detect FILE REMOVED
            // ----------------------------------------
            if (upper.find("FILE REMOVED") !=
                std::string::npos)
            {
                std::cout
                    << "FILE REMOVED detected.\n";

                break;
            }


            // Start next line
            line.clear();
        }
        else
        {
            line += c;
        }
    }


    // ------------------------------------------------
    // Close
    // ------------------------------------------------
    outputFile.close();

    close(udpFd);

    close(serialFd);


    std::cout
        << "\nAcquisition finished.\n";

    return 0;
}
