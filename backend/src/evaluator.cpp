#include "blueprint/evaluator.hpp"

#include <array>
#include <stdexcept>

namespace blueprint
{
    namespace
    {
        int straight_high(std::uint16_t mask)
        {
            for (int high = 12; high >= 4; --high)
                if ((mask & (31U << (high - 4))) == (31U << (high - 4)))
                    return high;
            return (mask & 0x100fU) == 0x100fU ? 3 : -1;    // A2345, five high
        }

        HandValue encode(HandCategory category, std::array<int, 5> ranks)
        {
            std::uint32_t value = static_cast<std::uint32_t>(category);
            for (auto rank : ranks)
                value = (value << 4) | static_cast<std::uint32_t>(rank < 0 ? 0 : rank + 2);
            return {value};
        }
    }    // namespace

    HandValue evaluate_hand(std::span<const Card> cards)
    {
        if (cards.size() < 5 || cards.size() > 7)
            throw std::invalid_argument("evaluation requires 5, 6 or 7 cards");
        std::array<unsigned int, 13> counts{};
        std::array<unsigned int, 4> suits{};
        std::array<std::uint16_t, 4> suited{};
        std::uint16_t mask = 0;
        std::uint64_t seen = 0;
        for (auto card : cards)
        {
            if (card >= 52 || (seen & (std::uint64_t{1} << card)))
                throw std::invalid_argument("invalid or duplicate evaluator card");
            seen |= std::uint64_t{1} << card;
            ++counts[card % 13];
            ++suits[card / 13];
            mask |= static_cast<std::uint16_t>(1U << (card % 13));
            suited[card / 13] |= static_cast<std::uint16_t>(1U << (card % 13));
        }
        int flush_suit = -1;
        for (int s = 0; s < 4; ++s)
            if (suits[s] >= 5)
            {
                flush_suit = s;
                auto high = straight_high(suited[s]);
                if (high >= 0)
                    return encode(HandCategory::straight_flush, {high, -1, -1, -1, -1});
            }
        std::array<int, 13> ordered{};
        std::size_t distinct = 0;
        int quad = -1, trip = -1, pair = -1;
        for (int r = 12; r >= 0; --r)
        {
            if (counts[r])
                ordered[distinct++] = r;
            if (counts[r] == 4)
                quad = r;
            if (counts[r] >= 3 && trip < 0)
                trip = r;
        }
        if (quad >= 0)
        {
            auto kicker = ordered[0] == quad ? ordered[1] : ordered[0];
            return encode(HandCategory::four_of_a_kind, {quad, kicker, -1, -1, -1});
        }
        if (trip >= 0)
        {
            for (int r = 12; r >= 0; --r)
                if (r != trip && counts[r] >= 2)
                {
                    pair = r;
                    break;
                }
            if (pair >= 0)
                return encode(HandCategory::full_house, {trip, pair, -1, -1, -1});
        }
        if (flush_suit >= 0)
        {
            std::array<int, 5> ranks{};
            std::size_t i = 0;
            for (int r = 12; r >= 0 && i < 5; --r)
                if (suited[flush_suit] & (1U << r))
                    ranks[i++] = r;
            return encode(HandCategory::flush, ranks);
        }
        auto high = straight_high(mask);
        if (high >= 0)
            return encode(HandCategory::straight, {high, -1, -1, -1, -1});
        if (trip >= 0)
        {
            std::array<int, 5> ranks{trip, -1, -1, -1, -1};
            std::size_t i = 1;
            for (std::size_t j = 0; j < distinct && i < 3; ++j)
                if (ordered[j] != trip)
                    ranks[i++] = ordered[j];
            return encode(HandCategory::three_of_a_kind, ranks);
        }
        std::array<int, 2> pairs{-1, -1};
        std::size_t np = 0;
        for (int r = 12; r >= 0 && np < 2; --r)
            if (counts[r] == 2)
                pairs[np++] = r;
        if (np == 2)
        {
            int kicker = -1;
            for (std::size_t j = 0; j < distinct; ++j)
                if (ordered[j] != pairs[0] && ordered[j] != pairs[1])
                {
                    kicker = ordered[j];
                    break;
                }
            return encode(HandCategory::two_pair, {pairs[0], pairs[1], kicker, -1, -1});
        }
        if (np == 1)
        {
            std::array<int, 5> ranks{pairs[0], -1, -1, -1, -1};
            std::size_t i = 1;
            for (std::size_t j = 0; j < distinct && i < 4; ++j)
                if (ordered[j] != pairs[0])
                    ranks[i++] = ordered[j];
            return encode(HandCategory::pair, ranks);
        }
        return encode(HandCategory::high_card, {ordered[0], ordered[1], ordered[2], ordered[3], ordered[4]});
    }
}    // namespace blueprint
