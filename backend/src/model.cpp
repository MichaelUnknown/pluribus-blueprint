#include "blueprint/model.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <type_traits>

#include <openssl/evp.h>

namespace blueprint
{
    namespace
    {
        constexpr std::uint64_t basis = 14695981039346656037ULL, prime = 1099511628211ULL;

        void require(bool condition, const char* message)
        {
            if (!condition)
                throw std::runtime_error(message);
        }

        class Input
        {
            std::ifstream file_;
            std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest_{EVP_MD_CTX_new(), EVP_MD_CTX_free};
            bool payload_ = false;

           public:
            std::uint64_t remaining, checksum = basis;

            explicit Input(const std::filesystem::path& path) : file_(path, std::ios::binary), remaining(std::filesystem::file_size(path))
            {
                require(file_.is_open(), "cannot open model");
                require(remaining >= 32 && remaining <= std::uint64_t{4} * 1024 * 1024 * 1024, "model size outside [32 bytes, 4 GiB]");
                require(digest_ && EVP_DigestInit_ex(digest_.get(), EVP_sha256(), nullptr) == 1, "SHA-256 initialization failed");
            }

            void payload() { payload_ = true; }

            void bytes(unsigned char* out, std::size_t n)
            {
                require(n <= remaining, "truncated model / invalid length");
                require(static_cast<bool>(file_.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n))), "model read failed");
                require(EVP_DigestUpdate(digest_.get(), out, n) == 1, "SHA-256 update failed");
                if (payload_)
                    for (std::size_t i = 0; i < n; ++i)
                        checksum = (checksum ^ out[i]) * prime;
                remaining -= n;
            }

            template <class T>
            T integer()
            {
                static_assert(std::is_unsigned_v<T>);
                std::array<unsigned char, sizeof(T)> b{};
                bytes(b.data(), b.size());
                std::uint64_t value = 0;
                for (std::size_t i = 0; i < b.size(); ++i)
                    value |= std::uint64_t{b[i]} << (8 * i);
                return static_cast<T>(value);
            }

            float f32() { return std::bit_cast<float>(integer<std::uint32_t>()); }

            double f64() { return std::bit_cast<double>(integer<std::uint64_t>()); }

            std::string string()
            {
                auto n = integer<std::uint32_t>();
                require(n <= 1024 && n <= remaining, "invalid provenance string length");
                std::string s(n, '\0');
                bytes(reinterpret_cast<unsigned char*>(s.data()), n);
                return s;
            }

            std::string finish()
            {
                require(remaining == 0 && file_.peek() == std::ifstream::traits_type::eof(), "trailing model data");
                std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
                unsigned int size = 0;
                require(EVP_DigestFinal_ex(digest_.get(), hash.data(), &size) == 1 && size == 32, "SHA-256 finalization failed");
                std::ostringstream out;
                out << std::hex << std::setfill('0');
                for (unsigned int i = 0; i < size; ++i)
                    out << std::setw(2) << static_cast<unsigned int>(hash[i]);
                return out.str();
            }
        };

        constexpr std::array<std::uint64_t, 3> counts{1286792, 13960050, 123156254};
        constexpr std::array<std::uint64_t, 3> weights{25989600, 305377800, 2809475760};
        constexpr std::array<std::uint8_t, 169> groups{
            4, 1, 1, 1, 1, 1, 2, 2, 2, 4, 4, 4, 6, 1, 6, 1, 1, 1, 1, 2, 2, 3, 4, 4, 6, 6, 1, 1, 6, 1, 1, 2, 2, 2, 3, 3, 4, 4, 6, 1, 1, 1, 6,
            3, 3, 3, 3, 3, 3, 4, 4, 6, 1, 1, 1, 1, 7, 3, 3, 3, 3, 4, 5, 6, 6, 1, 1, 1, 2, 3, 7, 3, 3, 5, 5, 5, 6, 7, 1, 1, 2, 2, 3, 3, 8, 3,
            5, 5, 5, 6, 7, 2, 2, 2, 2, 3, 3, 3, 8, 5, 5, 5, 7, 7, 2, 2, 2, 2, 3, 3, 3, 5, 8, 5, 7, 7, 7, 2, 2, 4, 4, 4, 4, 5, 5, 5, 8, 7, 7,
            7, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 8, 7, 7, 4, 4, 4, 6, 6, 6, 6, 6, 7, 7, 7, 8, 7, 6, 6, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8};

        std::uint64_t grouping_hash()
        {
            auto hash = basis;
            for (auto g : groups)
                hash = (hash ^ g) * prime;
            return hash;
        }

        CanonicalCards decode(std::uint64_t key, std::size_t size)
        {
            std::array<Card, 7> cards{};
            auto rest = key;
            for (std::size_t i = size + 2; i > 0; --i)
            {
                cards[i - 1] = static_cast<Card>(rest & 63);
                rest >>= 6;
            }
            require(rest == 0, "key has excess bits");
            require(cards[0] < cards[1] && std::is_sorted(cards.begin() + 2, cards.begin() + static_cast<std::ptrdiff_t>(size + 2)),
                    "unsorted cards in key");
            return canonicalize({cards[0], cards[1]}, std::span<const Card>(cards.data() + 2, size));
        }
    }    // namespace

    ModelReader ModelReader::load(const std::filesystem::path& path, Street expected, CoveragePolicy policy)
    {
        require(expected == Street::flop || expected == Street::turn || expected == Street::river, "model requires postflop street");
        Input in(path);
        std::array<unsigned char, 8> magic{};
        in.bytes(magic.data(), magic.size());
        require(std::string(magic.begin(), magic.end()) == "FLOPABS1", "invalid model magic");
        require(in.integer<std::uint32_t>() == 4, "artifact is not a model (kind 4)");
        require(in.integer<std::uint32_t>() == 2, "only model format v2 is supported");
        auto length = in.integer<std::uint64_t>(), checksum = in.integer<std::uint64_t>();
        require(length == in.remaining, "payload length does not match file size");
        in.payload();
        require(in.string() == "c6b6963670623e21166e185832dd3a775eb75b6d", "unsupported evaluator revision");
        require(in.string() == "suit24-private2-unordered-board-lex6-v2", "unsupported canonicalization revision");
        require(in.string() == "uniform-heads-up-half-ties-v2", "unsupported feature revision");
        auto round = in.integer<std::uint8_t>(), complete = in.integer<std::uint8_t>();
        require(round < 3 && round == static_cast<unsigned int>(expected) - 1, "model round mismatch");
        require(complete <= 1, "invalid completeness flag");
        require(complete || policy == CoveragePolicy::test_fixture_only, "partial model rejected: complete production coverage required");
        auto n = in.integer<std::uint64_t>();
        require(n >= 200 && n <= counts[round] && n <= in.remaining / 10, "invalid catalogue count");
        require(!complete || n == counts[round], "complete catalogue count mismatch");
        ModelReader result;
        result.info_ = {expected, complete != 0, 0, n, 0, 0, {}};
        result.keys_.resize(static_cast<std::size_t>(n));
        std::vector<std::uint8_t> orbit(static_cast<std::size_t>(n));
        for (std::size_t i = 0; i < n; ++i)
        {
            auto key = in.integer<std::uint64_t>();
            auto weight = in.integer<std::uint8_t>();
            require(i == 0 || key > result.keys_[i - 1], "catalogue keys must be strictly ascending");
            auto canonical = decode(key, board_size(expected));
            require(canonical.key == key && canonical.orbit_weight == weight, "invalid canonical key or orbit weight");
            result.keys_[i] = key;
            orbit[i] = weight;
            result.info_.orbit_sum += weight;
        }
        require(!complete || result.info_.orbit_sum == weights[round], "complete orbit sum mismatch");
        result.info_.source_catalogue_fnv1a64 = in.integer<std::uint64_t>();
        require(in.integer<std::uint8_t>() == round, "feature round mismatch");
        auto dim = in.integer<std::uint32_t>(), completions = in.integer<std::uint32_t>(), opponents = in.integer<std::uint32_t>();
        (void)in.integer<std::uint64_t>();
        auto grouping = in.integer<std::uint64_t>();
        require(opponents == 990, "invalid opponent hand count");
        require(round == 2 ? dim == 8 && completions == 1 && grouping == grouping_hash()
                           : dim >= 2 && dim <= 1000 && completions == (round == 0 ? 1081U : 46U) && grouping == 0,
                "invalid feature configuration");
        result.info_.dimension = dim;
        auto buckets = in.integer<std::uint32_t>(), restarts = in.integer<std::uint32_t>(), updates = in.integer<std::uint32_t>();
        auto tolerance = in.f64();
        (void)in.integer<std::uint64_t>();
        auto update = in.integer<std::uint8_t>(), weighting = in.integer<std::uint8_t>(), metric = in.integer<std::uint8_t>();
        require(buckets == 200 && restarts >= 1 && restarts <= 1000 && updates >= 1 && updates <= 100000, "invalid training dimensions");
        require(std::isfinite(tolerance) && tolerance >= 0 && tolerance < 1 && update <= 1 && weighting <= 1 &&
                    metric == (round == 2 ? 1 : 0) && (round != 2 || update == 1),
                "invalid training configuration");
        (void)in.integer<std::uint64_t>();
        require(std::uint64_t{200} * dim * 4 <= in.remaining, "truncated centers");
        for (unsigned int b = 0; b < 200; ++b)
        {
            float previous = 0;
            for (unsigned int j = 0; j < dim; ++j)
            {
                auto value = in.f32();
                require(std::isfinite(value) && value >= 0 && value <= 1, "invalid center coordinate");
                if (round != 2)
                    require(value >= previous && (j + 1 != dim || value == 1), "invalid CDF center");
                previous = value;
            }
        }
        require(n <= in.remaining, "truncated assignments");
        result.assignments_.resize(static_cast<std::size_t>(n));
        in.bytes(result.assignments_.data(), result.assignments_.size());
        std::array<std::uint64_t, 200> occupancy{}, weighted{};
        for (std::size_t i = 0; i < n; ++i)
        {
            auto b = result.assignments_[i];
            require(b < 200, "bucket ID outside 0..199");
            ++occupancy[b];
            weighted[b] += weighting ? orbit[i] : 1;
        }
        auto history = in.integer<std::uint64_t>();
        require(history > 0 && history <= std::uint64_t{restarts} * (updates + 1) && history <= in.remaining / 20,
                "invalid history length");
        double distance_bound = round == 2 ? std::sqrt(8.0) : 1.0;
        double objective_bound = static_cast<double>(weighting ? result.info_.orbit_sum : n) * (round == 2 ? 8.0 : 1.0);
        for (std::uint64_t i = 0; i < history; ++i)
        {
            auto restart = in.integer<std::uint32_t>(), iteration = in.integer<std::uint32_t>(), repairs = in.integer<std::uint32_t>();
            auto objective = in.f64();
            require(restart < restarts && iteration <= updates && repairs <= 200 && std::isfinite(objective) && objective >= 0 &&
                        objective <= objective_bound,
                    "invalid history record");
        }
        std::array<double, 5> stats{};
        for (auto& v : stats)
        {
            v = in.f64();
            require(std::isfinite(v) && v >= 0, "invalid diagnostics");
        }
        require(stats[0] <= objective_bound && stats[1] <= distance_bound && stats[2] <= stats[3] && stats[3] <= stats[4] &&
                    stats[4] <= distance_bound,
                "diagnostics outside bounds");
        require(in.integer<std::uint64_t>() == 200, "occupancy length must be 200");
        for (auto count : occupancy)
            require(count > 0 && in.integer<std::uint64_t>() == count, "occupancy mismatch or empty bucket");
        for (auto count : weighted)
            require(in.integer<std::uint64_t>() == count, "weighted occupancy mismatch");
        require(in.checksum == checksum, "payload FNV-1a checksum mismatch");
        result.info_.sha256 = in.finish();
        return result;
    }

    std::uint8_t ModelReader::bucket(std::array<Card, 2> hand, std::span<const Card> board) const
    {
        require(board.size() == board_size(info_.street), "board length does not match model round");
        auto key = canonicalize(hand, board).key;
        auto it = std::lower_bound(keys_.begin(), keys_.end(), key);
        require(it != keys_.end() && *it == key, "canonical key missing from model");
        return assignments_[static_cast<std::size_t>(it - keys_.begin())];
    }

    BucketProvider::BucketProvider(const ModelPaths& paths, CoveragePolicy policy)
        : models_{ModelReader::load(paths.flop, Street::flop, policy), ModelReader::load(paths.turn, Street::turn, policy),
                  ModelReader::load(paths.river, Street::river, policy)}
    {
    }

    const ModelReader& BucketProvider::model(Street street) const
    {
        require(street == Street::flop || street == Street::turn || street == Street::river, "no preflop model file");
        return models_[static_cast<std::size_t>(street) - 1];
    }

    std::uint16_t BucketProvider::bucket(Street street, std::array<Card, 2> hand, std::span<const Card> board) const
    {
        require(board.size() == board_size(street), "board length does not match street");
        if (street == Street::preflop)
            return preflop_class(hand);
        return model(street).bucket(hand, board);
    }
}    // namespace blueprint
