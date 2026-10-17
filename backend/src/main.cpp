#include <charconv>
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

#include "blueprint/model.hpp"
#include "blueprint/poker.hpp"

namespace
{
    void usage()
    {
        std::cout << "blueprint verify-models (--config models.conf | --flop PATH --turn PATH --river PATH)\n"
                     "blueprint inspect-model --model PATH --street flop|turn|river\n"
                     "blueprint bucket --model PATH --street flop|turn|river --hand Ac,Ad --board Kh,7s,2d\n"
                     "blueprint bucket --street preflop --hand Ac,Ad\n"
                     "blueprint simulate-hand --seed 42\n";
    }

    std::vector<blueprint::Card> cards(std::string text)
    {
        std::vector<blueprint::Card> result;
        std::size_t start = 0;
        do
        {
            auto pos = text.find(',', start);
            result.push_back(blueprint::parse_card(text.substr(start, pos == text.npos ? pos : pos - start)));
            if (pos == text.npos)
                break;
            start = pos + 1;
        } while (true);
        return result;
    }

    void print(const blueprint::ModelReader& model)
    {
        auto& info = model.info();
        std::cout << "round=" << static_cast<int>(info.street) - 1 << " states=" << info.states << " complete=" << info.complete
                  << " dimension=" << info.dimension << " sha256=" << info.sha256 << " source_catalogue_fnv1a64=" << std::hex
                  << std::setfill('0') << std::setw(16) << info.source_catalogue_fnv1a64 << std::dec << '\n';
    }
}    // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--help")
        {
            usage();
            return 0;
        }
        if (argc < 2)
            throw std::invalid_argument("missing command (use --help)");
        std::string command = argv[1];
        std::map<std::string, std::string> options;
        for (int i = 2; i < argc; i += 2)
        {
            if (i + 1 >= argc || !std::string(argv[i]).starts_with("--") || !options.emplace(argv[i], argv[i + 1]).second)
                throw std::invalid_argument("invalid or duplicate option");
        }
        auto take = [&](const std::string& key) {
            auto it = options.find(key);
            if (it == options.end())
                throw std::invalid_argument("missing option " + key);
            auto v = it->second;
            options.erase(it);
            return v;
        };
        auto done = [&]() {
            if (!options.empty())
                throw std::invalid_argument("unknown or incompatible option: " + options.begin()->first);
        };
        if (command == "simulate-hand")
        {
            auto text = take("--seed");
            done();
            std::uint64_t seed = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), seed);
            if (error != std::errc{} || end != text.data() + text.size())
                throw std::invalid_argument("seed must be an unsigned 64-bit decimal integer");
            blueprint::PokerState game(seed);
            std::size_t actions = 0;
            while (!game.is_terminal())
            {
                if (game.public_state().phase == blueprint::HandPhase::chance)
                    game.deal_next_street();
                else
                {
                    auto legal = game.legal_actions();
                    game.apply(game.public_state().actor, legal.check ? blueprint::Action::check() : blueprint::Action::call());
                    ++actions;
                }
            }
            auto paid = game.settle();
            std::cout << "rules=" << blueprint::poker_rules_id << " deal=" << blueprint::deal_profile_id << " seed=" << seed
                      << " actions=" << actions << " pot=" << game.public_state().pot() << "\n";
            for (blueprint::Seat seat = 0; seat < blueprint::seat_count; ++seat)
                std::cout << "seat=" << static_cast<unsigned int>(seat) << " final_stack=" << paid.final_stacks[seat]
                          << " utility=" << paid.utility[seat] << "\n";
            return 0;
        }
        if (command == "verify-models")
        {
            blueprint::ModelPaths paths;
            if (options.contains("--config"))
                paths = blueprint::ModelPaths::read(take("--config"));
            else
            {
                paths.flop = take("--flop");
                paths.turn = take("--turn");
                paths.river = take("--river");
            }
            done();
            blueprint::BucketProvider provider(paths);
            for (auto street : {blueprint::Street::flop, blueprint::Street::turn, blueprint::Street::river})
            {
                const auto& model = provider.model(street);
                print(model);
                const std::array<blueprint::Card, 5> board{37, 44, 13, 1, 28};    // Kh 7s 2d 3c 4h
                auto bucket =
                    provider.bucket(street, {12, 25}, std::span<const blueprint::Card>(board.data(), blueprint::board_size(street)));
                std::cout << "canonical_vector_bucket=" << bucket << '\n';
            }
            return 0;
        }
        if (command != "inspect-model" && command != "bucket")
            throw std::invalid_argument("unknown command");
        auto name = take("--street");
        blueprint::Street street;
        if (name == "preflop")
            street = blueprint::Street::preflop;
        else if (name == "flop")
            street = blueprint::Street::flop;
        else if (name == "turn")
            street = blueprint::Street::turn;
        else if (name == "river")
            street = blueprint::Street::river;
        else
            throw std::invalid_argument("invalid street");
        if (command == "inspect-model")
        {
            auto path = take("--model");
            done();
            print(blueprint::ModelReader::load(path, street));
            return 0;
        }
        auto hand = cards(take("--hand"));
        if (hand.size() != 2)
            throw std::invalid_argument("hand requires two cards");
        if (street == blueprint::Street::preflop)
        {
            done();
            std::cout << blueprint::preflop_class({hand[0], hand[1]}) << '\n';
            return 0;
        }
        auto board = cards(take("--board"));
        auto path = take("--model");
        done();
        if (board.size() != blueprint::board_size(street))
            throw std::invalid_argument("board length does not match street");
        blueprint::validate_cards({hand[0], hand[1]}, board);
        auto model = blueprint::ModelReader::load(path, street);
        std::cout << static_cast<unsigned int>(model.bucket({hand[0], hand[1]}, board)) << '\n';
    }
    catch (const std::exception& e)
    {
        std::cerr << "blueprint: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
