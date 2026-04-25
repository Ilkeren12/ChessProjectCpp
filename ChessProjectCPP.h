#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace chess {

enum class Color { White, Black };
enum class PieceType { None = 0, Pawn = 1, Knight = 2, Bishop = 3, Rook = 4, Queen = 5, King = 6 };

struct Move {
    int from = -1;
    int to = -1;
    PieceType promotion = PieceType::None;

    bool operator==(const Move& other) const {
        return from == other.from && to == other.to && promotion == other.promotion;
    }
};

class Board {
public:
    Board();

    void reset();
    Color sideToMove() const { return side_; }
    const std::array<int, 64>& squares() const { return squares_; }
    int at(int square) const;

    std::vector<Move> legalMoves() const;
    std::vector<Move> legalMovesFrom(int from) const;
    bool makeMove(const Move& move);
    bool makeMoveUci(const std::string& uci);

    bool inCheck(Color color) const;
    bool isCheckmate() const;
    bool isStalemate() const;
    bool isGameOver() const { return isCheckmate() || isStalemate(); }

    std::string statusText() const;
    std::string fenLike() const;

    static Color opposite(Color color) { return color == Color::White ? Color::Black : Color::White; }
    static int colorSign(Color color) { return color == Color::White ? 1 : -1; }
    static Color pieceColor(int piece) { return piece > 0 ? Color::White : Color::Black; }
    static PieceType pieceType(int piece) { return static_cast<PieceType>(piece < 0 ? -piece : piece); }

    static bool isInside(int row, int col) { return row >= 0 && row < 8 && col >= 0 && col < 8; }
    static std::string squareName(int square);
    static int parseSquare(const std::string& text);
    static std::string moveToUci(const Move& move);
    static std::optional<Move> moveFromUci(const std::string& uci);

private:
    std::array<int, 64> squares_{};
    Color side_ = Color::White;
    bool whiteCastleKing_ = true;
    bool whiteCastleQueen_ = true;
    bool blackCastleKing_ = true;
    bool blackCastleQueen_ = true;
    int enPassantSquare_ = -1;

    std::vector<Move> pseudoLegalMoves() const;
    void addPawnMoves(std::vector<Move>& moves, int square) const;
    void addKnightMoves(std::vector<Move>& moves, int square) const;
    void addSlidingMoves(std::vector<Move>& moves, int square, const std::vector<std::pair<int, int>>& dirs) const;
    void addKingMoves(std::vector<Move>& moves, int square) const;

    bool isSquareAttacked(int square, Color byColor) const;
    void makeMoveUnchecked(const Move& move);
    bool sameMoveIgnoringAutoQueen(const Move& a, const Move& b) const;
};

int materialValue(PieceType type);
int evaluateMaterial(const Board& board);
std::optional<Move> findBestBotMove(const Board& board, int depth = 3);

} // namespace chess
