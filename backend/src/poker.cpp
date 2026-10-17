#include "blueprint/poker.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

#include "blueprint/evaluator.hpp"

namespace blueprint
{
    namespace
    {
        Seat next_seat(Seat seat, unsigned int offset = 1)
        {
            return static_cast<Seat>((seat + offset) % seat_count);
        }

        void validate_rules(const PokerRules& rules)
        {
            constexpr auto limit = std::numeric_limits<Chips>::max() / 12;
            if (rules.button >= seat_count || rules.small_blind <= 0 || rules.big_blind < rules.small_blind || rules.big_blind > limit)
                throw std::invalid_argument("invalid button or blinds");
            for (auto stack : rules.starting_stacks)
                if (stack <= 0 || stack > limit)
                    throw std::invalid_argument("six positive starting stacks, each at most INT64_MAX/12, are required");
        }
    }    // namespace

    Deck shuffled_deck(std::uint64_t seed)
    {
        Deck deck{};
        std::iota(deck.begin(), deck.end(), Card{0});
        std::mt19937_64 rng(seed);
        for (std::uint64_t i = 51; i > 0; --i)
        {
            const auto bound = i + 1, threshold = (std::uint64_t{0} - bound) % bound;
            std::uint64_t draw;
            do
            {
                draw = rng();
            } while (draw < threshold);
            std::swap(deck[static_cast<std::size_t>(i)], deck[static_cast<std::size_t>(draw % bound)]);
        }
        return deck;
    }

    bool LegalActions::contains(Action action) const
    {
        if (action.kind != ActionKind::raise_to && action.amount != 0)
            return false;
        switch (action.kind)
        {
            case ActionKind::fold:
                return fold;
            case ActionKind::check:
                return check;
            case ActionKind::call:
                return call;
            case ActionKind::raise_to:
                return raises && action.amount >= raises->minimum && action.amount <= raises->maximum;
        }
        return false;
    }

    Chips PublicPokerState::pot() const
    {
        return std::accumulate(contributions.begin(), contributions.end(), Chips{0});
    }

    PokerState::PokerState(std::uint64_t seed, PokerRules rules) : PokerState(shuffled_deck(seed), rules) {}

    PokerState::PokerState(const Deck& deck, PokerRules rules)
    {
        validate_rules(rules);
        std::uint64_t seen = 0;
        for (auto card : deck)
        {
            if (card >= 52 || (seen & (std::uint64_t{1} << card)))
                throw std::invalid_argument("deck must contain all 52 distinct cards");
            seen |= std::uint64_t{1} << card;
        }
        state_.rules = rules;
        state_.stacks = rules.starting_stacks;
        for (unsigned int round = 0; round < 2; ++round)
            for (unsigned int i = 0; i < 6; ++i)
                hole_cards_[next_seat(rules.button, i + 1)][round] = deck[round * 6 + i];
        std::copy_n(deck.begin() + 12, 5, runout_.begin());
        contribute(next_seat(rules.button), std::min(rules.small_blind, state_.stacks[next_seat(rules.button)]));
        contribute(next_seat(rules.button, 2), std::min(rules.big_blind, state_.stacks[next_seat(rules.button, 2)]));
        state_.current_bet = rules.big_blind;
        state_.last_full_raise = rules.big_blind;
        state_.pending.fill(true);
        update_turn(next_seat(rules.button, 2));
    }

    PlayerObservation PokerState::observation(Seat viewer) const
    {
        if (viewer >= seat_count)
            throw std::invalid_argument("invalid observation seat");
        return {state_, viewer, hole_cards_[viewer]};
    }

    LegalActions PokerState::legal_actions() const
    {
        LegalActions result;
        if (state_.phase != HandPhase::decision)
            return result;
        auto seat = state_.actor;
        auto outstanding = state_.current_bet - state_.street_contributions[seat];
        result.fold = outstanding > 0;
        result.check = outstanding == 0;
        result.call = outstanding > 0;
        result.call_amount = std::min(outstanding, state_.stacks[seat]);
        if (state_.may_raise[seat] && state_.stacks[seat] > outstanding)
        {
            auto maximum = state_.street_contributions[seat] + state_.stacks[seat];
            auto full_minimum = state_.current_bet + state_.last_full_raise;
            result.raises = RaiseRange{std::min(full_minimum, maximum), maximum, maximum < full_minimum};
        }
        return result;
    }

    void PokerState::contribute(Seat seat, Chips amount)
    {
        state_.stacks[seat] -= amount;
        state_.contributions[seat] += amount;
        state_.street_contributions[seat] += amount;
        state_.all_in[seat] = state_.stacks[seat] == 0;
    }

    void PokerState::return_uncalled()
    {
        Seat highest = 0;
        for (Seat i = 1; i < seat_count; ++i)
            if (state_.contributions[i] > state_.contributions[highest])
                highest = i;
        Chips second = 0;
        for (Seat i = 0; i < seat_count; ++i)
            if (i != highest)
                second = std::max(second, state_.contributions[i]);
        auto excess = state_.contributions[highest] - second;
        if (excess > 0)
        {
            // Previous rounds already returned excess; only this street can be unmatched.
            if (excess > state_.street_contributions[highest])
                throw std::logic_error("uncalled contribution crossed street boundary");
            state_.contributions[highest] -= excess;
            state_.street_contributions[highest] -= excess;
            state_.stacks[highest] += excess;
            state_.returned_uncalled[highest] += excess;
            state_.all_in[highest] = state_.stacks[highest] == 0 && !state_.folded[highest];
        }
        state_.current_bet = *std::max_element(state_.street_contributions.begin(), state_.street_contributions.end());
    }

    void PokerState::update_turn(Seat after)
    {
        unsigned int live = 0, able = 0;
        Seat lone = no_seat;
        for (Seat i = 0; i < seat_count; ++i)
        {
            state_.all_in[i] = !state_.folded[i] && state_.stacks[i] == 0;
            if (!state_.folded[i])
            {
                ++live;
                if (state_.stacks[i] > 0)
                {
                    ++able;
                    lone = i;
                }
            }
            if (state_.folded[i] || state_.all_in[i])
                state_.pending[i] = false;
        }
        if (live > 1)
        {
            if (able <= 1)
            {
                // No further contested raise is possible. Do not demand a nominal short BB's excess.
                state_.current_bet = *std::max_element(state_.street_contributions.begin(), state_.street_contributions.end());
                if (lone != no_seat)
                    state_.pending[lone] = state_.street_contributions[lone] < state_.current_bet;
            }
            for (Seat i = 0; i < seat_count; ++i)
            {
                state_.may_raise[i] = !state_.folded[i] && !state_.all_in[i] && able >= 2 &&
                                      state_.stacks[i] > state_.current_bet - state_.street_contributions[i] &&
                                      (!acted_[i] || state_.current_bet - acted_at_bet_[i] >= state_.last_full_raise);
            }
            for (unsigned int step = 1; step <= 6; ++step)
            {
                auto seat = next_seat(after, step);
                if (state_.pending[seat])
                {
                    state_.actor = seat;
                    state_.phase = HandPhase::decision;
                    return;
                }
            }
        }
        state_.pending.fill(false);
        state_.may_raise.fill(false);
        state_.actor = no_seat;
        return_uncalled();
        state_.phase = live == 1 || state_.street == Street::river ? HandPhase::terminal : HandPhase::chance;
    }

    void PokerState::apply(Seat actor, Action action)
    {
        if (state_.phase != HandPhase::decision || actor != state_.actor || !legal_actions().contains(action))
            throw std::invalid_argument("illegal poker action or acting seat");
        state_.pending[actor] = false;
        switch (action.kind)
        {
            case ActionKind::fold:
                state_.folded[actor] = true;
                break;
            case ActionKind::check:
                break;
            case ActionKind::call:
                contribute(actor, std::min(state_.current_bet - state_.street_contributions[actor], state_.stacks[actor]));
                break;
            case ActionKind::raise_to: {
                auto increment = action.amount - state_.current_bet;
                if (increment >= state_.last_full_raise)
                    state_.last_full_raise = increment;
                contribute(actor, action.amount - state_.street_contributions[actor]);
                state_.current_bet = action.amount;
                for (Seat i = 0; i < seat_count; ++i)
                    if (!state_.folded[i] && !state_.all_in[i] && state_.street_contributions[i] < state_.current_bet)
                        state_.pending[i] = true;
                break;
            }
        }
        acted_[actor] = true;
        acted_at_bet_[actor] = state_.current_bet;
        update_turn(actor);
    }

    void PokerState::deal_next_street()
    {
        if (state_.phase != HandPhase::chance)
            throw std::invalid_argument("not at a street transition");
        state_.street = static_cast<Street>(static_cast<unsigned int>(state_.street) + 1);
        auto count = board_size(state_.street);
        std::copy(runout_.begin() + static_cast<std::ptrdiff_t>(state_.revealed), runout_.begin() + static_cast<std::ptrdiff_t>(count),
                  state_.board.begin() + static_cast<std::ptrdiff_t>(state_.revealed));
        state_.revealed = count;
        state_.current_bet = 0;
        state_.last_full_raise = state_.rules.big_blind;
        state_.street_contributions.fill(0);
        acted_.fill(false);
        acted_at_bet_.fill(0);
        state_.pending.fill(true);
        update_turn(state_.rules.button);
    }

    Settlement PokerState::settle() const
    {
        if (!is_terminal())
            throw std::invalid_argument("cannot settle an unfinished hand");
        Settlement result;
        result.final_stacks = state_.stacks;
        result.returned_uncalled = state_.returned_uncalled;
        auto live = std::count(state_.folded.begin(), state_.folded.end(), false);
        std::array<HandValue, 6> values{};
        if (live > 1)
        {
            if (state_.revealed != 5)
                throw std::logic_error("showdown requires a complete board");
            for (Seat i = 0; i < seat_count; ++i)
                if (!state_.folded[i])
                {
                    std::array<Card, 7> cards{hole_cards_[i][0], hole_cards_[i][1]};
                    std::copy(state_.board.begin(), state_.board.end(), cards.begin() + 2);
                    values[i] = evaluate_hand(cards);
                }
        }
        auto levels = state_.contributions;
        std::sort(levels.begin(), levels.end());
        Chips previous = 0;
        for (auto level : levels)
        {
            if (level == previous)
                continue;
            PotAward award;
            unsigned int contributors = 0;
            HandValue best{};
            for (Seat i = 0; i < seat_count; ++i)
                if (state_.contributions[i] >= level)
                {
                    ++contributors;
                    award.eligible[i] = !state_.folded[i];
                    if (award.eligible[i])
                        best = std::max(best, values[i]);
                }
            award.amount = (level - previous) * contributors;
            previous = level;
            unsigned int winners = 0;
            for (Seat i = 0; i < seat_count; ++i)
                if (award.eligible[i] && values[i] == best)
                {
                    award.winners[i] = true;
                    ++winners;
                }
            if (winners == 0)
                throw std::logic_error("pot has no eligible winner");
            auto remainder = award.amount % winners;
            for (unsigned int step = 1; step <= 6; ++step)
            {
                auto seat = next_seat(state_.rules.button, step);
                if (award.winners[seat])
                {
                    award.payouts[seat] = award.amount / winners + (remainder > 0 ? 1 : 0);
                    if (remainder > 0)
                        --remainder;
                    result.winnings[seat] += award.payouts[seat];
                    result.final_stacks[seat] += award.payouts[seat];
                }
            }
            result.pots.push_back(award);
        }
        for (Seat i = 0; i < seat_count; ++i)
            result.utility[i] = result.final_stacks[i] - state_.rules.starting_stacks[i];
        if (std::accumulate(result.utility.begin(), result.utility.end(), Chips{0}) != 0)
            throw std::logic_error("chip conservation failed");
        return result;
    }
}    // namespace blueprint
