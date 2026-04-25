#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windowsx.h>

#include <atomic>
#include <chrono>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "common/Chess.h"

#pragma comment(lib, "ws2_32.lib")

using chess::Board;
using chess::Color;
using chess::Move;
using chess::PieceType;

namespace {

constexpr int BOARD_X = 30;
constexpr int BOARD_Y = 30;
constexpr int SQ = 80;
constexpr int BOARD_SIZE = SQ * 8;
constexpr int PANEL_X = 705;
constexpr int WINDOW_W = 940;
constexpr int WINDOW_H = 725;
constexpr UINT WM_NETLINE = WM_APP + 42;

constexpr int ID_BOT = 1001;
constexpr int ID_LOCAL = 1002;
constexpr int ID_RESET = 1003;
constexpr int ID_ONLINE = 1004;
constexpr int ID_HOST = 1005;
constexpr int ID_PORT = 1006;
constexpr int ID_NAME = 1007;

enum class Mode { Menu, Local, Bot, Online };

HWND g_hwnd = nullptr;
HWND g_hostEdit = nullptr;
HWND g_portEdit = nullptr;
HWND g_nameEdit = nullptr;
HFONT g_pieceFont = nullptr;
HFONT g_textFont = nullptr;
HFONT g_titleFont = nullptr;

Board g_board;
Mode g_mode = Mode::Menu;
Color g_myColor = Color::White;
bool g_flipBoard = false;
int g_selected = -1;
std::vector<Move> g_selectedMoves;
std::wstring g_extraStatus = L"Mod seç: bot, aynı bilgisayar veya online.";

SOCKET g_socket = INVALID_SOCKET;
std::atomic<bool> g_netRunning{false};
std::thread g_netThread;

std::wstring utf8ToWide(const std::string& text) {
    if (text.empty()) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (count <= 0) return L"";
    std::wstring out(static_cast<size_t>(count - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), count);
    return out;
}

std::string wideToUtf8(const std::wstring& text) {
    if (text.empty()) return "";
    int count = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (count <= 0) return "";
    std::string out(static_cast<size_t>(count - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, out.data(), count, nullptr, nullptr);
    return out;
}

std::wstring getWindowText(HWND hwnd) {
    int len = GetWindowTextLengthW(hwnd);
    std::wstring text(static_cast<size_t>(len + 1), L'\0');
    GetWindowTextW(hwnd, text.data(), len + 1);
    text.resize(static_cast<size_t>(len));
    return text;
}

void setStatus(const std::wstring& text) {
    g_extraStatus = text;
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

std::wstring modeText() {
    switch (g_mode) {
    case Mode::Bot: return L"Mod: Bota karşı (sen Beyaz)";
    case Mode::Local: return L"Mod: 2 oyuncu / aynı PC";
    case Mode::Online: return g_myColor == Color::White ? L"Mod: Online (sen Beyaz)" : L"Mod: Online (sen Siyah)";
    default: return L"Mod: Menü";
    }
}

std::wstring pieceText(int piece) {
    switch (piece) {
    case 1: return L"♙";
    case 2: return L"♘";
    case 3: return L"♗";
    case 4: return L"♖";
    case 5: return L"♕";
    case 6: return L"♔";
    case -1: return L"♟";
    case -2: return L"♞";
    case -3: return L"♝";
    case -4: return L"♜";
    case -5: return L"♛";
    case -6: return L"♚";
    default: return L"";
    }
}

int displayToSquare(int displayRow, int displayCol) {
    int row = displayRow;
    int col = displayCol;
    if (g_flipBoard) {
        row = 7 - displayRow;
        col = 7 - displayCol;
    }
    return row * 8 + col;
}

void squareToDisplay(int square, int& displayRow, int& displayCol) {
    int row = square / 8;
    int col = square % 8;
    if (g_flipBoard) {
        displayRow = 7 - row;
        displayCol = 7 - col;
    } else {
        displayRow = row;
        displayCol = col;
    }
}

int pointToSquare(int x, int y) {
    if (x < BOARD_X || y < BOARD_Y || x >= BOARD_X + BOARD_SIZE || y >= BOARD_Y + BOARD_SIZE) return -1;
    int displayCol = (x - BOARD_X) / SQ;
    int displayRow = (y - BOARD_Y) / SQ;
    return displayToSquare(displayRow, displayCol);
}

bool canControlCurrentSide() {
    if (g_board.isGameOver()) return false;
    if (g_mode == Mode::Local) return true;
    if (g_mode == Mode::Bot) return g_board.sideToMove() == Color::White;
    if (g_mode == Mode::Online) return g_board.sideToMove() == g_myColor;
    return false;
}

bool sendLine(const std::string& line) {
    if (g_socket == INVALID_SOCKET) return false;
    std::string data = line;
    if (data.empty() || data.back() != '\n') data.push_back('\n');
    const char* ptr = data.c_str();
    int left = static_cast<int>(data.size());
    while (left > 0) {
        int sent = send(g_socket, ptr, left, 0);
        if (sent <= 0) return false;
        ptr += sent;
        left -= sent;
    }
    return true;
}

void closeNetwork() {
    g_netRunning = false;
    if (g_socket != INVALID_SOCKET) {
        shutdown(g_socket, SD_BOTH);
        closesocket(g_socket);
        g_socket = INVALID_SOCKET;
    }
}

void postNetLine(const std::string& line) {
    auto* heapLine = new std::string(line);
    PostMessageW(g_hwnd, WM_NETLINE, 0, reinterpret_cast<LPARAM>(heapLine));
}

void networkLoop() {
    std::string pending;
    char buffer[512];
    while (g_netRunning && g_socket != INVALID_SOCKET) {
        int n = recv(g_socket, buffer, sizeof(buffer), 0);
        if (n <= 0) break;
        pending.append(buffer, buffer + n);
        size_t pos = 0;
        while ((pos = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, pos);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            pending.erase(0, pos + 1);
            postNetLine(line);
        }
    }
    postNetLine("DISCONNECTED");
}

void connectOnline() {
    closeNetwork();

    std::string host = wideToUtf8(getWindowText(g_hostEdit));
    std::string portText = wideToUtf8(getWindowText(g_portEdit));
    std::string playerName = wideToUtf8(getWindowText(g_nameEdit));
    if (host.empty()) host = "127.0.0.1";
    if (portText.empty()) portText = "5555";
    if (playerName.empty()) playerName = "Player";

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        setStatus(L"Winsock başlatılamadı.");
        return;
    }

    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), portText.c_str(), &hints, &result) != 0) {
        setStatus(L"Adres çözülemedi. IP ve portu kontrol et.");
        return;
    }

    SOCKET s = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(result);
        setStatus(L"Socket oluşturulamadı.");
        return;
    }

    if (connect(s, result->ai_addr, static_cast<int>(result->ai_addrlen)) == SOCKET_ERROR) {
        closesocket(s);
        freeaddrinfo(result);
        setStatus(L"Sunucuya bağlanamadı. Linux server çalışıyor mu?");
        return;
    }
    freeaddrinfo(result);

    g_socket = s;
    g_netRunning = true;
    g_mode = Mode::Online;
    g_flipBoard = false;
    g_myColor = Color::White;
    g_selected = -1;
    g_selectedMoves.clear();
    g_board.reset();
    sendLine("HELLO " + playerName);
    setStatus(L"Bağlandı. Eşleşme bekleniyor...");

    g_netThread = std::thread(networkLoop);
    g_netThread.detach();
}

void chooseMode(Mode mode) {
    closeNetwork();
    g_mode = mode;
    g_board.reset();
    g_flipBoard = false;
    g_myColor = Color::White;
    g_selected = -1;
    g_selectedMoves.clear();
    if (mode == Mode::Bot) setStatus(L"Bot modu başladı. Sen Beyaz taşlarla oynuyorsun.");
    if (mode == Mode::Local) setStatus(L"Aynı bilgisayarda 2 oyuncu modu başladı.");
}

void maybeBotMove() {
    if (g_mode != Mode::Bot || g_board.isGameOver() || g_board.sideToMove() != Color::Black) return;
    setStatus(L"Bot düşünüyor...");
    auto best = chess::findBestBotMove(g_board, 3);
    if (best) {
        g_board.makeMove(*best);
        std::wstring uci = utf8ToWide(Board::moveToUci(*best));
        setStatus(L"Bot hamlesi: " + uci);
    }
}

Move preferQueenPromotion(const std::vector<Move>& moves, int to) {
    Move fallback;
    bool hasFallback = false;
    for (const auto& m : moves) {
        if (m.to != to) continue;
        if (!hasFallback) { fallback = m; hasFallback = true; }
        if (m.promotion == PieceType::Queen) return m;
    }
    return fallback;
}

void clearSelection() {
    g_selected = -1;
    g_selectedMoves.clear();
}

void clickSquare(int square) {
    if (square < 0 || g_mode == Mode::Menu) return;
    if (!canControlCurrentSide()) return;

    const int piece = g_board.at(square);
    const bool ownPiece = piece != 0 && Board::pieceColor(piece) == g_board.sideToMove();

    if (g_selected < 0) {
        if (ownPiece) {
            g_selected = square;
            g_selectedMoves = g_board.legalMovesFrom(square);
        }
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }

    if (ownPiece && square != g_selected) {
        g_selected = square;
        g_selectedMoves = g_board.legalMovesFrom(square);
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }

    bool canMoveThere = false;
    for (const auto& m : g_selectedMoves) {
        if (m.to == square) { canMoveThere = true; break; }
    }

    if (canMoveThere) {
        Move move = preferQueenPromotion(g_selectedMoves, square);
        std::string uci = Board::moveToUci(move);
        if (g_board.makeMove(move)) {
            clearSelection();
            if (g_mode == Mode::Online) {
                sendLine("MOVE " + uci);
                setStatus(L"Hamle gönderildi: " + utf8ToWide(uci));
            } else {
                setStatus(utf8ToWide(g_board.statusText()));
                maybeBotMove();
            }
        }
    } else {
        clearSelection();
    }
    InvalidateRect(g_hwnd, nullptr, FALSE);
}

void drawRoundedPanel(HDC hdc) {
    RECT panel{PANEL_X - 15, 25, WINDOW_W - 25, WINDOW_H - 45};
    HBRUSH brush = CreateSolidBrush(RGB(32, 36, 44));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(55, 60, 72));
    HGDIOBJ oldBrush = SelectObject(hdc, brush);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, panel.left, panel.top, panel.right, panel.bottom, 22, 22);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void drawTextBlock(HDC hdc, const std::wstring& text, RECT rect, HFONT font, COLORREF color, UINT format = DT_LEFT | DT_WORDBREAK) {
    HGDIOBJ oldFont = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    DrawTextW(hdc, text.c_str(), -1, &rect, format);
    SelectObject(hdc, oldFont);
}

void drawBoard(HDC hdc) {
    HBRUSH bg = CreateSolidBrush(RGB(18, 20, 26));
    RECT full{0, 0, WINDOW_W, WINDOW_H};
    FillRect(hdc, &full, bg);
    DeleteObject(bg);

    HPEN outerPen = CreatePen(PS_SOLID, 4, RGB(20, 22, 26));
    HGDIOBJ oldPen = SelectObject(hdc, outerPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, BOARD_X - 3, BOARD_Y - 3, BOARD_X + BOARD_SIZE + 3, BOARD_Y + BOARD_SIZE + 3);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(outerPen);

    for (int dr = 0; dr < 8; ++dr) {
        for (int dc = 0; dc < 8; ++dc) {
            int square = displayToSquare(dr, dc);
            bool light = ((dr + dc) % 2) == 0;
            COLORREF color = light ? RGB(238, 238, 210) : RGB(118, 150, 86);
            if (square == g_selected) color = RGB(246, 246, 105);
            HBRUSH brush = CreateSolidBrush(color);
            RECT rc{BOARD_X + dc * SQ, BOARD_Y + dr * SQ, BOARD_X + (dc + 1) * SQ, BOARD_Y + (dr + 1) * SQ};
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);
        }
    }

    // Legal hamle noktaları.
    for (const auto& m : g_selectedMoves) {
        int dr = 0, dc = 0;
        squareToDisplay(m.to, dr, dc);
        int cx = BOARD_X + dc * SQ + SQ / 2;
        int cy = BOARD_Y + dr * SQ + SQ / 2;
        HBRUSH dot = CreateSolidBrush(g_board.at(m.to) == 0 ? RGB(55, 80, 55) : RGB(150, 45, 45));
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(40, 40, 40));
        HGDIOBJ oldB = SelectObject(hdc, dot);
        HGDIOBJ oldP = SelectObject(hdc, pen);
        int radius = g_board.at(m.to) == 0 ? 9 : 17;
        Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
        SelectObject(hdc, oldB);
        SelectObject(hdc, oldP);
        DeleteObject(dot);
        DeleteObject(pen);
    }

    HGDIOBJ oldFont = SelectObject(hdc, g_pieceFont);
    SetBkMode(hdc, TRANSPARENT);
    for (int sq = 0; sq < 64; ++sq) {
        int piece = g_board.at(sq);
        if (piece == 0) continue;
        int dr = 0, dc = 0;
        squareToDisplay(sq, dr, dc);
        RECT rc{BOARD_X + dc * SQ, BOARD_Y + dr * SQ - 3, BOARD_X + (dc + 1) * SQ, BOARD_Y + (dr + 1) * SQ + 3};
        std::wstring text = pieceText(piece);

        // Hafif gölge.
        RECT shadow = rc;
        OffsetRect(&shadow, 2, 2);
        SetTextColor(hdc, RGB(50, 50, 50));
        DrawTextW(hdc, text.c_str(), -1, &shadow, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SetTextColor(hdc, piece > 0 ? RGB(250, 250, 240) : RGB(18, 18, 18));
        DrawTextW(hdc, text.c_str(), -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    SelectObject(hdc, oldFont);

    // Koordinatlar.
    SelectObject(hdc, g_textFont);
    SetTextColor(hdc, RGB(210, 214, 220));
    SetBkMode(hdc, TRANSPARENT);
    for (int i = 0; i < 8; ++i) {
        wchar_t file = static_cast<wchar_t>((g_flipBoard ? L'h' : L'a') + (g_flipBoard ? -i : i));
        wchar_t rank = static_cast<wchar_t>((g_flipBoard ? L'1' : L'8') + (g_flipBoard ? i : -i));
        RECT fileRc{BOARD_X + i * SQ, BOARD_Y + BOARD_SIZE + 5, BOARD_X + (i + 1) * SQ, BOARD_Y + BOARD_SIZE + 25};
        std::wstring fs(1, file);
        DrawTextW(hdc, fs.c_str(), -1, &fileRc, DT_CENTER | DT_SINGLELINE);
        RECT rankRc{BOARD_X - 23, BOARD_Y + i * SQ + 25, BOARD_X - 5, BOARD_Y + (i + 1) * SQ};
        std::wstring rs(1, rank);
        DrawTextW(hdc, rs.c_str(), -1, &rankRc, DT_CENTER | DT_SINGLELINE);
    }
}

void drawPanel(HDC hdc) {
    drawRoundedPanel(hdc);
    RECT title{PANEL_X, 40, WINDOW_W - 45, 70};
    drawTextBlock(hdc, L"Online Satranç", title, g_titleFont, RGB(245, 245, 245), DT_CENTER | DT_SINGLELINE);

    RECT modeRc{PANEL_X, 170, WINDOW_W - 45, 220};
    drawTextBlock(hdc, modeText(), modeRc, g_textFont, RGB(235, 238, 244));

    RECT statusRc{PANEL_X, 215, WINDOW_W - 45, 310};
    std::wstring boardStatus = utf8ToWide(g_board.statusText());
    drawTextBlock(hdc, L"Durum: " + boardStatus + L"\n" + g_extraStatus, statusRc, g_textFont, RGB(225, 228, 235));

    RECT hostLabel{PANEL_X, 372, WINDOW_W - 45, 396};
    drawTextBlock(hdc, L"Online bağlantı", hostLabel, g_textFont, RGB(245, 245, 245), DT_LEFT | DT_SINGLELINE);

    RECT help{PANEL_X, 520, WINDOW_W - 45, 665};
    drawTextBlock(hdc,
        L"Nasıl oynanır:\n"
        L"• Taşa tıkla, sonra hedef kareye tıkla.\n"
        L"• Piyon son sıraya gelince otomatik vezir olur.\n"
        L"• Rok, şah tehdidi ve geçiş kareleri kontrol edilerek yapılır.\n"
        L"• Online için Linux server IP/port girip bağlan.",
        help, g_textFont, RGB(205, 210, 220));
}

void createControls(HWND hwnd) {
    g_titleFont = CreateFontW(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_textFont = CreateFontW(17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_pieceFont = CreateFontW(58, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_SWISS, L"Segoe UI Symbol");

    CreateWindowW(L"BUTTON", L"Bota Karşı", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PANEL_X, 90, 190, 32, hwnd, reinterpret_cast<HMENU>(ID_BOT), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"2 Oyuncu", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PANEL_X, 128, 190, 32, hwnd, reinterpret_cast<HMENU>(ID_LOCAL), nullptr, nullptr);
    CreateWindowW(L"BUTTON", L"Yeniden Başlat", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PANEL_X, 325, 190, 32, hwnd, reinterpret_cast<HMENU>(ID_RESET), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"IP:", WS_CHILD | WS_VISIBLE,
                  PANEL_X, 400, 45, 22, hwnd, nullptr, nullptr, nullptr);
    g_hostEdit = CreateWindowW(L"EDIT", L"127.0.0.1", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               PANEL_X + 45, 397, 145, 26, hwnd, reinterpret_cast<HMENU>(ID_HOST), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Port:", WS_CHILD | WS_VISIBLE,
                  PANEL_X, 430, 45, 22, hwnd, nullptr, nullptr, nullptr);
    g_portEdit = CreateWindowW(L"EDIT", L"5555", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               PANEL_X + 45, 427, 145, 26, hwnd, reinterpret_cast<HMENU>(ID_PORT), nullptr, nullptr);

    CreateWindowW(L"STATIC", L"Ad:", WS_CHILD | WS_VISIBLE,
                  PANEL_X, 460, 45, 22, hwnd, nullptr, nullptr, nullptr);
    g_nameEdit = CreateWindowW(L"EDIT", L"Player", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
                               PANEL_X + 45, 457, 145, 26, hwnd, reinterpret_cast<HMENU>(ID_NAME), nullptr, nullptr);

    CreateWindowW(L"BUTTON", L"Online Bağlan", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  PANEL_X, 488, 190, 32, hwnd, reinterpret_cast<HMENU>(ID_ONLINE), nullptr, nullptr);

    SendMessageW(g_hostEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
    SendMessageW(g_portEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
    SendMessageW(g_nameEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_textFont), TRUE);
}

void handleNetworkLine(const std::string& line) {
    if (line == "DISCONNECTED") {
        if (g_mode == Mode::Online) setStatus(L"Bağlantı kesildi.");
        return;
    }
    if (line.rfind("COLOR ", 0) == 0) {
        char color = line.size() >= 7 ? line[6] : 'W';
        g_myColor = (color == 'B') ? Color::Black : Color::White;
        g_flipBoard = g_myColor == Color::Black;
        setStatus(g_myColor == Color::White ? L"Renginiz: Beyaz" : L"Renginiz: Siyah");
        return;
    }
    if (line == "START") {
        g_board.reset();
        clearSelection();
        setStatus(L"Online maç başladı.");
        return;
    }
    if (line.rfind("MOVE ", 0) == 0) {
        std::string uci = line.substr(5);
        if (g_board.makeMoveUci(uci)) {
            clearSelection();
            setStatus(L"Rakip hamlesi: " + utf8ToWide(uci));
        } else {
            setStatus(L"Sunucudan geçersiz hamle geldi: " + utf8ToWide(uci));
        }
        return;
    }
    if (line.rfind("OK", 0) == 0) {
        setStatus(L"Hamle onaylandı.");
        return;
    }
    if (line.rfind("ERR ", 0) == 0) {
        setStatus(L"Sunucu hatası: " + utf8ToWide(line.substr(4)));
        return;
    }
    if (line.rfind("MSG ", 0) == 0) {
        setStatus(utf8ToWide(line.substr(4)));
        return;
    }
    setStatus(L"Sunucu: " + utf8ToWide(line));
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        g_hwnd = hwnd;
        createControls(hwnd);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_BOT: chooseMode(Mode::Bot); return 0;
        case ID_LOCAL: chooseMode(Mode::Local); return 0;
        case ID_RESET:
            if (g_mode == Mode::Online) {
                setStatus(L"Online oyunda yeniden başlatmak için tekrar bağlan.");
            } else if (g_mode == Mode::Bot || g_mode == Mode::Local) {
                chooseMode(g_mode);
            }
            return 0;
        case ID_ONLINE: connectOnline(); return 0;
        default: break;
        }
        break;

    case WM_LBUTTONDOWN: {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        clickSquare(pointToSquare(x, y));
        return 0;
    }

    case WM_NETLINE: {
        auto* line = reinterpret_cast<std::string*>(lParam);
        if (line) {
            handleNetworkLine(*line);
            delete line;
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, WINDOW_W, WINDOW_H);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        drawBoard(mem);
        drawPanel(mem);
        BitBlt(hdc, 0, 0, WINDOW_W, WINDOW_H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        closeNetwork();
        if (g_pieceFont) DeleteObject(g_pieceFont);
        if (g_textFont) DeleteObject(g_textFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        WSACleanup();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDPIAware();

    const wchar_t CLASS_NAME[] = L"OnlineChessCppWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.style = CS_HREDRAW | CS_VREDRAW;

    RegisterClassW(&wc);

    RECT rc{0, 0, WINDOW_W, WINDOW_H};
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND hwnd = CreateWindowExW(
        0,
        CLASS_NAME,
        L"C++ Online Satranç",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (!hwnd) return 0;
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
