#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

#include "common/Chess.h"

using chess::Board;
using chess::Color;

namespace {

struct Client {
    int fd = -1;
    std::string name = "Player";
};

bool sendLine(int fd, const std::string& line) {
    std::string data = line;
    if (data.empty() || data.back() != '\n') data.push_back('\n');
    const char* ptr = data.c_str();
    size_t left = data.size();
    while (left > 0) {
        ssize_t sent = send(fd, ptr, left, MSG_NOSIGNAL);
        if (sent <= 0) return false;
        ptr += sent;
        left -= static_cast<size_t>(sent);
    }
    return true;
}

bool recvLine(int fd, std::string& out) {
    out.clear();
    char ch = 0;
    while (true) {
        ssize_t n = recv(fd, &ch, 1, 0);
        if (n <= 0) return false;
        if (ch == '\n') break;
        if (ch != '\r') out.push_back(ch);
        if (out.size() > 2048) return false;
    }
    return true;
}

void closeClient(Client& c) {
    if (c.fd >= 0) {
        close(c.fd);
        c.fd = -1;
    }
}

std::string trimNameFromHello(const std::string& line) {
    if (line.rfind("HELLO ", 0) == 0) {
        std::string name = line.substr(6);
        if (!name.empty() && name.size() <= 40) return name;
    }
    return "Player";
}

Client acceptClient(int listenFd) {
    sockaddr_in addr{};
    socklen_t len = sizeof(addr);
    int fd = accept(listenFd, reinterpret_cast<sockaddr*>(&addr), &len);
    Client c;
    c.fd = fd;
    if (fd < 0) return c;

    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip));
    std::cout << "Yeni bağlantı: " << ip << ':' << ntohs(addr.sin_port) << std::endl;

    sendLine(fd, "MSG Linux TCP satranç sunucusuna bağlandın. Rakip bekleniyor...");
    std::string hello;
    if (recvLine(fd, hello)) c.name = trimNameFromHello(hello);
    return c;
}

void matchThread(Client white, Client black) {
    std::cout << "Maç başladı: " << white.name << " (Beyaz) vs " << black.name << " (Siyah)" << std::endl;
    Board board;

    sendLine(white.fd, "COLOR W");
    sendLine(black.fd, "COLOR B");
    sendLine(white.fd, "MSG Rakip bulundu: " + black.name);
    sendLine(black.fd, "MSG Rakip bulundu: " + white.name);
    sendLine(white.fd, "START");
    sendLine(black.fd, "START");

    while (true) {
        Client& current = board.sideToMove() == Color::White ? white : black;
        Client& other = board.sideToMove() == Color::White ? black : white;

        std::string line;
        if (!recvLine(current.fd, line)) {
            sendLine(other.fd, "MSG Rakip bağlantıdan çıktı. Oyun bitti.");
            break;
        }

        if (line.rfind("MOVE ", 0) != 0) {
            sendLine(current.fd, "ERR Sadece MOVE komutu bekleniyor.");
            continue;
        }

        std::string uci = line.substr(5);
        if (!board.makeMoveUci(uci)) {
            sendLine(current.fd, "ERR Geçersiz hamle: " + uci);
            continue;
        }

        sendLine(current.fd, "OK " + uci);
        sendLine(other.fd, "MOVE " + uci);

        std::string status = board.statusText();
        if (board.isCheckmate() || board.isStalemate()) {
            sendLine(white.fd, "MSG " + status);
            sendLine(black.fd, "MSG " + status);
            break;
        }
    }

    closeClient(white);
    closeClient(black);
    std::cout << "Maç kapandı." << std::endl;
}

int createListenSocket(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 32) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

} // namespace

int main(int argc, char** argv) {
    signal(SIGPIPE, SIG_IGN);

    int port = 5555;
    if (argc >= 2) {
        std::istringstream ss(argv[1]);
        ss >> port;
        if (port <= 0 || port > 65535) port = 5555;
    }

    int listenFd = createListenSocket(port);
    if (listenFd < 0) {
        std::cerr << "Sunucu başlatılamadı. Port kullanımda olabilir: " << port << std::endl;
        return 1;
    }

    std::cout << "Linux TCP ChessServer çalışıyor. Port: " << port << std::endl;
    std::cout << "Windows istemciler bu IP/port ile bağlanabilir." << std::endl;

    while (true) {
        Client white = acceptClient(listenFd);
        if (white.fd < 0) continue;
        Client black = acceptClient(listenFd);
        if (black.fd < 0) {
            closeClient(white);
            continue;
        }
        std::thread(matchThread, white, black).detach();
    }
}
