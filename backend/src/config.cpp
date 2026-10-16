#include <fstream>
#include <map>
#include <stdexcept>

#include "blueprint/model.hpp"

namespace blueprint
{
    ModelPaths ModelPaths::read(const std::filesystem::path& config)
    {
        std::ifstream in(config);
        if (!in)
            throw std::runtime_error("cannot open model configuration: " + config.string());
        std::map<std::string, std::filesystem::path> paths;
        auto trim = [](std::string s) {
            auto a = s.find_first_not_of(" \t\r\n");
            if (a == s.npos)
                return std::string{};
            return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
        };
        std::string line;
        while (std::getline(in, line))
        {
            line = trim(line);
            if (line.empty() || line[0] == '#')
                continue;
            auto pos = line.find('=');
            if (pos == line.npos)
                throw std::runtime_error("configuration requires key = path");
            auto key = trim(line.substr(0, pos)), value = trim(line.substr(pos + 1));
            if ((key != "flop" && key != "turn" && key != "river") || value.empty() || paths.contains(key))
                throw std::runtime_error("unknown, duplicate or empty configuration entry");
            auto path = std::filesystem::path(value);
            paths.emplace(key, path.is_absolute() ? path : config.parent_path() / path);
        }
        if (!in.eof())
            throw std::runtime_error("configuration read failed");
        if (paths.size() != 3)
            throw std::runtime_error("configuration requires flop, turn and river paths");
        return {paths.at("flop"), paths.at("turn"), paths.at("river")};
    }
}    // namespace blueprint
