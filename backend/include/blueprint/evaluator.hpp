#pragma once
#include <compare>

#include "blueprint/cards.hpp"

namespace blueprint
{
    enum class HandCategory : std::uint8_t
    {
        high_card,
        pair,
        two_pair,
        three_of_a_kind,
        straight,
        flush,
        full_house,
        four_of_a_kind,
        straight_flush
    };

    // Higher values win; equal values split. Suit never breaks a tie.
    struct HandValue
    {
        std::uint32_t value{};

        HandCategory category() const { return static_cast<HandCategory>(value >> 20); }

        auto operator<=>(const HandValue&) const = default;
    };

    // Best five from exactly 5, 6 or 7 distinct cards, with the milestone-1 card IDs.
    HandValue evaluate_hand(std::span<const Card> cards);
}    // namespace blueprint
