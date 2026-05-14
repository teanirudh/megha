#include <iostream>
#include <string>

#include "config.hpp"
#include "core/runner.hpp"

using namespace megha;

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        std::cout << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    std::string config_path = argv[1];

    try
    {
        ExperimentConfig cfg = load_experiment_config(config_path);
        ExperimentRunner::run(cfg);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
