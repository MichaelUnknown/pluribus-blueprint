#include <algorithm>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>

#include "blueprint/poker.hpp"

using namespace blueprint;

namespace
{
    void check(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    template <class F>
    void rejects(F f)
    {
        bool threw = false;
        try
        {
            f();
        }
        catch (const std::invalid_argument&)
        {
            threw = true;
        }
        check(threw, "expected invalid_argument");
    }

    void invariants(const PokerState& game)
    {
        const auto& s = game.public_state();
        auto total = std::accumulate(s.rules.starting_stacks.begin(), s.rules.starting_stacks.end(), Chips{0});
        check(std::accumulate(s.stacks.begin(), s.stacks.end(), s.pot()) == total, "chip conservation during hand");
        for (Seat i = 0; i < 6; ++i)
        {
            check(s.stacks[i] >= 0 && s.contributions[i] >= 0 && s.street_contributions[i] >= 0, "nonnegative chips");
            check(s.street_contributions[i] <= s.contributions[i], "street vs total contributions");
            check(s.all_in[i] == (!s.folded[i] && s.stacks[i] == 0), "all-in flag");
            check(!s.pending[i] || (!s.folded[i] && !s.all_in[i]), "pending actor eligibility");
            check(game.observation(i).public_state == s, "observation public state");
        }
        for (std::size_t i = s.revealed; i < 5; ++i)
            check(s.board[i] == hidden_card, "unrevealed card disclosure");
        if (s.phase == HandPhase::decision)
        {
            check(s.actor < 6 && s.pending[s.actor], "decision actor");
            const auto legal = game.legal_actions();
            check(legal.check != legal.call && legal.fold == legal.call, "check/call/fold domain");
            check(legal.call_amount >= 0 && legal.call_amount <= s.stacks[s.actor], "call amount");
            check(legal.raises.has_value() == s.may_raise[s.actor], "public raise permission matches domain");
            if (legal.raises)
            {
                check(legal.raises->minimum > s.current_bet, "raise exceeds bet");
                check(legal.raises->maximum == s.stacks[s.actor] + s.street_contributions[s.actor], "raise cap");
            }
        }
        else
            check(s.actor == no_seat && game.legal_actions() == LegalActions{}, "no actions outside decision");
    }

    void act(PokerState& game, Seat seat, Action action)
    {
        check(game.public_state().actor == seat, "expected actor");
        game.apply(seat, action);
        invariants(game);
    }

    void passive_round(PokerState& game)
    {
        while (game.public_state().phase == HandPhase::decision)
        {
            auto legal = game.legal_actions();
            act(game, game.public_state().actor, legal.check ? Action::check() : Action::call());
        }
    }

    Settlement finish(PokerState& game)
    {
        unsigned int steps = 0;
        while (!game.is_terminal())
        {
            check(++steps < 100, "bounded passive hand");
            if (game.public_state().phase == HandPhase::chance)
                game.deal_next_street();
            else
            {
                auto legal = game.legal_actions();
                game.apply(game.public_state().actor, legal.check ? Action::check() : Action::call());
            }
            invariants(game);
        }
        auto before = game.public_state();
        auto result = game.settle();
        check(game.public_state() == before, "settle is pure");
        check(game.settle().final_stacks == result.final_stacks, "settle idempotent");
        check(std::accumulate(result.utility.begin(), result.utility.end(), Chips{0}) == 0, "zero sum utility");
        check(std::accumulate(result.winnings.begin(), result.winnings.end(), Chips{0}) == before.pot(), "pot payout conservation");
        return result;
    }

    Deck fixed_deck(std::array<std::string, 6> holes, std::string board, Seat button = 0)
    {
        Deck deck{};
        std::array<bool, 52> used{};
        std::size_t cursor = 0;
        auto add = [&](std::string_view text) {
            auto c = parse_card(text);
            check(!used[c], "duplicate fixture card");
            used[c] = true;
            deck[cursor++] = c;
        };
        for (std::size_t round = 0; round < 2; ++round)
            for (std::size_t i = 1; i <= 6; ++i)
                add(std::string_view(holes[(button + i) % 6]).substr(round * 3, 2));
        for (std::size_t i = 0; i < 5; ++i)
            add(std::string_view(board).substr(i * 3, 2));
        for (Card c = 0; c < 52; ++c)
            if (!used[c])
                deck[cursor++] = c;
        return deck;
    }

    void action_order_and_validation()
    {
        constexpr Deck golden{18, 35, 33, 50, 23, 17, 40, 16, 12, 30, 36, 3,  9, 10, 27, 49, 29, 19, 26, 39, 48, 41, 28, 46, 22, 31,
                              4,  21, 20, 43, 47, 11, 45, 13, 37, 6,  38, 15, 7, 32, 8,  1,  44, 34, 24, 2,  42, 5,  25, 0,  51, 14};
        check(shuffled_deck(42) == golden, "seeded deck profile regression vector");
        PokerState deal(golden);
        check(deal.observation(1).private_cards == std::array<Card, 2>{18, 40} &&
                  deal.observation(0).private_cards == std::array<Card, 2>{17, 3},
              "round-robin deal starts at SB");
        auto complete = finish(deal);
        (void)complete;
        check(deal.public_state().board == std::array<Card, 5>{9, 10, 27, 49, 29}, "fixed runout without burns");

        for (Seat button = 0; button < 6; ++button)
        {
            PokerRules rules;
            rules.button = button;
            PokerState game(42, rules);
            auto& start = game.public_state();
            check(start.pot() == 150 && start.current_bet == 100, "initial blinds");
            check(start.contributions[(button + 1) % 6] == 50 && start.contributions[(button + 2) % 6] == 100, "blind positions");
            invariants(game);
            for (unsigned int offset = 3; offset < 8; ++offset)
                act(game, static_cast<Seat>((button + offset) % 6), Action::call());
            check(game.public_state().actor == (button + 2) % 6, "BB retains option");
            check(game.legal_actions().check && game.legal_actions().raises->minimum == 200, "BB check/raise option");
            act(game, static_cast<Seat>((button + 2) % 6), Action::check());
            check(game.public_state().phase == HandPhase::chance, "preflop ends after BB");
            game.deal_next_street();
            check(game.public_state().actor == (button + 1) % 6, "postflop begins left of button");
            check(game.public_state().last_full_raise == 100 && game.legal_actions().raises->minimum == 100, "postflop minimum opening");
            auto result = finish(game);
            (void)result;
            check(game.public_state().revealed == 5 && game.public_state().pot() == 600, "complete six-player passive hand");
        }
        PokerState game(1);
        auto before = game.observation(3);
        rejects([&] { game.apply(3, Action::check()); });
        rejects([&] { game.apply(4, Action::call()); });
        rejects([&] { game.apply(3, Action::raise_to(199)); });
        rejects([&] { game.apply(3, Action::raise_to(10001)); });
        rejects([&] { game.apply(3, {ActionKind::call, 1}); });
        rejects([&] { game.apply(3, {static_cast<ActionKind>(9), 0}); });
        rejects([&] { game.deal_next_street(); });
        rejects([&] { game.settle(); });
        rejects([&] { game.observation(6); });
        check(game.observation(3) == before, "invalid operations leave state unchanged");
        act(game, 3, Action::raise_to(350));
        check(game.legal_actions().raises->minimum == 600, "raise increment is beyond current bet");
        act(game, 4, Action::raise_to(1000));
        check(game.legal_actions().raises->minimum == 1650, "full raise size updated");
        PokerRules invalid;
        invalid.button = 6;
        rejects([&] { PokerState bad(1, invalid); });
        invalid = {};
        invalid.starting_stacks[0] = 0;
        rejects([&] { PokerState bad(1, invalid); });
        invalid = {};
        invalid.starting_stacks[0] = std::numeric_limits<Chips>::max();
        rejects([&] { PokerState bad(1, invalid); });
        invalid = {};
        invalid.big_blind = 0;
        rejects([&] { PokerState bad(1, invalid); });
        auto bad = shuffled_deck(1);
        bad[0] = bad[1];
        rejects([&] { PokerState g(bad); });
        bad = shuffled_deck(1);
        bad[0] = 52;
        rejects([&] { PokerState g(bad); });
        PokerState many_raises(1);
        for (unsigned int i = 0; i < 60; ++i)
            act(many_raises, many_raises.public_state().actor, Action::raise_to(many_raises.legal_actions().raises->minimum));
        check(many_raises.public_state().current_bet == 6100, "no artificial raise-count cap");
        finish(many_raises);
        PokerState last(1);
        for (auto seat : {3, 4, 5, 0, 1})
            act(last, static_cast<Seat>(seat), Action::fold());
        check(last.is_terminal() && last.public_state().revealed == 0, "fold win needs no board");
        auto paid = finish(last);
        check(paid.utility == std::array<Chips, 6>{0, -50, 50, 0, 0, 0}, "blind winner utility includes blinds");
        check(paid.returned_uncalled[2] == 50, "uncalled BB returned");
        rejects([&] { last.apply(2, Action::check()); });
        rejects([&] { last.deal_next_street(); });
    }

    void short_all_ins()
    {
        PokerRules rules;
        rules.starting_stacks[5] = 350;
        PokerState game(1, rules);
        act(game, 3, Action::raise_to(300));
        act(game, 4, Action::call());
        check(game.legal_actions().raises == std::optional<RaiseRange>{{350, 350, true}}, "short all-in only domain");
        act(game, 5, Action::raise_to(350));
        auto unacted = game;
        check(unacted.legal_actions().raises->minimum == 550, "unacted can raise over short all-in");
        act(unacted, 0, Action::raise_to(550));
        for (auto seat : {1, 2})
            act(unacted, static_cast<Seat>(seat), Action::fold());
        check(unacted.legal_actions().raises->minimum == 750, "full raise reopens action");
        for (auto seat : {0, 1, 2})
            act(game, static_cast<Seat>(seat), Action::fold());
        check(!game.legal_actions().raises && game.legal_actions().call_amount == 50, "short raise does not reopen raiser");
        auto before = game.public_state();
        rejects([&] { game.apply(3, Action::raise_to(550)); });
        check(before == game.public_state(), "closed raise rejected atomically");
        act(game, 3, Action::call());
        check(!game.legal_actions().raises, "short raise does not reopen caller");
        act(game, 4, Action::call());
        check(game.public_state().pot() == 1200, "short raise calls conserved");
        finish(game);
        rules = {};
        rules.starting_stacks[5] = 400;
        rules.starting_stacks[1] = 500;
        PokerState cumulative(1, rules);
        act(cumulative, 3, Action::raise_to(300));
        act(cumulative, 4, Action::call());
        act(cumulative, 5, Action::raise_to(400));
        act(cumulative, 0, Action::call());
        act(cumulative, 1, Action::raise_to(500));
        act(cumulative, 2, Action::fold());
        check(cumulative.legal_actions().raises->minimum == 700, "cumulative short all-ins reopen at full increment");
        check(cumulative.public_state().last_full_raise == 200, "short increments do not change full minimum");
        act(cumulative, 3, Action::call());
        check(cumulative.legal_actions().raises.has_value(), "earlier caller also reopened");
        act(cumulative, 4, Action::call());
        check(!cumulative.legal_actions().raises, "later caller not reopened by smaller increment");
        act(cumulative, 0, Action::call());
        finish(cumulative);
        rules = {};
        rules.starting_stacks[2] = 150;
        PokerState postflop(1, rules);
        passive_round(postflop);
        postflop.deal_next_street();
        act(postflop, 1, Action::check());
        check(postflop.legal_actions().raises == std::optional<RaiseRange>{{50, 50, true}}, "short opening all-in");
        act(postflop, 2, Action::raise_to(50));
        check(postflop.legal_actions().raises->minimum == 150, "raise above short opening");
        for (auto seat : {3, 4, 5, 0})
            act(postflop, static_cast<Seat>(seat), Action::call());
        check(!postflop.legal_actions().raises, "short opening does not reopen prior checker");
        act(postflop, 1, Action::call());
        finish(postflop);
    }

    void all_in_runout_and_short_blinds()
    {
        PokerRules rules;
        rules.starting_stacks[3] = 500;
        PokerState game(1, rules);
        act(game, 3, Action::raise_to(500));
        for (auto seat : {4, 5, 0, 1})
            act(game, static_cast<Seat>(seat), Action::fold());
        check(game.public_state().actor == 2 && !game.legal_actions().raises,
              "lone funded player must call or fold, cannot bet into empty side pot");
        auto fold = game;
        act(fold, 2, Action::fold());
        check(finish(fold).utility[3] == 150, "all-in fold win and refund");
        act(game, 2, Action::call());
        check(game.public_state().phase == HandPhase::chance, "all-in matched: chance only");
        for (auto street : {Street::flop, Street::turn, Street::river})
        {
            game.deal_next_street();
            check(game.public_state().street == street, "all-in runout street order");
            check(game.public_state().phase == (street == Street::river ? HandPhase::terminal : HandPhase::chance),
                  "no meaningless all-in checks");
        }
        finish(game);
        PokerState overbet(1);
        act(overbet, 3, Action::raise_to(10000));
        for (auto seat : {4, 5, 0, 1})
            act(overbet, static_cast<Seat>(seat), Action::fold());
        act(overbet, 2, Action::call());
        finish(overbet);    // all players remaining are all-in
        rules = {};
        rules.starting_stacks[4] = 500;
        PokerState unmatched(1, rules);
        act(unmatched, 3, Action::raise_to(10000));
        check(unmatched.legal_actions().call_amount == 500 && !unmatched.legal_actions().raises, "short all-in call amount and no raise");
        act(unmatched, 4, Action::call());
        for (auto seat : {5, 0, 1, 2})
            act(unmatched, static_cast<Seat>(seat), Action::fold());
        check(unmatched.public_state().returned_uncalled[3] == 9500 && unmatched.public_state().stacks[3] == 9500,
              "unmatched shove returned before runout");
        check(!unmatched.public_state().all_in[3], "refund restores stack and all-in flag");
        auto result = finish(unmatched);
        check(result.returned_uncalled[3] == 9500, "refund retained in settlement audit");
        rules = {};
        rules.starting_stacks[2] = 20;
        PokerRules lone_rules;
        lone_rules.starting_stacks = {20, 1000, 30, 50, 60, 40};
        PokerState lone_sb(1, lone_rules);
        for (auto seat : {3, 4, 5, 0})
            act(lone_sb, static_cast<Seat>(seat), Action::call());
        check(lone_sb.public_state().actor == 1 && lone_sb.legal_actions().call_amount == 10 && !lone_sb.legal_actions().raises,
              "sole funded SB only owes actual all-in wager, not nominal BB");
        act(lone_sb, 1, Action::call());
        check(lone_sb.public_state().pot() == 260, "short blind closure contribution total");
        finish(lone_sb);
        PokerRules huge;
        constexpr auto limit = std::numeric_limits<Chips>::max() / 12;
        huge.starting_stacks.fill(limit);
        huge.small_blind = limit / 2;
        huge.big_blind = limit;
        PokerState large(1, huge);
        finish(large);
        PokerState short_bb(1, rules);
        check(short_bb.legal_actions().call_amount == 100 && short_bb.legal_actions().raises->minimum == 200,
              "nominal BB still sets multiway bring-in");
        passive_round(short_bb);
        check(short_bb.public_state().pot() == 520, "short blind is real contribution");
        finish(short_bb);
    }

    void pots_and_showdown()
    {
        auto deck = fixed_deck({"4c 5c", "6c 8c", "Tc 2d", "Ac Ad", "Kc Kd", "Qc Qd"}, "2c 3d 7h 9s Jc");
        PokerRules rules;
        rules.starting_stacks = {300, 10000, 10000, 100, 200, 300};
        PokerState game(deck, rules);
        act(game, 3, Action::call());
        act(game, 4, Action::raise_to(200));
        act(game, 5, Action::raise_to(300));
        act(game, 0, Action::call());
        act(game, 1, Action::fold());
        act(game, 2, Action::fold());
        auto result = finish(game);
        check(result.utility == std::array<Chips, 6>{-300, -50, -100, 450, 100, -100}, "independent main/side pot payouts");
        check(result.pots.size() == 4, "contribution levels include folded blinds");
        check(result.pots[0].amount == 300 && result.pots[1].amount == 250 && result.pots[2].amount == 300 && result.pots[3].amount == 200,
              "exact side-pot amounts");
        check(result.winnings == std::array<Chips, 6>{0, 0, 0, 550, 300, 200}, "different winner for each side pot");
        check(!result.pots[0].eligible[1] && !result.pots[0].eligible[2], "folded contributions are ineligible");
        rules = {};
        rules.small_blind = 1;
        rules.big_blind = 2;
        rules.starting_stacks[0] = rules.starting_stacks[3] = rules.starting_stacks[4] = 100;
        deck = fixed_deck({"Ah As", "6c 8c", "Tc 2d", "Ac Ad", "Kc Kd", "4c 5c"}, "2c 3d 7h 9s Jc");
        PokerState tied(deck, rules);
        act(tied, 3, Action::raise_to(100));
        act(tied, 4, Action::call());
        act(tied, 5, Action::fold());
        act(tied, 0, Action::call());
        act(tied, 1, Action::fold());
        act(tied, 2, Action::fold());
        auto split = finish(tied);
        check(split.winnings[3] == 152 && split.winnings[0] == 151, "odd chip goes to first tied winner left of button");
        auto board_only = fixed_deck({"2d 3d", "4d 5d", "6d 7d", "8d 9d", "Td Jd", "Qd Kd"}, "Tc Jc Qc Kc Ac");
        PokerState six_way_tie(board_only);
        check(finish(six_way_tie).utility == std::array<Chips, 6>{}, "all six play the board and split equally");
        // Folded royal flush cannot beat a live hand, even at showdown.
        deck = fixed_deck({"Tc 3d", "6d 8d", "4h 5h", "Ah Ad", "Kh Kd", "Qh Qd"}, "Ac Kc Qc Jc 2d");
        PokerState folded_best(deck);
        for (auto seat : {3, 4, 5})
            act(folded_best, static_cast<Seat>(seat), Action::call());
        act(folded_best, 0, Action::fold());
        act(folded_best, 1, Action::call());
        act(folded_best, 2, Action::check());
        auto won = finish(folded_best);
        check(won.winnings[0] == 0 && won.winnings[3] == 500, "folded best hand excluded");
    }

    void copies_and_privacy()
    {
        auto deck = shuffled_deck(42), changed = deck;
        std::swap(changed[0], changed[1]);
        std::swap(changed[16], changed[17]);
        PokerState original(deck), other(changed);
        check(original.observation(3) == other.observation(3) && original.legal_actions() == other.legal_actions(),
              "opponent holes and future river excluded");
        auto branch = original;
        act(branch, 3, Action::raise_to(350));
        check(original.public_state().pot() == 150 && original.public_state().actor == 3, "sibling stacks/action obligations isolated");
        auto saved = original;
        for (unsigned int street = 0; street < 3; ++street)
        {
            passive_round(original);
            passive_round(other);
            if (street < 2)
            {
                original.deal_next_street();
                other.deal_next_street();
                check(original.observation(3) == other.observation(3), "unrevealed river stays hidden after flop/turn");
            }
        }
        original.deal_next_street();
        other.deal_next_street();
        check(original.public_state().board[4] != other.public_state().board[4], "different river revealed only on river");
        auto a = finish(original), b = finish(saved);
        check(a.final_stacks == b.final_stacks, "copied state preserves sampled deal and future behavior");
        finish(branch);
        check(branch.public_state().board == original.public_state().board, "sibling branches share same runout without mutation");
    }

    void random_hands()
    {
        std::mt19937_64 choices(591);
        for (std::uint64_t seed = 0; seed < 2000; ++seed)
        {
            PokerRules rules;
            rules.button = static_cast<Seat>(seed % 6);
            for (auto& stack : rules.starting_stacks)
                stack = static_cast<Chips>(1 + choices() % 1000);
            PokerState game(seed, rules), replay = game;
            std::size_t steps = 0;
            while (!game.is_terminal())
            {
                check(++steps < 500, "bounded random hand");
                invariants(game);
                check(game.public_state() == replay.public_state() && game.legal_actions() == replay.legal_actions(),
                      "deterministic replay");
                if (game.public_state().phase == HandPhase::chance)
                {
                    game.deal_next_street();
                    replay.deal_next_street();
                    continue;
                }
                auto legal = game.legal_actions();
                auto action = legal.check ? Action::check() : Action::call();
                auto choice = choices() % 5;
                if (choice == 0 && legal.fold)
                    action = Action::fold();
                if (choice >= 3 && legal.raises)
                {
                    auto range = *legal.raises;
                    auto target =
                        range.minimum + static_cast<Chips>(choices() % static_cast<std::uint64_t>(range.maximum - range.minimum + 1));
                    action = Action::raise_to(target);
                }
                game.apply(game.public_state().actor, action);
                replay.apply(replay.public_state().actor, action);
            }
            auto result = finish(game), expected = finish(replay);
            check(result.utility == expected.utility, "identical complete-hand payoffs");
        }
    }
}    // namespace

int main()
{
    try
    {
        action_order_and_validation();
        short_all_ins();
        all_in_runout_and_short_blinds();
        pots_and_showdown();
        copies_and_privacy();
        random_hands();
        std::cout << "Poker edge cases and 2,000 deterministic random six-player hands passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
