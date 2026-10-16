#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace blueprint
{
    using Card = std::uint8_t;
    enum class Street : std::uint8_t
    {
        preflop,
        flop,
        turn,
        river
    };

    struct CanonicalCards
    {
        std::uint64_t key;
        std::uint8_t orbit_weight;
    };

    Card parse_card(std::string_view text);
    std::size_t board_size(Street street);
    void validate_cards(std::array<Card, 2> hand, std::span<const Card> board);
    CanonicalCards canonicalize(std::array<Card, 2> hand, std::span<const Card> board);
    // preflop-169-v1: pairs r; suited 13+p; offsuit 91+p; p=h*(h-1)/2+l.
    std::uint16_t preflop_class(std::array<Card, 2> hand);
}    // namespace blueprint
