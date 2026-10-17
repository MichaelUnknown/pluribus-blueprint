#pragma once
#include <optional>
#include <vector>

#include "blueprint/cards.hpp"

namespace blueprint
{
    using Chips = std::int64_t;
    using Seat = std::uint8_t;
    inline constexpr std::size_t seat_count = 6;
    inline constexpr Seat no_seat = 6;
    inline constexpr Card hidden_card = 255;
    inline constexpr std::string_view poker_rules_id = "nlhe-six-v1";
    inline constexpr std::string_view deal_profile_id = "mt19937-64-fisher-yates-rejection-no-burn-v1";
    using Deck = std::array<Card, 52>;
    Deck shuffled_deck(std::uint64_t seed);

    struct PokerRules
    {
        std::array<Chips, 6> starting_stacks{10000, 10000, 10000, 10000, 10000, 10000};
        Chips small_blind = 50, big_blind = 100;
        Seat button = 0;
        bool operator==(const PokerRules&) const = default;
    };
    enum class HandPhase : std::uint8_t
    {
        decision,
        chance,
        terminal
    };
    enum class ActionKind : std::uint8_t
    {
        fold,
        check,
        call,
        raise_to
    };

    struct Action
    {
        ActionKind kind{};
        Chips amount{};    // Street contribution target, only for raise_to. Zero otherwise.

        static Action fold() { return {ActionKind::fold, 0}; }

        static Action check() { return {ActionKind::check, 0}; }

        static Action call() { return {ActionKind::call, 0}; }

        static Action raise_to(Chips target) { return {ActionKind::raise_to, target}; }

        bool operator==(const Action&) const = default;
    };

    struct RaiseRange
    {
        Chips minimum, maximum;
        bool short_all_in_only;
        bool operator==(const RaiseRange&) const = default;
    };

    // The complete concrete action domain, without enumerating one action per chip.
    struct LegalActions
    {
        bool fold = false, check = false, call = false;
        Chips call_amount = 0;
        std::optional<RaiseRange> raises;
        bool contains(Action action) const;
        bool operator==(const LegalActions&) const = default;
    };

    // All fields here are public knowledge. Unrevealed board slots are always 255.
    struct PublicPokerState
    {
        PokerRules rules;
        Street street = Street::preflop;
        HandPhase phase = HandPhase::decision;
        Seat actor = no_seat;
        std::array<Chips, 6> stacks{}, contributions{}, street_contributions{}, returned_uncalled{};
        std::array<bool, 6> folded{}, all_in{}, pending{}, may_raise{};
        Chips current_bet = 0, last_full_raise = 0;
        std::array<Card, 5> board{hidden_card, hidden_card, hidden_card, hidden_card, hidden_card};
        std::size_t revealed = 0;
        Chips pot() const;
        bool operator==(const PublicPokerState&) const = default;
    };

    struct PlayerObservation
    {
        PublicPokerState public_state;
        Seat viewer;
        std::array<Card, 2> private_cards;
        bool operator==(const PlayerObservation&) const = default;
    };

    struct PotAward
    {
        Chips amount{};
        std::array<bool, 6> eligible{}, winners{};
        std::array<Chips, 6> payouts{};
    };

    struct Settlement
    {
        std::array<Chips, 6> final_stacks{}, utility{}, returned_uncalled{}, winnings{};
        std::vector<PotAward> pots;
    };

    class PokerState
    {
       public:
        explicit PokerState(std::uint64_t seed, PokerRules rules = {});
        // Full-deck fixture/replay entry point. All 52 cards must be unique and valid.
        explicit PokerState(const Deck& deck, PokerRules rules = {});

        const PublicPokerState& public_state() const { return state_; }

        PlayerObservation observation(Seat viewer) const;
        LegalActions legal_actions() const;
        void apply(Seat actor, Action action);
        void deal_next_street();

        bool is_terminal() const { return state_.phase == HandPhase::terminal; }

        // Pure/idempotent: no second payout or hidden-card disclosure through public state.
        Settlement settle() const;

       private:
        PublicPokerState state_;
        std::array<std::array<Card, 2>, 6> hole_cards_{};
        std::array<Card, 5> runout_{};
        std::array<Chips, 6> acted_at_bet_{};
        std::array<bool, 6> acted_{};
        void contribute(Seat seat, Chips amount);
        void update_turn(Seat after);
        void return_uncalled();
    };
}    // namespace blueprint
