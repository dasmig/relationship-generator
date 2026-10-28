# Relationship Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/relationship-generator/main/LICENSE.MIT)
[![CI](https://github.com/dasmig/relationship-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/relationship-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/relationship-generator.svg)](https://github.com/dasmig/relationship-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/relationship-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/relationship-generator/)

**[API Reference](https://dasmig.github.io/relationship-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/relationship-generator/releases)**

Believable families around a person: parents and grandparents, siblings and half-siblings, partners, stepchildren, children and grandchildren, with dates of birth and death and names, drawn from demography (fertility by age, marriage ages, life expectancy) for each country.

## Features

- **A Whole Family**. Parents, grandparents, siblings, half-siblings, partners (current and former, of either sex), stepchildren, children (by birth or adoption), grandchildren and twins. Each has a date of birth, whether they are alive, a date of death for the dead and a name that follows the culture's convention (two surnames in Spain, patronymics in Russia).

- **Demography, Not Templates**. The mother's age at a birth comes from the country's fertility by age, the number of siblings from the fertility rate of the birth year, marriages from the mean age at first marriage, and who is alive from a survival curve that matches the country's life expectancy. Data from UN World Population Prospects 2024, the UN World Marriage Data and the UN household database.

- **Stepparents and Households**. Ask for `stepparents` to get the partners a parent took while the anchor was a child, with the children the parent had with them and their own children from before (stepsiblings), and for `home` to get who the anchor lives with at the reference date (alone, a couple, with children, with parents, three generations), drawn from the country's household figures. Both are off by default and then change nothing else.

- **A Person You Know**. Give the anchor's name, parents, siblings or unions and the rest is built to match; `expand()` builds the family of any member of a family so that the two agree.

- **Deterministic Seeding**. Per-call `get_family(request, seed)` for reproducible results, and `family::seed()` for replaying a previous generation with the same request and data.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same result on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped data to add, change or remove entries (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_RELATIONSHIP_EMBED` to build the `lite` tier into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `relg` instances with their own data and random engine.

## Integration

[`relgen.hpp`](https://github.com/dasmig/relationship-generator/blob/main/dasmig/relgen.hpp) is the library [released here](https://github.com/dasmig/relationship-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/relationship-generator/blob/main/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/relationship-generator/tree/main/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/relgen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/relationship-generator/tree/main/resources) folder (or define `DASMIG_RELATIONSHIP_EMBED`). The library composes [birth-generator](https://github.com/dasmig/birth-generator) and [name-generator](https://github.com/dasmig/name-generator): their headers must be on the include path too, with their data (or `DASMIG_BIRTH_EMBED` and `DASMIG_NAME_EMBED`).

## Usage

```cpp
#include <dasmig/relgen.hpp>
#include <iostream>

auto& gen = dasmig::relg::instance(); // loads the lite tier

dasmig::family_request request;
request.anchor.country_code = "BR";
request.anchor.age = 40;
request.up = 2;   // grandparents too
request.down = 2; // grandchildren too

auto family = gen.get_family(request);
std::cout << family << '\n';           // BR family of 16 (anchor born 1982-08-15)
for (const auto& m : family.members)
    std::cout << dasmig::relation_name(m.role) << ": " << m.name << '\n';
std::cout << family.to_json() << '\n'; // {"country":"BR","members":[...],"edges":[...],...}

// The same seed gives the same result everywhere.
auto again = gen.get_family(request, family.seed());

// The family of one of its members, agreeing with the first.
auto theirs = gen.expand(family, family.children().front().id, dasmig::family_request{});
```

See the [usage guide](doc/usage.md) for everything else.

## Building

```bash
make            # example
make test       # tests (LIBS=.. holds the sibling checkouts of birth-generator and name-generator)
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/relgen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.3 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [birth-generator](https://github.com/dasmig/birth-generator) | 3.2 | No (headers on the include path) | The anchor, the country's age pyramid and life expectancy |
| [name-generator](https://github.com/dasmig/name-generator) | 4.5 | No (headers on the include path) | Names by culture, and the names of relatives |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## Related Libraries

| Library | Description |
|---------|-------------|
| [address-generator](https://github.com/dasmig/address-generator) | Postal addresses with real streets by place |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [device-generator](https://github.com/dasmig/device-generator) | Devices, browsers, user agents and IP addresses that fit together |
| [email-generator](https://github.com/dasmig/email-generator) | Email addresses in each country's style |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
| [interest-generator](https://github.com/dasmig/interest-generator) | Religion, hobbies and favourite media by country, age and sex |
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [occupation-generator](https://github.com/dasmig/occupation-generator) | Employment status, education and occupation by country, age and sex |
| [persona-generator](https://github.com/dasmig/persona-generator) | Complete characters composed from the other generators |
| [phone-generator](https://github.com/dasmig/phone-generator) | Phone numbers valid in each country's numbering plan |
| [trait-generator](https://github.com/dasmig/trait-generator) | Personality traits with conflicts, synergies and Big Five axes |
| [user-generator](https://github.com/dasmig/user-generator) | Complete user records: account, username, email, phone and address |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2026 Diego Dasso Migotto
```
