#include "Chess.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>

namespace chess {

namespace {
constexpr int WP = 1;
constexpr int WN = 2;
constexpr int WB = 3;
constexpr int WR = 4;
constexpr int WQ = 5;
constexpr int WK = 6;
constexpr int BP = -1;
constexpr int BN = -2;
constexpr int BB = -3;
constexpr int BR = -4;
constexpr int BQ = -5;
constexpr int BK = -6;

int idx(int row, int col) { return row * 8 + col; }
int rowOf(int square) { return square / 8; }
int colOf(int square) { return square % 8; }

char promotionChar(PieceType type) {
    switch (type) {
    case PieceType::Queen: return 'q';
    case PieceType::Rook: return 'r';
    case PieceType::Bishop: return 'b';
    case PieceType::Knight: return 'n';
    default: return '\0';
    }
}

PieceType promotionType(char c) {
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(c)))) {
    case 'q': return PieceType::Queen;
    case 'r': return PieceType::Rook;
    case 'b': return PieceType::Bishop;
    case 'n': return PieceType::Knight;
    default: return PieceType::None;
    }
}

int signedPiece(Color color, PieceType type) {
    return Board::colorSign(color) * static_cast<int>(type);
}

} // namespace

Board::Board() { reset(); }

void Board::reset() {
    squares_.fill(0);
    squares_[0] = BR; squares_[1] = BN; squares_[2] = BB; squares_[3] = BQ;
    squares_[4] = BK; squares_[5] = BB; squares_[6] = BN; squares_[7] = BR;
    for (int i = 8; i < 16; ++i) squares_[i] = BP;
    for (int i = 48; i < 56; ++i) squares_[i] = WP;
    squares_[56] = WR; squares_[57] = WN; squares_[58] = WB; squares_[59] = WQ;
    squares_[60] = WK; squares_[61] = WB; squares_[62] = WN; squares_[63] = WR;

    side_ = Color::White;
    whiteCastleKing_ = whiteCastleQueen_ = blackCastleKing_ = blackCastleQueen_ = true;
    enPassantSquare_ = -1;
}

int Board::at(int square) const {
    if (square < 0 || square >= 64) return 0;
    return squares_[square];
}

std::string Board::squareName(int square) {
    if (square < 0 || square >= 64) return "??";
    std::string s;
    s.push_back(static_cast<char>('a' + colOf(square)));
    s.push_back(static_cast<char>('8' - rowOf(square)));
    return s;
}

int Board::parseSquare(const std::string& text) {
    if (text.size() != 2) return -1;
    const char file = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    const char rank = text[1];
    if (file < 'a' || file > 'h' || rank < '1' || rank > '8') return -1;
    const int col = file - 'a';
    const int row = '8' - rank;
    return idx(row, col);
}

std::string Board::moveToUci(const Move& move) {
    std::string out = squareName(move.from) + squareName(move.to);
    const char p = promotionChar(move.promotion);
    if (p) out.push_back(p);
    return out;
}

std::optional<Move> Board::moveFromUci(const std::string& uci) {
    if (uci.size() < 4) return std::nullopt;
    Move m;
    m.from = parseSquare(uci.substr(0, 2));
    m.to = parseSquare(uci.substr(2, 2));
    if (m.from < 0 || m.to < 0) return std::nullopt;
    if (uci.size() >= 5) m.promotion = promotionType(uci[4]);
    return m;
}

std::vector<Move> Board::pseudoLegalMoves() const {
    std::vector<Move> moves;
    moves.reserve(64);
    for (int sq = 0; sq < 64; ++sq) {
        const int piece = squares_[sq];
        if (piece == 0 || pieceColor(piece) != side_) continue;

        switch (pieceType(piece)) {
        case PieceType::Pawn: addPawnMoves(moves, sq); break;
        case PieceType::Knight: addKnightMoves(moves, sq); break;
        case PieceType::Bishop: addSlidingMoves(moves, sq, {{-1,-1},{-1,1},{1,-1},{1,1}}); break;
        case PieceType::Rook: addSlidingMoves(moves, sq, {{-1,0},{1,0},{0,-1},{0,1}}); break;
        case PieceType::Queen: addSlidingMoves(moves, sq, {{-1,-1},{-1,1},{1,-1},{1,1},{-1,0},{1,0},{0,-1},{0,1}}); break;
        case PieceType::King: addKingMoves(moves, sq); break;
        default: break;
        }
    }
    return moves;
}

void Board::addPawnMoves(std::vector<Move>& moves, int square) const {
    const int piece = squares_[square];
    const Color color = pieceColor(piece);
    const int dir = color == Color::White ? -1 : 1;
    const int startRow = color == Color::White ? 6 : 1;
    const int promoteRow = color == Color::White ? 0 : 7;
    const int r = rowOf(square);
    const int c = colOf(square);

    auto addPromotionOrNormal = [&](int to) {
        if (rowOf(to) == promoteRow) {
            moves.push_back({square, to, PieceType::Queen});
            moves.push_back({square, to, PieceType::Rook});
            moves.push_back({square, to, PieceType::Bishop});
            moves.push_back({square, to, PieceType::Knight});
        } else {
            moves.push_back({square, to, PieceType::None});
        }
    };

    const int oneRow = r + dir;
    if (isInside(oneRow, c) && squares_[idx(oneRow, c)] == 0) {
        addPromotionOrNormal(idx(oneRow, c));
        const int twoRow = r + 2 * dir;
        if (r == startRow && isInside(twoRow, c) && squares_[idx(twoRow, c)] == 0) {
            moves.push_back({square, idx(twoRow, c), PieceType::None});
        }
    }

    for (int dc : {-1, 1}) {
        const int nr = r + dir;
        const int nc = c + dc;
        if (!isInside(nr, nc)) continue;
        const int to = idx(nr, nc);
        const int target = squares_[to];
        if (target != 0 && pieceColor(target) != color) {
            addPromotionOrNormal(to);
        }
        if (to == enPassantSquare_) {
            moves.push_back({square, to, PieceType::None});
        }
    }
}

void Board::addKnightMoves(std::vector<Move>& moves, int square) const {
    const int piece = squares_[square];
    const Color color = pieceColor(piece);
    const int r = rowOf(square);
    const int c = colOf(square);
    constexpr int offsets[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (const auto& off : offsets) {
        const int nr = r + off[0];
        const int nc = c + off[1];
        if (!isInside(nr, nc)) continue;
        const int to = idx(nr, nc);
        if (squares_[to] == 0 || pieceColor(squares_[to]) != color) {
            moves.push_back({square, to, PieceType::None});
        }
    }
}

void Board::addSlidingMoves(std::vector<Move>& moves, int square, const std::vector<std::pair<int, int>>& dirs) const {
    const int piece = squares_[square];
    const Color color = pieceColor(piece);
    const int r = rowOf(square);
    const int c = colOf(square);
    for (const auto& [dr, dc] : dirs) {
        int nr = r + dr;
        int nc = c + dc;
        while (isInside(nr, nc)) {
            const int to = idx(nr, nc);
            if (squares_[to] == 0) {
                moves.push_back({square, to, PieceType::None});
            } else {
                if (pieceColor(squares_[to]) != color) moves.push_back({square, to, PieceType::None});
                break;
            }
            nr += dr;
            nc += dc;
        }
    }
}

void Board::addKingMoves(std::vector<Move>& moves, int square) const {
    const int piece = squares_[square];
    const Color color = pieceColor(piece);
    const int r = rowOf(square);
    const int c = colOf(square);

    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            const int nr = r + dr;
            const int nc = c + dc;
            if (!isInside(nr, nc)) continue;
            const int to = idx(nr, nc);
            if (squares_[to] == 0 || pieceColor(squares_[to]) != color) {
                moves.push_back({square, to, PieceType::None});
            }
        }
    }

    // Rok: şah tehdit altındaysa, geçeceği kareler tehdit altındaysa veya arada taş varsa yasaktır.
    if (inCheck(color)) return;
    const Color enemy = opposite(color);
    if (color == Color::White && square == 60) {
        if (whiteCastleKing_ && squares_[61] == 0 && squares_[62] == 0 &&
            !isSquareAttacked(61, enemy) && !isSquareAttacked(62, enemy)) {
            moves.push_back({60, 62, PieceType::None});
        }
        if (whiteCastleQueen_ && squares_[59] == 0 && squares_[58] == 0 && squares_[57] == 0 &&
            !isSquareAttacked(59, enemy) && !isSquareAttacked(58, enemy)) {
            moves.push_back({60, 58, PieceType::None});
        }
    }
    if (color == Color::Black && square == 4) {
        if (blackCastleKing_ && squares_[5] == 0 && squares_[6] == 0 &&
            !isSquareAttacked(5, enemy) && !isSquareAttacked(6, enemy)) {
            moves.push_back({4, 6, PieceType::None});
        }
        if (blackCastleQueen_ && squares_[3] == 0 && squares_[2] == 0 && squares_[1] == 0 &&
            !isSquareAttacked(3, enemy) && !isSquareAttacked(2, enemy)) {
            moves.push_back({4, 2, PieceType::None});
        }
    }
}

bool Board::isSquareAttacked(int square, Color byColor) const {
    const int r = rowOf(square);
    const int c = colOf(square);

    // Piyon saldırıları: saldıran piyonun bulunabileceği karelere bakılır.
    const int pawnRow = byColor == Color::White ? r + 1 : r - 1;
    for (int dc : {-1, 1}) {
        const int pc = c + dc;
        if (isInside(pawnRow, pc) && squares_[idx(pawnRow, pc)] == signedPiece(byColor, PieceType::Pawn)) {
            return true;
        }
    }

    constexpr int knightOffsets[8][2] = {{-2,-1},{-2,1},{-1,-2},{-1,2},{1,-2},{1,2},{2,-1},{2,1}};
    for (const auto& off : knightOffsets) {
        const int nr = r + off[0];
        const int nc = c + off[1];
        if (isInside(nr, nc) && squares_[idx(nr, nc)] == signedPiece(byColor, PieceType::Knight)) return true;
    }

    const std::vector<std::pair<int, int>> bishopDirs = {{-1,-1},{-1,1},{1,-1},{1,1}};
    const std::vector<std::pair<int, int>> rookDirs = {{-1,0},{1,0},{0,-1},{0,1}};
    auto scan = [&](const std::vector<std::pair<int, int>>& dirs, PieceType a, PieceType b) {
        for (const auto& [dr, dc] : dirs) {
            int nr = r + dr;
            int nc = c + dc;
            while (isInside(nr, nc)) {
                const int p = squares_[idx(nr, nc)];
                if (p != 0) {
                    if (pieceColor(p) == byColor && (pieceType(p) == a || pieceType(p) == b)) return true;
                    break;
                }
                nr += dr;
                nc += dc;
            }
        }
        return false;
    };
    if (scan(bishopDirs, PieceType::Bishop, PieceType::Queen)) return true;
    if (scan(rookDirs, PieceType::Rook, PieceType::Queen)) return true;

    for (int dr = -1; dr <= 1; ++dr) {
        for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;
            const int nr = r + dr;
            const int nc = c + dc;
            if (isInside(nr, nc) && squares_[idx(nr, nc)] == signedPiece(byColor, PieceType::King)) return true;
        }
    }

    return false;
}

bool Board::inCheck(Color color) const {
    const int king = signedPiece(color, PieceType::King);
    auto it = std::find(squares_.begin(), squares_.end(), king);
    if (it == squares_.end()) return true;
    const int kingSquare = static_cast<int>(std::distance(squares_.begin(), it));
    return isSquareAttacked(kingSquare, opposite(color));
}

std::vector<Move> Board::legalMoves() const {
    std::vector<Move> legal;
    const auto pseudo = pseudoLegalMoves();
    legal.reserve(pseudo.size());
    const Color moving = side_;
    for (const Move& m : pseudo) {
        Board copy = *this;
        copy.makeMoveUnchecked(m);
        if (!copy.inCheck(moving)) legal.push_back(m);
    }
    return legal;
}

std::vector<Move> Board::legalMovesFrom(int from) const {
    std::vector<Move> out;
    for (const Move& m : legalMoves()) {
        if (m.from == from) out.push_back(m);
    }
    return out;
}

bool Board::sameMoveIgnoringAutoQueen(const Move& a, const Move& b) const {
    if (a.from != b.from || a.to != b.to) return false;
    if (a.promotion == b.promotion) return true;
    const int piece = squares_[a.from];
    if (pieceType(piece) == PieceType::Pawn) {
        const int lastRow = pieceColor(piece) == Color::White ? 0 : 7;
        if (rowOf(a.to) == lastRow) {
            return (a.promotion == PieceType::Queen && b.promotion == PieceType::None) ||
                   (a.promotion == PieceType::None && b.promotion == PieceType::Queen);
        }
    }
    return false;
}

bool Board::makeMove(const Move& move) {
    for (const Move& legal : legalMoves()) {
        if (sameMoveIgnoringAutoQueen(legal, move)) {
            Move actual = legal;
            if (actual.promotion == PieceType::None && move.promotion != PieceType::None) actual.promotion = move.promotion;
            makeMoveUnchecked(actual);
            return true;
        }
    }
    return false;
}

bool Board::makeMoveUci(const std::string& uci) {
    auto move = moveFromUci(uci);
    if (!move) return false;
    return makeMove(*move);
}

void Board::makeMoveUnchecked(const Move& move) {
    const int movingPiece = squares_[move.from];
    const Color movingColor = pieceColor(movingPiece);
    const PieceType movingType = pieceType(movingPiece);
    const int oldEnPassant = enPassantSquare_;

    // Kale hakları: şah, kale veya ilk karedeki kalenin yenmesi hakları kapatır.
    if (movingType == PieceType::King) {
        if (movingColor == Color::White) { whiteCastleKing_ = false; whiteCastleQueen_ = false; }
        else { blackCastleKing_ = false; blackCastleQueen_ = false; }
    }
    if (move.from == 63 || move.to == 63) whiteCastleKing_ = false;
    if (move.from == 56 || move.to == 56) whiteCastleQueen_ = false;
    if (move.from == 7 || move.to == 7) blackCastleKing_ = false;
    if (move.from == 0 || move.to == 0) blackCastleQueen_ = false;

    squares_[move.from] = 0;

    // Geçerken alma.
    if (movingType == PieceType::Pawn && move.to == oldEnPassant && squares_[move.to] == 0) {
        const int capturedPawnSquare = movingColor == Color::White ? move.to + 8 : move.to - 8;
        if (capturedPawnSquare >= 0 && capturedPawnSquare < 64) squares_[capturedPawnSquare] = 0;
    }

    int finalPiece = movingPiece;
    const int targetRow = rowOf(move.to);
    if (movingType == PieceType::Pawn && (targetRow == 0 || targetRow == 7)) {
        PieceType promo = move.promotion == PieceType::None ? PieceType::Queen : move.promotion;
        finalPiece = signedPiece(movingColor, promo);
    }
    squares_[move.to] = finalPiece;

    // Rokta kaleyi de hareket ettir.
    if (movingType == PieceType::King && std::abs(move.to - move.from) == 2) {
        if (move.to == 62) { squares_[61] = squares_[63]; squares_[63] = 0; }
        if (move.to == 58) { squares_[59] = squares_[56]; squares_[56] = 0; }
        if (move.to == 6)  { squares_[5] = squares_[7];   squares_[7] = 0; }
        if (move.to == 2)  { squares_[3] = squares_[0];   squares_[0] = 0; }
    }

    enPassantSquare_ = -1;
    if (movingType == PieceType::Pawn && std::abs(move.to - move.from) == 16) {
        enPassantSquare_ = (move.from + move.to) / 2;
    }
    side_ = opposite(side_);
}

bool Board::isCheckmate() const {
    return inCheck(side_) && legalMoves().empty();
}

bool Board::isStalemate() const {
    return !inCheck(side_) && legalMoves().empty();
}

std::string Board::statusText() const {
    if (isCheckmate()) {
        return std::string("Şah mat! ") + (side_ == Color::White ? "Siyah" : "Beyaz") + " kazandı.";
    }
    if (isStalemate()) return "Pat! Oyun berabere.";
    std::string side = side_ == Color::White ? "Beyaz" : "Siyah";
    if (inCheck(side_)) return side + " şah altında.";
    return side + " oynar.";
}

std::string Board::fenLike() const {
    std::ostringstream ss;
    for (int r = 0; r < 8; ++r) {
        int empty = 0;
        for (int c = 0; c < 8; ++c) {
            const int p = squares_[idx(r, c)];
            if (p == 0) { ++empty; continue; }
            if (empty) { ss << empty; empty = 0; }
            const bool white = p > 0;
            char ch = '?';
            switch (pieceType(p)) {
            case PieceType::Pawn: ch = 'p'; break;
            case PieceType::Knight: ch = 'n'; break;
            case PieceType::Bishop: ch = 'b'; break;
            case PieceType::Rook: ch = 'r'; break;
            case PieceType::Queen: ch = 'q'; break;
            case PieceType::King: ch = 'k'; break;
            default: break;
            }
            ss << (white ? static_cast<char>(std::toupper(ch)) : ch);
        }
        if (empty) ss << empty;
        if (r != 7) ss << '/';
    }
    ss << ' ' << (side_ == Color::White ? 'w' : 'b');
    return ss.str();
}

int materialValue(PieceType type) {
    switch (type) {
    case PieceType::Pawn: return 100;
    case PieceType::Knight: return 320;
    case PieceType::Bishop: return 330;
    case PieceType::Rook: return 500;
    case PieceType::Queen: return 900;
    case PieceType::King: return 0;
    default: return 0;
    }
}

int evaluateMaterial(const Board& board) {
    int score = 0;
    for (int p : board.squares()) {
        if (p == 0) continue;
        const int value = materialValue(Board::pieceType(p));
        score += p > 0 ? value : -value;
    }
    return score;
}

namespace {
int negamax(Board board, int depth, int alpha, int beta) {
    const int sideFactor = board.sideToMove() == Color::White ? 1 : -1;
    if (board.isCheckmate()) return -100000 - depth;
    if (board.isStalemate()) return 0;
    if (depth == 0) return sideFactor * evaluateMaterial(board);

    auto moves = board.legalMoves();
    // Basit hamle sıralama: yeme hamleleri önce.
    std::sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b) {
        return board.at(a.to) != 0 && board.at(b.to) == 0;
    });

    int best = std::numeric_limits<int>::min() / 2;
    for (const Move& m : moves) {
        Board next = board;
        next.makeMove(m);
        int score = -negamax(next, depth - 1, -beta, -alpha);
        best = std::max(best, score);
        alpha = std::max(alpha, score);
        if (alpha >= beta) break;
    }
    return best;
}
} // namespace

std::optional<Move> findBestBotMove(const Board& board, int depth) {
    auto moves = board.legalMoves();
    if (moves.empty()) return std::nullopt;

    Move bestMove = moves.front();
    int bestScore = std::numeric_limits<int>::min() / 2;
    for (const Move& m : moves) {
        Board next = board;
        next.makeMove(m);
        int score = -negamax(next, depth - 1, -1000000, 1000000);
        if (score > bestScore) {
            bestScore = score;
            bestMove = m;
        }
    }
    return bestMove;
}

} // namespace chess
