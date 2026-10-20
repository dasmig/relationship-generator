# Usage Guide

This guide covers every feature of the relationship-generator library. For a quick overview, see the [README](../README.md). For the data and the model behind it, see [Data and Method](data.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-a-family">Generating a Family</a></li>
<li><a href="#the-request">The Request</a></li>
<li><a href="#reading-a-family">Reading a Family</a></li>
<li><a href="#names">Names</a></li>
<li><a href="#a-person-you-know-and-their-relatives">A Person You Know and Their Relatives</a></li>
<li><a href="#expanding-a-member">Expanding a Member</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#batch-generation">Batch Generation</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/relgen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::relg::instance();
    const auto family = gen.get_family(dasmig::family_request{});
    std::cout << family << '\n';                  // BR family of 9 (anchor born 1989-03-02)
    for (const auto& m : family.members)
    {
        std::cout << dasmig::relation_name(m.role) << ": " << m.name << ", " << int{m.age} << '\n';
    }
}
```

A family is built around one person, the **anchor** (`members[0]`): the anchor's parents and grandparents, brothers and sisters (and half-siblings), partners (current and former), stepchildren, children and grandchildren, each with a date of birth, whether they are alive, a date of death for the dead, and a name.

## Installation

Copy `dasmig/relgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. The library composes [birth-generator](https://github.com/dasmig/birth-generator) (the anchor, the country's age pyramid and life expectancy) and [name-generator](https://github.com/dasmig/name-generator) (names, 4.5.0 or later), whose headers must be on the include path too. Supply `resources/` next to the program, or define `DASMIG_RELATIONSHIP_EMBED` to compile the `lite` tier in (and `DASMIG_BIRTH_EMBED` and `DASMIG_NAME_EMBED` for the others).

## Loading Resources

`instance()` loads the `lite` tier on first use (or `full` if there is no `lite`), from the compiled-in copy or from the first of `resources/`, `../resources`, `relationship-generator/resources` and `../relationship-generator/resources` that has it. It draws from the `birth-generator` and `name-generator` singletons.

```cpp
dasmig::relg gen;                          // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite); // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();        // this generator's tables and birth-generator's data
gen.has_family_data(); // this generator's tables only
gen.countries();       // the countries with figures of their own
```

Each `load` replaces the data and every layer loaded before. To draw from other instances of the libraries it composes, construct the generator with them (they must outlive it):

```cpp
dasmig::bthg births; // loaded elsewhere
dasmig::ng names;
dasmig::relg gen{dasmig::relg::sources{.birth = &births, .name = &names}};
```

## Generating a Family

```cpp
auto v = gen.get_family(dasmig::family_request{});         // draws a seed from the instance engine
auto w = gen.get_family(dasmig::family_request{}, 1234);   // deterministic; does not touch the engine

v.anchor();        // members[0]
v.parents();       // father then mother
v.siblings();      // full brothers and sisters, oldest first
v.half_siblings();
v.grandparents();  // father's side first
v.partners();      // in the order the unions began
v.stepchildren();  // the partners' children by earlier unions
v.children();
v.grandchildren();
v.with_role(dasmig::relation::mother);
v.union_with(partner_id); // the union (start, end, how it ended) with a partner
```

`family_request{}` asks for the defaults: a random anchor, parents, siblings, partners, children and names.

## The Request

```cpp
dasmig::family_request request;
request.anchor.country_code = "BR";               // ISO 3166-1 alpha-2, any case; drawn by population if omitted
request.anchor.bio_sex = dasmig::sex::female;     // drawn if omitted
request.anchor.age = 50;                          // completed years at the reference date (0 to 122), or...
request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1973}, std::chrono::month{5}, std::chrono::day{20}};
request.up = 2;           // generations above: 0 none, 1 parents, 2 grandparents too
request.down = 2;         // generations below: 0 none, 1 children, 2 grandchildren too
request.siblings = true;  // brothers, sisters and half-siblings (needs up >= 1)
request.partners = true;  // the partners, current and former
request.names = true;     // give everyone a name
request.name_culture = dasmig::ng::to_culture(L"es"); // names of another culture than the country's
request.weighted = true;  // draw an omitted country by population (false: uniformly)
request.reference_date = {std::chrono::year{2023}, std::chrono::July, std::chrono::day{1}}; // ages and who is alive are counted at this date
```

- Whatever the anchor leaves out is drawn. A fixed `birth` wins over `age`. A fixed birth with a country and a sex does not consult `birth-generator`, so such an anchor is whoever the caller says.
- The anchor is always alive. An `age` outside 0 to 122, a `birth` after the reference date or one that makes the anchor older than 122, or an unknown country code throws `std::invalid_argument`.
- `reference_date` defaults to 1 July 2023, the date of the UN estimates the data comes from. The age pyramid and life expectancy are those of 2023 whatever the date, so a date far from it gives families that are valid in themselves but whose demography is today's.
- With `partners = false` the partners are left out but the children remain (their other parent is not listed). Stepchildren need `partners = true` and `down >= 1`.
- Choices are never kept on the generator: one call cannot change the next.

## Reading a Family

Every `person` has an `id` (its index in `family::members`), `role` (`relation`), `bio_sex`, `given`, `patronymic`, `family` and `name`, `birth`, `alive`, `age` (in completed years at the reference date, or at death), `death` for the dead and `twin`.

```cpp
for (const auto& e : family.edges)
{
    switch (e.kind)
    {
    case dasmig::edge_kind::parent_of:  // e.from is a parent of e.to; e.genetic is false for adoption
    case dasmig::edge_kind::sibling_of: // they share a parent; e.from is the older; e.full if both are shared
    case dasmig::edge_kind::partner_of: // e.from is the anchor; e.start, e.end (years), e.ended
        break;
    }
}
```

- Siblings are linked through any shared parent, so half-siblings, stepchildren and a person's children by different unions need no special case: `full` is true when both parents are shared. A parent who is not a member (the other parent of a half-sibling, a grandchild's other parent) is left out of the edges.
- Each kind of member is numbered oldest first.
- Twins are full siblings born on the same day; each has `twin` set.
- `family::country_code`, `reference_date`, `fertility_source` and `union_source` say where the figures came from (the country's own row, its subregion's median or the world's).

## Names

Names come from name-generator, by the culture of the anchor's country: a parent is named after the child, a sibling, child, stepchild or grandchild after both parents, a partner is drawn and a wife may take the anchor's family name, each by the culture's convention (two surnames in Spain and Portugal, patronymics in Russia and Iceland, a family name first in China, Japan and Korea). A country whose culture has no lists in the loaded tier gets no names, and `names = false` gives none.

`name_culture` makes the whole family follow another culture than the anchor's country's, for a family of immigrants or a country without a culture of its own.

## A Person You Know and Their Relatives

The anchor can be a person you already have. Give the name and the family is built to match:

```cpp
dasmig::family_request request;
request.anchor.country_code = "ES";
request.anchor.bio_sex = dasmig::sex::male;
request.anchor.age = 35;
request.anchor.name = dasmig::person_name{.given = "Juan", .patronymic = "", .family = "Garcia Perez"};
auto family = gen.get_family(request, 7);
// The father is "... Garcia ...", the mother "... Perez ...", the siblings "... Garcia Perez".
```

`person_name::family` is the family name; two words are two surnames in a culture with two (Spain, Portugal). Without parents' names, the parents follow the anchor's; with them, the anchor follows the parents.

You can also give the relatives you know. A relative that is given is used as it is, and a list that is given (even an empty one) is the whole list:

```cpp
dasmig::known_person mother;
mother.bio_sex = dasmig::sex::female;
mother.birth = std::chrono::year_month_day{std::chrono::year{1962}, std::chrono::month{3}, std::chrono::day{2}};
mother.death = std::chrono::year_month_day{std::chrono::year{2015}, std::chrono::month{8}, std::chrono::day{9}};
mother.name = dasmig::person_name{.given = "Greta", .patronymic = "", .family = "Weber"};

request.anchor.mother = mother;                                     // the father is drawn around her
request.anchor.siblings = std::vector<dasmig::known_sibling>{};     // no siblings or half-siblings
request.anchor.unions = std::vector<dasmig::known_union>{};         // no partners or children
```

| Field | Meaning |
| --- | --- |
| `name` | The anchor's name |
| `father`, `mother` | A parent; the other is drawn at the usual gap. `known_person::parents` holds that person's own parents (the grandparents) |
| `siblings` | `known_sibling` (`person`, `full`, and for a half-sibling the shared parent's sex `through`); none are drawn when set |
| `unions` | `known_union` (`partner`, `start`, `end`, `children`, `stepchildren`); none are drawn when set. A union without a `partner` is known by its children alone |

A known person has `bio_sex`, `birth`, an optional `death` (before the reference date; later is read as alive) and `name`. A known parent born after the anchor throws `std::invalid_argument`.

## Expanding a Member

Any member of a family can have a family of their own that agrees with the first:

```cpp
auto base = gen.get_family(request, 1);
auto kid = base.children().front();

dasmig::family_request around;
around.up = 2;
around.down = 1;
auto theirs = gen.expand(base, kid.id, around, 99);
// theirs.anchor() is the child; the parents are the anchor and the partner of `base`,
// the siblings are the other children, with the same names and dates.
```

The member keeps their birth, sex, name and country, and the relatives `base` lists: parents (and grandparents), siblings and half-siblings, partners, stepchildren and children. The rest is drawn. The request's `anchor` and `reference_date` are ignored (the member and the family's date are used). A member whose partners or children `base` did not list (a family made with `partners = false` or `down = 0`) has them drawn again. See [Data and Method](data.md) for what is kept.

`expand(base, member, request)` without a seed draws one from the instance engine.

## Seeding and Deterministic Generation

- `get_family(request, seed)` gives the same result for the same request, seed and data, on every platform, without advancing the instance engine.
- `get_family(request)` draws a fresh seed from the instance engine, initially seeded from system entropy. There are no generator-level `seed()` or `unseed()` controls; for deterministic sequences, keep a `dasmig::core::rng` in the caller and pass its next value as the seed.
- `v.seed()` is the seed a result was generated from: `get_family(request, v.seed())` replays it with the same request and data.
- Everything is drawn from streams split from the seed by name (`"parents"`, `"siblings"`, `"unions"`, `"children"`, `"names"`, ...), so asking for no siblings or no names leaves the other draws as they were.

## Extending Data

The shipped data is a set of layered tables: `regions.tsv`, `fertility.tsv`, `unions.tsv`, `households.tsv`, `defaults.tsv` and `parameters.tsv` (see [Data and Method](data.md)). Files with the same columns can be loaded on top of them to add rows, change them or remove them. Rows are keyed by `cca2`, `subregion` or `key`.

```cpp
// A directory may hold any of the table files; a single file must have one of those names.
gen.extend("mods/");
gen.extend("mods/parameters.tsv", dasmig::core::layer_mode::add);

// From text, for example supplied by a WebAssembly host.
gen.extend_from_string(dasmig::family_table::parameters, "key\tvalue\nmax_siblings\t4\n", "scenario");

// Remove a row: a layer needs only the key and a disabled column.
gen.extend_from_string(dasmig::family_table::fertility, "cca2\tdisabled\nBR\t1\n", "cleanup");

gen.layers(dasmig::family_table::parameters); // what is loaded: source, mode, rows added, replaced, removed, warnings
```

- Every value in a layer is checked before anything changes. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was.
- A country removed from `fertility.tsv` falls back to its subregion's row in `defaults.tsv`, and the family says so (`fertility_source`).
- `load(...)` replaces the base and drops every layer.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto v = gen.get_family(dasmig::family_request{});
v.signature().seed; // the call seed
v.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume. Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

```json
{"country":"BR","reference_date":"2023-07-01",
 "members":[{"id":0,"role":"anchor","sex":"female","name":{"given":"Gisele","family":"Alves Da Silva Carolina","full":"Gisele Alves Da Silva Carolina"},"birth":"1982-08-15","alive":true,"age":40}, ...],
 "edges":[{"kind":"parent_of","from":1,"to":0}, {"kind":"sibling_of","from":0,"to":3,"full":true},
          {"kind":"partner_of","from":0,"to":12,"start":2007,"ended":"ongoing"}, ...],
 "sources":{"fertility":"country","unions":"country"},
 "signature":{"seed":"0000000000000001","data":"..."}}
```

A member that is a twin has `"twin":true`; the dead have `death_age` and `death` in place of `age`.

## Compiled-in Data

Define `DASMIG_RELATIONSHIP_EMBED` before including the header to compile the `lite` tier into the program. `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same `data_hash()` as the file, so seeds replay the same either way.

`dasmig/relgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Thread Safety

`get_family(request, seed)`, `expand(base, member, request, seed)` and the batch getters are const and may run concurrently on one instance. Calls that draw from the instance engine (`get_family(request)`, `expand(base, member, request)`), `load` and `extend` need external synchronisation (e.g. a `std::mutex`) on the same instance.

## Batch Generation

Every seeded getter has batch forms: `get_family_batch` returns a `std::vector`, and `for_each_family` streams the items to a sink in index order without holding the batch.

```cpp
auto& gen = dasmig::relg::instance();
const auto request = dasmig::family_request{};

// 1,000 results from one batch seed, on four threads.
auto items = gen.get_family_batch(request, {.count = 1000, .threads = 4}, 42);

// Item 1 replays on its own: the batch seed and the index give its seed.
auto again = gen.get_family(request, dasmig::core::item_seed(42, 1));

// A million results to a file as JSON Lines, never all in memory.
std::ofstream out{"items.jsonl"};
dasmig::core::jsonl_writer lines{out};
gen.for_each_family(request, {.count = 1'000'000, .threads = 0}, 42, lines);
```

- **Item seeds.** Item `i` is exactly `get_family(request, core::item_seed(batch_seed, first + i))`. A range can be generated directly (`first = 1000` starts at item 1000), two batches that follow each other equal one, and the output is the same for any `threads` (0 means the hardware count). Without a `batch_seed` one is drawn like the single getter draws its own.
- **Options.** `core::batch_options` has `count`, `first`, `threads` and `unique`. The request describes one item; the options describe the batch.
- **Uniqueness.** `.unique = core::unique_rule::by<family>(key)` regenerates an item whose key already appeared in the batch, with a deterministic retry seed, and throws `std::runtime_error` naming the item when the attempts run out.
- **Sinks.** `core::jsonl_writer` writes one `to_json()` per line, `core::csv_writer` flattens each result's JSON into dotted columns. Any callable `(std::uint64_t index, family&)` works.
- Batch calls use the same const getters from several threads, which the thread-safety rules above allow; the CI runs the batch tests under ThreadSanitizer.

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error` | Any generation call before data is loaded (this library's or birth-generator's); names requested while name-generator has no data |
| `std::invalid_argument` | `up` or `down` outside 0 to 2; an anchor `age` outside 0 to 122; a `birth` after the reference date or making the anchor older than 122; an unknown country code; a known person with an empty given name, a birth that is not a date, a death before the birth, or a parent not born before the child; a union known to end before it begins; `expand` with a member that is not in the family |
| `dasmig::core::tsv_error` | A malformed table or layer (message gives file, line and column); the data is unchanged |
