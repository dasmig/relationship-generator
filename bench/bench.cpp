// Benchmarks for relationship-generator; see bench.hpp for the options.

#include "../dasmig/relgen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"relationship-generator", argc, argv};

    run.load("load", [&] {
        dasmig::relg gen;
        return gen.load(run.tier());
    });

    dasmig::relg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_family", [&](std::uint64_t seed) { return gen.get_family({}, seed); });
    run.calls("get_family to_json", [&](std::uint64_t seed) {
        return gen.get_family({}, seed).to_json();
    });

    run.batches("get_family_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_family_batch({}, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}
