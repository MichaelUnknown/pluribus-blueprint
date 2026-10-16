#pragma once
#include <filesystem>
#include <string>
#include <vector>

#include "blueprint/cards.hpp"

namespace blueprint
{
    // Deliberately explicit: production callers must use the default complete policy.
    enum class CoveragePolicy
    {
        production,
        test_fixture_only
    };

    struct ModelInfo
    {
        Street street{};
        bool complete{};
        std::uint32_t dimension{};
        std::uint64_t states{}, orbit_sum{}, source_catalogue_fnv1a64{};
        std::string sha256;
    };

    class ModelReader
    {
       public:
        static ModelReader load(const std::filesystem::path& path, Street expected, CoveragePolicy policy = CoveragePolicy::production);

        const ModelInfo& info() const { return info_; }

        std::uint8_t bucket(std::array<Card, 2> hand, std::span<const Card> board) const;

       private:
        ModelInfo info_;
        std::vector<std::uint64_t> keys_;
        std::vector<std::uint8_t> assignments_;
    };

    struct ModelPaths
    {
        std::filesystem::path flop, turn, river;
        static ModelPaths read(const std::filesystem::path& config);
    };

    class BucketProvider
    {
       public:
        explicit BucketProvider(const ModelPaths& paths, CoveragePolicy policy = CoveragePolicy::production);
        std::uint16_t bucket(Street street, std::array<Card, 2> hand, std::span<const Card> board) const;
        const ModelReader& model(Street street) const;

       private:
        std::array<ModelReader, 3> models_;
    };
}    // namespace blueprint
