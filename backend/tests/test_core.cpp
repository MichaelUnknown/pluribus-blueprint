#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

#include "blueprint/model.hpp"

namespace
{
    void check(bool condition)
    {
        if (!condition)
            throw std::runtime_error("test assertion failed");
    }

    template <class F>
    void rejects(F f)
    {
        bool rejected = false;
        try
        {
            f();
        }
        catch (const std::exception&)
        {
            rejected = true;
        }
        check(rejected);
    }
}    // namespace

int main(int argc, char** argv)
{
    using namespace blueprint;
    try
    {
        if (argc == 2)
        {
            BucketProvider provider(ModelPaths::read(argv[1]), CoveragePolicy::test_fixture_only);
            std::array<Card, 5> board{37, 44, 13, 1, 28};
            for (auto street : {Street::flop, Street::turn, Street::river})
            {
                auto b = std::span<const Card>(board.data(), board_size(street));
                check(provider.bucket(street, {12, 25}, b) == provider.model(street).bucket({12, 25}, b));
            }
            check(provider.bucket(Street::preflop, {12, 25}, {}) == 12);
            rejects([&] { provider.bucket(Street::preflop, {12, 25}, board); });
            rejects([&] { provider.model(Street::preflop); });
            return 0;
        }
        if (argc == 4)
        {
            auto street = static_cast<Street>(std::stoi(argv[2]));
            auto model = ModelReader::load(argv[1], street, CoveragePolicy::test_fixture_only);
            std::array<Card, 5> board{37, 44, 13, 1, 28};
            auto b = std::span<const Card>(board.data(), board_size(street));
            auto bucket = model.bucket({12, 25}, b);
            check(bucket == static_cast<unsigned int>(std::stoi(argv[3])));
            auto reversed = board;
            std::reverse(reversed.begin(), reversed.begin() + static_cast<std::ptrdiff_t>(b.size()));
            check(model.bucket({25, 12}, std::span<const Card>(reversed.data(), b.size())) == bucket);
            std::array<Card, 4> perm{0, 1, 2, 3};
            do
            {
                auto map = [&](Card c) { return static_cast<Card>(perm[c / 13] * 13 + c % 13); };
                auto mapped = board;
                for (auto& c : mapped)
                    c = map(c);
                check(model.bucket({map(12), map(25)}, std::span<const Card>(mapped.data(), b.size())) == bucket);
            } while (std::next_permutation(perm.begin(), perm.end()));
            check(model.info().sha256.size() == 64);
            std::cout << model.info().sha256 << '\n';
            rejects([&] { model.bucket({12, 25}, {}); });
            rejects([&] { model.bucket({0, 1}, b); });
            return 0;
        }
        std::array<Card, 5> board{37, 44, 13, 1, 28};
        std::array<std::uint64_t, 3> expected{207882226, 13304391666, 851481053548};
        for (std::size_t size = 3; size <= 5; ++size)
        {
            auto b = std::span<const Card>(board.data(), size);
            auto c = canonicalize({12, 25}, b);
            check(c.key == expected[size - 3] && c.orbit_weight == 24);
            auto sorted = board;
            std::sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(size));
            do
            {
                std::array<Card, 4> perm{0, 1, 2, 3};
                do
                {
                    auto map = [&](Card card) { return static_cast<Card>(13 * perm[card / 13] + card % 13); };
                    auto mapped = sorted;
                    for (auto& card : mapped)
                        card = map(card);
                    auto variant = canonicalize({map(25), map(12)}, std::span<const Card>(mapped.data(), size));
                    check(variant.key == c.key && variant.orbit_weight == c.orbit_weight);
                } while (std::next_permutation(perm.begin(), perm.end()));
            } while (std::next_permutation(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(size)));
        }
        std::array<unsigned int, 169> classes{};
        for (Card a = 0; a < 52; ++a)
            for (Card b = static_cast<Card>(a + 1); b < 52; ++b)
            {
                auto id = preflop_class({a, b});
                check(id < 169);
                check(preflop_class({b, a}) == id);
                ++classes[id];
            }
        for (std::size_t i = 0; i < 169; ++i)
            check(classes[i] == (i < 13 ? 6U : i < 91 ? 4U : 12U));
        check(preflop_class({12, 25}) == 12 && preflop_class({0, 1}) == 13 && preflop_class({0, 14}) == 91);
        check(parse_card("Ac") == 12 && parse_card("As") == 51);
        rejects([] { parse_card("ac"); });
        rejects([] { parse_card(""); });
        rejects([] { preflop_class({0, 0}); });
        rejects([] { preflop_class({0, 52}); });
        rejects([&] { canonicalize({12, 25}, std::span<const Card>(board.data(), 2)); });
        rejects([] { canonicalize({12, 25}, std::array<Card, 3>{12, 1, 2}); });
        rejects([] { board_size(static_cast<Street>(9)); });
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
