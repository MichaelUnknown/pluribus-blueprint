#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "blueprint/evaluator.hpp"
#include "blueprint/poker.hpp"

using namespace blueprint;

namespace
{
    void check(bool value, const char* message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    std::vector<Card> cards(std::string text)
    {
        std::vector<Card> out;
        for (std::size_t i = 0; i < text.size(); i += 3)
            out.push_back(parse_card(text.substr(i, 2)));
        return out;
    }

    // Independent five-card oracle: sort ranks into multiplicity groups, no rank bit masks.
    HandValue five_reference(std::array<Card, 5> hand)
    {
        std::map<int, int> counts;
        std::array<int, 5> ranks{};
        bool flush = true;
        for (std::size_t i = 0; i < 5; ++i)
        {
            ++counts[hand[i] % 13];
            ranks[i] = hand[i] % 13;
            flush &= hand[i] / 13 == hand[0] / 13;
        }
        std::sort(ranks.begin(), ranks.end(), std::greater<>());
        bool straight = counts.size() == 5 && ranks[0] - ranks[4] == 4;
        int high = ranks[0];
        if (ranks == std::array<int, 5>{12, 3, 2, 1, 0})
        {
            straight = true;
            high = 3;
        }
        std::vector<std::pair<int, int>> groups;
        for (auto [r, n] : counts)
            groups.emplace_back(n, r);
        std::sort(groups.begin(), groups.end(), std::greater<>());
        unsigned int category = 0;
        std::vector<int> tie;
        if (straight && flush)
        {
            category = 8;
            tie = {high};
        }
        else if (groups[0].first == 4)
        {
            category = 7;
            tie = {groups[0].second, groups[1].second};
        }
        else if (groups[0].first == 3 && groups[1].first == 2)
        {
            category = 6;
            tie = {groups[0].second, groups[1].second};
        }
        else if (flush)
        {
            category = 5;
            tie.assign(ranks.begin(), ranks.end());
        }
        else if (straight)
        {
            category = 4;
            tie = {high};
        }
        else
        {
            category = groups[0].first == 3 ? 3 : groups[0].first == 2 ? (groups[1].first == 2 ? 2 : 1) : 0;
            for (auto [n, r] : groups)
            {
                (void)n;
                tie.push_back(r);
            }
        }
        std::uint32_t value = category;
        for (std::size_t i = 0; i < 5; ++i)
            value = (value << 4) | (i < tie.size() ? static_cast<std::uint32_t>(tie[i] + 2) : 0);
        return {value};
    }

    void exhaustive()
    {
        std::array<std::uint64_t, 9> frequencies{};
        std::array<std::set<std::uint32_t>, 9> strengths;
        for (Card a = 0; a < 48; ++a)
            for (Card b = static_cast<Card>(a + 1); b < 49; ++b)
                for (Card c = static_cast<Card>(b + 1); c < 50; ++c)
                    for (Card d = static_cast<Card>(c + 1); d < 51; ++d)
                        for (Card e = static_cast<Card>(d + 1); e < 52; ++e)
                        {
                            std::array<Card, 5> hand{a, b, c, d, e};
                            auto rank = evaluate_hand(hand);
                            auto category = static_cast<std::size_t>(rank.category());
                            check(category < 9, "invalid category");
                            ++frequencies[category];
                            strengths[category].insert(rank.value);
                        }
        check(frequencies == std::array<std::uint64_t, 9>{1302540, 1098240, 123552, 54912, 10200, 5108, 3744, 624, 40},
              "five-card category census");
        constexpr std::array<std::size_t, 9> distinct{1277, 2860, 858, 858, 10, 1277, 156, 156, 10};
        for (std::size_t i = 0; i < 9; ++i)
            check(strengths[i].size() == distinct[i], "five-card strength census");
        std::cout << "All 2,598,960 five-card hands: category frequencies and 7,462 strengths verified\n";
    }

    void examples()
    {
        const std::array<std::string, 9> examples{"Ac Jd 9h 7s 4c 3d 2h", "Ac Ad Jh 9s 7c 4d 2h", "Ac Ad Jh Js 9c 4d 2h",
                                                  "Ac Ad Ah Js 9c 4d 2h", "Ac 2d 3h 4s 5c 9d Kh", "Ac Jc 9c 7c 4c 3d 2h",
                                                  "Ac Ad Ah Js Jc Jd 2h", "Ac Ad Ah As Jc 4d 2h", "Ac 2c 3c 4c 5c 9d Kh"};
        HandValue previous{};
        for (std::size_t i = 0; i < examples.size(); ++i)
        {
            auto hand = cards(examples[i]);
            auto value = evaluate_hand(hand);
            check(static_cast<std::size_t>(value.category()) == i, "fixed category");
            check(i == 0 || value > previous, "category ordering");
            previous = value;
            std::reverse(hand.begin(), hand.end());
            check(evaluate_hand(hand) == value, "card-order invariance");
        }
        auto beats = [](std::string a, std::string b) {
            check(evaluate_hand(cards(a)) > evaluate_hand(cards(b)), "fixed kicker ordering");
        };
        beats("2c 3d 4h 5s 6c", "Ac 2d 3h 4s 5c");
        beats("Ac Ad Ah Ks Kc", "Kc Kd Kh As Ac");
        beats("Ac Ad Ah Js Jc Jd 2h", "Ac Ad Ah Ts Tc Td 2h");
        beats("Ac Ad Ah As Kc", "Ac Ad Ah As Qc");
        beats("Ac Ad Kc Kd Qc Qd 2s", "Ac Ad Kc Kd Jc Jd 2s");
        beats("Ac Kc 8c 6c 3c", "Ac Qc Jc 9c 7c");
        beats("Ac Ad Kc Qd Jh", "Ac Ad Kc Qd Th");
        beats("Ac Ad Ah Kc Qd", "Ac Ad Ah Kc Jd");
        auto board = cards("Tc Jc Qc Kc Ac");
        auto a = board, b = board;
        a.insert(a.end(), {0, 14});
        b.insert(b.end(), {25, 38});
        check(evaluate_hand(a) == evaluate_hand(b), "board-only tie");
        for (auto bad : {cards("Ac Ac 2d 3d 4h"), cards("Ac 2d 3h 4s"), cards("Ac 2d 3h 4s 5c 6d 7h 8s")})
        {
            bool threw = false;
            try
            {
                evaluate_hand(bad);
            }
            catch (const std::invalid_argument&)
            {
                threw = true;
            }
            check(threw, "reject bad evaluation");
        }
        bool threw = false;
        try
        {
            evaluate_hand(std::array<Card, 5>{0, 1, 2, 3, 52});
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        check(threw, "out of range card");
    }

    void seven_reference()
    {
        for (std::uint64_t seed = 0; seed < 10000; ++seed)
        {
            auto deck = shuffled_deck(seed);
            for (std::size_t size : {5U, 6U, 7U})
            {
                HandValue best{};
                for (std::size_t a = 0; a < size - 4; ++a)
                    for (std::size_t b = a + 1; b < size - 3; ++b)
                        for (std::size_t c = b + 1; c < size - 2; ++c)
                            for (std::size_t d = c + 1; d < size - 1; ++d)
                                for (std::size_t e = d + 1; e < size; ++e)
                                    best = std::max(best, five_reference({deck[a], deck[b], deck[c], deck[d], deck[e]}));
                check(evaluate_hand(std::span<const Card>(deck.data(), size)) == best, "independent best-of-five oracle");
            }
        }
    }
}    // namespace

int main(int argc, char**)
{
    try
    {
        if (argc > 1)
            exhaustive();
        else
        {
            examples();
            seven_reference();
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
