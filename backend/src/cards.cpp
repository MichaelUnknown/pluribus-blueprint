#include "blueprint/cards.hpp"

#include <algorithm>
#include <stdexcept>

namespace blueprint
{
    Card parse_card(std::string_view text)
    {
        constexpr std::string_view ranks = "23456789TJQKA", suits = "cdhs";
        if (text.size() != 2 || ranks.find(text[0]) == ranks.npos || suits.find(text[1]) == suits.npos)
            throw std::invalid_argument("card must use rank 23456789TJQKA and suit cdhs, e.g. Ac");
        return static_cast<Card>(13 * suits.find(text[1]) + ranks.find(text[0]));
    }

    std::size_t board_size(Street street)
    {
        switch (street)
        {
            case Street::preflop:
                return 0;
            case Street::flop:
                return 3;
            case Street::turn:
                return 4;
            case Street::river:
                return 5;
        }
        throw std::invalid_argument("invalid street");
    }

    void validate_cards(std::array<Card, 2> hand, std::span<const Card> board)
    {
        std::uint64_t seen = 0;
        auto add = [&](Card c) {
            if (c >= 52 || (seen & (std::uint64_t{1} << c)))
                throw std::invalid_argument("duplicate or out-of-range card");
            seen |= std::uint64_t{1} << c;
        };
        for (auto c : hand)
            add(c);
        for (auto c : board)
            add(c);
    }

    CanonicalCards canonicalize(std::array<Card, 2> hand, std::span<const Card> board)
    {
        if (board.size() < 3 || board.size() > 5)
            throw std::invalid_argument("canonical board must have 3, 4 or 5 cards");
        validate_cards(hand, board);
        std::array<std::uint8_t, 4> perm{0, 1, 2, 3};
        std::array<std::uint64_t, 24> keys{};
        std::size_t count = 0;
        do
        {
            auto map = [&](Card c) { return static_cast<Card>(13 * perm[c / 13] + c % 13); };
            std::array<Card, 2> h{map(hand[0]), map(hand[1])};
            std::array<Card, 5> b{};
            // Sort the fixed array; unused sentinels remain after every real card.
            // Fixed bounds also avoid GCC's small-array std::sort false positives.
            for (std::size_t i = 0; i < b.size(); ++i)
                b[i] = i < board.size() ? map(board[i]) : Card{255};
            std::sort(h.begin(), h.end());
            std::sort(b.begin(), b.end());
            std::uint64_t key = 0;
            for (auto c : h)
                key = (key << 6) | c;
            for (std::size_t i = 0; i < board.size(); ++i)
                key = (key << 6) | b[i];
            keys[count++] = key;
        } while (std::next_permutation(perm.begin(), perm.end()));
        std::sort(keys.begin(), keys.end());
        return {keys[0], static_cast<std::uint8_t>(std::unique(keys.begin(), keys.end()) - keys.begin())};
    }

    std::uint16_t preflop_class(std::array<Card, 2> hand)
    {
        validate_cards(hand, {});
        auto l = std::min(hand[0] % 13, hand[1] % 13), h = std::max(hand[0] % 13, hand[1] % 13);
        if (l == h)
            return static_cast<std::uint16_t>(l);
        return static_cast<std::uint16_t>((hand[0] / 13 == hand[1] / 13 ? 13 : 91) + h * (h - 1) / 2 + l);
    }
}    // namespace blueprint
