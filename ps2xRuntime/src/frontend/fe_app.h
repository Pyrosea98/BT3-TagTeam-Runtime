#pragma once

#include <string>

namespace frontend
{
    struct FeConfig
    {
        std::string title;
        std::string exeDir;
        std::string defaultElf;
        int width = 800;
        int height = 600;
    };

    enum class FeAction
    {
        Boot,
        Quit
    };

    FeAction run(const FeConfig &cfg, std::string &bootElfOut);
}
