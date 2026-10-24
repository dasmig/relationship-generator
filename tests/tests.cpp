#include "catch_amalgamated.hpp"
#include "../dasmig/relgen.hpp"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>

using dasmig::family;
using dasmig::family_request;
using dasmig::person;
using dasmig::relation;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static dasmig::relg make_loaded_generator()
{
    dasmig::relg gen;
    if (!gen.load(dasmig::dataset::lite))
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(std::filesystem::path{candidate});
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

static std::int64_t days(const std::chrono::year_month_day& d)
{
    return std::chrono::sys_days{d}.time_since_epoch().count();
}

static double age_years(const std::chrono::year_month_day& from, const std::chrono::year_month_day& to)
{
    return static_cast<double>(days(to) - days(from)) / 365.2425;
}

static const person& member(const family& f, relation r)
{
    for (const auto& m : f.members)
    {
        if (m.role == r) return m;
    }
    FAIL("no member with the role " << dasmig::relation_name(r));
    return f.members.front();
}

static std::string json_of(const family& f) { return f.to_json(); }

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads the tables", "[loading]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.has_family_data());
    REQUIRE(gen.layers(dasmig::family_table::fertility).size() == 1);
    REQUIRE(gen.layers(dasmig::family_table::parameters).size() == 1);
    REQUIRE(gen.data_hash() != 0);
    const auto countries = gen.countries();
    REQUIRE(countries.size() > 150);
    REQUIRE(std::ranges::find(countries, "BR") != countries.end());
}

TEST_CASE("load - non-existent path is a no-op", "[loading]")
{
    dasmig::relg gen;
    gen.load(std::filesystem::path{"this/path/does/not/exist"});
    REQUIRE_FALSE(gen.has_family_data());
    REQUIRE(gen.data_hash() == 0);
}

TEST_CASE("get_family throws when no data is loaded", "[generation]")
{
    dasmig::relg gen;
    REQUIRE_THROWS_AS(gen.get_family(family_request{}), std::runtime_error);
}

TEST_CASE("the singleton loads data on first use", "[loading]")
{
    REQUIRE(dasmig::relg::instance().has_data());
}

// ---------------------------------------------------------------------------
// Determinism and structure
// ---------------------------------------------------------------------------

TEST_CASE("get_family with a seed is deterministic", "[generation][seed]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.up = 2;
    for (std::uint64_t seed : {1ULL, 2ULL, 42ULL, 123456789ULL})
    {
        const auto a = gen.get_family(request, seed);
        const auto b = gen.get_family(request, seed);
        REQUIRE(json_of(a) == json_of(b));
        REQUIRE(a.seed() == seed);
    }
    REQUIRE(json_of(gen.get_family(request, 1)) != json_of(gen.get_family(request, 2)));
}

TEST_CASE("explicit per-call seeds replay across generator instances", "[seed]")
{
    const auto one = make_loaded_generator();
    const auto two = make_loaded_generator();
    REQUIRE(json_of(one.get_family(family_request{}, 99)) == json_of(two.get_family(family_request{}, 99)));
}

TEST_CASE("up decides which generations are present", "[generation]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "FR";
    request.anchor.age = 40;
    request.down = 0;
    request.partners = false;

    request.up = 0;
    const auto alone = gen.get_family(request, 7);
    REQUIRE(alone.members.size() == 1);
    REQUIRE(alone.edges.empty());
    REQUIRE(alone.anchor().role == relation::anchor);

    request.up = 1;
    const auto parents = gen.get_family(request, 7);
    REQUIRE(parents.parents().size() == 2);
    REQUIRE(parents.grandparents().empty());

    request.up = 2;
    const auto grand = gen.get_family(request, 7);
    REQUIRE(grand.grandparents().size() == 4);
    REQUIRE(grand.members.size() == parents.members.size() + 4);
    for (std::size_t i = 0; i < grand.members.size(); ++i) REQUIRE(grand.members[i].id == i);

    request.up = 3;
    REQUIRE_THROWS_AS(gen.get_family(request, 7), std::invalid_argument);
    request.up = -1;
    REQUIRE_THROWS_AS(gen.get_family(request, 7), std::invalid_argument);
}

TEST_CASE("asking for more does not change what is already drawn", "[generation][streams]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 30;
    request.down = 0;
    request.partners = false;
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
        request.up = 1;
        const auto small = gen.get_family(request, seed);
        request.up = 2;
        const auto big = gen.get_family(request, seed);
        for (const auto role : {relation::anchor, relation::father, relation::mother, relation::sibling,
                                relation::half_sibling})
        {
            const auto x = small.with_role(role);
            const auto y = big.with_role(role);
            REQUIRE(x.size() == y.size());
            for (std::size_t i = 0; i < x.size(); ++i)
            {
                REQUIRE(x[i].birth == y[i].birth);
                REQUIRE(x[i].alive == y[i].alive);
                REQUIRE(x[i].name == y[i].name);
            }
        }
        // Without siblings the parents are the same people.
        request.up = 1;
        request.siblings = false;
        const auto only = gen.get_family(request, seed);
        request.siblings = true;
        REQUIRE(only.siblings().empty());
        REQUIRE(member(only, relation::mother).birth == member(small, relation::mother).birth);
        REQUIRE(member(only, relation::father).birth == member(small, relation::father).birth);
        REQUIRE(member(only, relation::mother).alive == member(small, relation::mother).alive);
    }
}

TEST_CASE("a fixed anchor is kept", "[generation][anchor]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "JP";
    request.anchor.bio_sex = dasmig::sex::female;
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1990}, std::chrono::March, std::chrono::day{14}};
    const auto f = gen.get_family(request, 5);
    REQUIRE(f.country_code == "JP");
    REQUIRE(f.anchor().bio_sex == dasmig::sex::female);
    REQUIRE(f.anchor().birth == *request.anchor.birth);
    REQUIRE(f.anchor().age == 33);
    REQUIRE(f.anchor().alive);

    // A birth date without a country draws the rest around it.
    request.anchor = {};
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1975}, std::chrono::June, std::chrono::day{2}};
    const auto g = gen.get_family(request, 6);
    REQUIRE(g.anchor().birth == *request.anchor.birth);

    // An age is the completed years at the reference date.
    request.anchor = {};
    request.anchor.age = 61;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        REQUIRE(gen.get_family(request, seed).anchor().age == 61);
    }

    request.anchor = {};
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{2030}, std::chrono::January, std::chrono::day{1}};
    request.anchor.country_code = "FR";
    request.anchor.bio_sex = dasmig::sex::male;
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{2001}, std::chrono::February, std::chrono::day{30}};
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
}

TEST_CASE("another reference date moves who is alive and the ages", "[generation]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "DE";
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1980}, std::chrono::May, std::chrono::day{5}};
    request.anchor.bio_sex = dasmig::sex::male;
    request.reference_date = std::chrono::year_month_day{std::chrono::year{2000}, std::chrono::May, std::chrono::day{5}};
    const auto f = gen.get_family(request, 3);
    REQUIRE(f.anchor().age == 20);
    REQUIRE(f.reference_date == request.reference_date);
}

// ---------------------------------------------------------------------------
// Consistency (property tests)
// ---------------------------------------------------------------------------

static void check_family(const family& f)
{
    const auto ref = days(f.reference_date);
    constexpr std::int64_t nine_months = 273;
    for (const auto& m : f.members)
    {
        REQUIRE(days(m.birth) <= ref);
        REQUIRE(m.alive == !m.death.has_value());
        if (m.death)
        {
            REQUIRE(days(*m.death) >= days(m.birth));
            REQUIRE(days(*m.death) <= ref);
        }
    }
    REQUIRE(f.anchor().alive);

    // Each kind of member is numbered oldest first.
    for (const auto role : {relation::sibling, relation::child, relation::grandchild, relation::stepsibling})
    {
        const auto same = f.with_role(role);
        for (std::size_t i = 1; i < same.size(); ++i)
        {
            REQUIRE(days(same[i - 1].birth) <= days(same[i].birth));
            REQUIRE(same[i - 1].id < same[i].id);
        }
    }
    // Half-siblings too, but for the children of a parent and a stepparent,
    // who come after the stepparents; half_siblings() is oldest first.
    const auto halves = f.half_siblings();
    for (std::size_t i = 1; i < halves.size(); ++i) REQUIRE(days(halves[i - 1].birth) <= days(halves[i].birth));

    std::map<std::size_t, std::vector<std::size_t>> parents_of;
    std::map<std::size_t, std::vector<std::int64_t>> births_of; // a mother's births, by day
    for (const auto& e : f.edges)
    {
        if (e.kind == dasmig::edge_kind::parent_of)
        {
            parents_of[e.to].push_back(e.from);
            const auto& parent = f.members.at(e.from);
            const auto& child = f.members.at(e.to);
            REQUIRE(days(parent.birth) < days(child.birth));
            if (!e.genetic) continue; // an adopted child has no such rules
            const double gap = age_years(parent.birth, child.birth);
            if (parent.bio_sex == dasmig::sex::female)
            {
                REQUIRE(gap >= 11.9);
                REQUIRE(gap < 50.1);
                // A mother is alive at the birth.
                if (parent.death) REQUIRE(days(*parent.death) >= days(child.birth));
                births_of[e.from].push_back(days(child.birth));
            }
            else
            {
                REQUIRE(gap >= 14.9);
                // A father is alive nine months before it.
                if (parent.death) REQUIRE(days(*parent.death) >= days(child.birth) - nine_months);
            }
        }
        else if (e.kind == dasmig::edge_kind::sibling_of)
        {
            REQUIRE(e.from != e.to);
            REQUIRE(days(f.members.at(e.from).birth) <= days(f.members.at(e.to).birth));
            // They share a parent; full siblings share the same ones.
            std::set<std::size_t> a(parents_of[e.from].begin(), parents_of[e.from].end());
            std::set<std::size_t> b(parents_of[e.to].begin(), parents_of[e.to].end());
            bool shared = false;
            for (const auto p : a) shared = shared || b.contains(p);
            // Siblings through an unlisted parent share none that is listed.
            if (e.full) REQUIRE(a == b);
            else REQUIRE((shared || !a.empty() || !b.empty()));
        }
    }
    // A mother's births are nine months apart, but for twins, born together.
    for (auto& [mother, births] : births_of)
    {
        std::ranges::sort(births);
        for (std::size_t i = 1; i < births.size(); ++i)
        {
            if (births[i] != births[i - 1]) REQUIRE(births[i] - births[i - 1] >= nine_months - 1);
        }
    }

    // Unions: begun when both were adults, ended by a separation while both
    // lived or by the partner's death, ongoing only while the partner is alive.
    // A parent's union with a stepparent has tests of its own.
    for (const auto& e : f.edges)
    {
        if (e.kind != dasmig::edge_kind::partner_of) continue;
        const auto& partner = f.members.at(e.to);
        if (e.from != 0)
        {
            REQUIRE((f.members.at(e.from).role == relation::father || f.members.at(e.from).role == relation::mother));
            REQUIRE((partner.role == relation::stepfather || partner.role == relation::stepmother));
            continue;
        }
        REQUIRE(*e.start - static_cast<int>(f.anchor().birth.year()) >= 15);
        REQUIRE(*e.start - static_cast<int>(partner.birth.year()) >= 15);
        REQUIRE(*e.start <= static_cast<int>(f.reference_date.year()));
        if (e.ended == dasmig::union_end::ongoing)
        {
            REQUIRE_FALSE(e.end.has_value());
            REQUIRE(partner.alive);
        }
        else
        {
            REQUIRE(*e.end >= *e.start);
            if (e.ended == dasmig::union_end::death)
            {
                REQUIRE_FALSE(partner.alive);
                REQUIRE(static_cast<int>(partner.death->year()) == *e.end);
            }
            else if (partner.death)
            {
                REQUIRE(static_cast<int>(partner.death->year()) >= *e.end);
            }
        }
    }
    // Unions do not overlap and come in order.
    int previous_end = -10000;
    for (const auto& e : f.edges)
    {
        if (e.kind != dasmig::edge_kind::partner_of || e.from != 0) continue;
        REQUIRE(*e.start >= previous_end);
        previous_end = e.end ? *e.end : 10000;
    }
}

TEST_CASE("every family is consistent, in every country", "[property]")
{
    const auto gen = make_loaded_generator();
    const auto countries = gen.countries();
    family_request request;
    request.up = 2;
    std::uint64_t seed = 1;
    for (const auto& country : countries)
    {
        request.anchor.country_code = country;
        for (int i = 0; i < 12; ++i)
        {
            // Skip the countries birth-generator has no data for.
            try
            {
                check_family(gen.get_family(request, seed++));
            }
            catch (const std::invalid_argument&)
            {
                break;
            }
        }
    }
    request.anchor = {};
    for (int i = 0; i < 3000; ++i) check_family(gen.get_family(request, seed++));
    // And with the stepfamily: stepparents, their children and the stepsiblings.
    request.stepparents = true;
    request.home = true;
    for (int i = 0; i < 3000; ++i) check_family(gen.get_family(request, seed++));
}

TEST_CASE("every family is consistent at the extremes of age", "[property]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.up = 2;
    request.anchor.country_code = "BR";
    std::uint64_t seed = 1000;
    for (const int age : {0, 1, 17, 50, 80, 100})
    {
        request.anchor.age = age;
        for (int i = 0; i < 300; ++i) check_family(gen.get_family(request, seed++));
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

namespace
{
struct summary
{
    double siblings{0.0};
    double mother_age{0.0};
    double mother_alive{0.0};
    double father_older{0.0};
};

summary measure(const dasmig::relg& gen, const std::string& country, int age, int n)
{
    family_request request;
    request.anchor.country_code = country;
    request.anchor.age = age;
    summary s;
    for (int i = 0; i < n; ++i)
    {
        const auto f = gen.get_family(request, 10 + static_cast<std::uint64_t>(i));
        s.siblings += static_cast<double>(f.siblings().size());
        const auto& mother = member(f, relation::mother);
        const auto& father = member(f, relation::father);
        s.mother_age += age_years(mother.birth, f.anchor().birth);
        s.mother_alive += mother.alive ? 1.0 : 0.0;
        s.father_older += age_years(father.birth, mother.birth);
    }
    s.siblings /= n;
    s.mother_age /= n;
    s.mother_alive /= n;
    s.father_older /= n;
    return s;
}
} // namespace

TEST_CASE("family size follows the fertility of the country and the time", "[stats]")
{
    const auto gen = make_loaded_generator();
    const auto niger = measure(gen, "NE", 25, 3000);
    const auto japan = measure(gen, "JP", 25, 3000);
    const auto japan_old = measure(gen, "JP", 70, 3000);
    // Niger's total fertility rate was above 7 around 1998; Japan's near 1.4.
    REQUIRE(niger.siblings > 4.5);
    REQUIRE(japan.siblings > 0.6);
    REQUIRE(japan.siblings < 1.4);
    // Japanese born around 1953 came from larger families (TFR about 2.0 to 2.2).
    REQUIRE(japan_old.siblings > japan.siblings + 0.3);
    // Seen from a child, family size is size-biased: more siblings than TFR - 1.
    REQUIRE(niger.siblings > 6.0 - 1.0);
}

TEST_CASE("mothers are about as old as the age at childbearing says", "[stats]")
{
    const auto gen = make_loaded_generator();
    for (const std::string country : {"BR", "IN", "FR", "NG"})
    {
        const auto s = measure(gen, country, 25, 2500);
        REQUIRE(s.mother_age > 22.0);
        REQUIRE(s.mother_age < 32.0);
        // Fathers are older than mothers by a few years.
        REQUIRE(s.father_older > 0.5);
        REQUIRE(s.father_older < 8.0);
    }
}

TEST_CASE("who is alive follows the age of the anchor", "[stats]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(measure(gen, "JP", 20, 2000).mother_alive > 0.97);
    REQUIRE(measure(gen, "JP", 55, 2000).mother_alive > 0.45);
    REQUIRE(measure(gen, "JP", 55, 2000).mother_alive < 0.85);
    REQUIRE(measure(gen, "JP", 85, 2000).mother_alive < 0.05);
    // The same age, a country with a lower life expectancy.
    REQUIRE(measure(gen, "NG", 55, 2000).mother_alive < measure(gen, "JP", 55, 2000).mother_alive);
}

// ---------------------------------------------------------------------------
// Data layers
// ---------------------------------------------------------------------------

TEST_CASE("a parameters layer changes the model", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto base_hash = gen.data_hash();
    gen.extend_from_string(dasmig::family_table::parameters, "key\tvalue\nmax_siblings\t0\n", "no-siblings.tsv");
    REQUIRE(gen.data_hash() != base_hash);
    family_request request;
    request.anchor.country_code = "NE";
    request.anchor.age = 20;
    for (std::uint64_t seed = 1; seed <= 50; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        REQUIRE(f.siblings().empty());
        REQUIRE(f.signature().data == gen.data_hash());
    }
}

TEST_CASE("a country without a row uses its subregion, then the world", "[extend][fallback]")
{
    auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 30;
    REQUIRE(gen.get_family(request, 1).fertility_source == dasmig::data_source::country);

    gen.extend_from_string(dasmig::family_table::fertility, "cca2\tdisabled\nBR\t1\n", "no-br.tsv");
    const auto sub = gen.get_family(request, 1);
    REQUIRE(sub.fertility_source == dasmig::data_source::subregion);
    REQUIRE(sub.union_source == dasmig::data_source::country);
    check_family(sub);

    gen.extend_from_string(dasmig::family_table::regions, "cca2\tdisabled\nBR\t1\n", "no-region.tsv");
    const auto world = gen.get_family(request, 1);
    REQUIRE(world.fertility_source == dasmig::data_source::world);
    check_family(world);
    REQUIRE(json_of(world).find("\"fertility\":\"world\"") != std::string::npos);
}

TEST_CASE("a fertility layer overrides a country's figures", "[extend]")
{
    auto gen = make_loaded_generator();
    // One age group only: every mother is 30 to 34 at every birth.
    gen.extend_from_string(dasmig::family_table::fertility,
                           "cca2\ttfr\tmac\tpasfr_10_14\tpasfr_15_19\tpasfr_20_24\tpasfr_25_29\tpasfr_30_34\t"
                           "pasfr_35_39\tpasfr_40_44\tpasfr_45_49\n"
                           "BR\t2\t32.5\t0\t0\t0\t0\t100\t0\t0\t0\n",
                           "thirties.tsv");
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 20;
    for (std::uint64_t seed = 1; seed <= 100; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        const double a = age_years(member(f, relation::mother).birth, f.anchor().birth);
        REQUIRE(a >= 29.9);
        REQUIRE(a < 35.1);
    }
}

TEST_CASE("a bad layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::family_table::parameters, "key\tvalue\nmax_sibling\t3\n", "typo.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::family_table::parameters, "key\tvalue\nmax_siblings\t-1\n", "neg.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::family_table::fertility,
                                             "cca2\ttfr\tmac\tpasfr_10_14\tpasfr_15_19\tpasfr_20_24\tpasfr_25_29\t"
                                             "pasfr_30_34\tpasfr_35_39\tpasfr_40_44\tpasfr_45_49\n"
                                             "BR\t2\t30\t0\t0\t0\t0\t0\t0\t0\t0\n",
                                             "zeros.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(dasmig::family_table::unions, "cca2\tsmam_m\nBR\tmany\n", "text.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend(std::filesystem::path{"somewhere/other.tsv"}), std::invalid_argument);
    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.has_data());
}

// ---------------------------------------------------------------------------
// Partners, children, half-siblings and names
// ---------------------------------------------------------------------------

TEST_CASE("down decides which generations below the anchor are present", "[generation]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 62;
    request.anchor.bio_sex = dasmig::sex::female;
    request.up = 0;

    request.down = 0;
    const auto none = gen.get_family(request, 3);
    REQUIRE(none.children().empty());
    REQUIRE(none.grandchildren().empty());

    request.down = 1;
    bool grandchildren_seen = false;
    for (std::uint64_t seed = 1; seed <= 30; ++seed)
    {
        request.down = 1;
        const auto kids = gen.get_family(request, seed);
        REQUIRE(kids.grandchildren().empty());
        request.down = 2;
        const auto grand = gen.get_family(request, seed);
        REQUIRE(grand.children().size() == kids.children().size());
        grandchildren_seen = grandchildren_seen || !grand.grandchildren().empty();
        check_family(grand);
    }
    REQUIRE(grandchildren_seen);

    request.down = 3;
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
}

TEST_CASE("partners, names and the other generations do not change each other", "[generation][streams]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 45;
    request.down = 2;
    const auto births = [](const std::vector<person>& people)
    {
        std::vector<std::int64_t> out;
        for (const auto& p : people) out.push_back(days(p.birth));
        return out;
    };
    for (std::uint64_t seed = 1; seed <= 40; ++seed)
    {
        request.up = 2;
        request.partners = true;
        request.names = true;
        const auto full = gen.get_family(request, seed);

        request.partners = false;
        const auto no_partners = gen.get_family(request, seed);
        REQUIRE(no_partners.partners().empty());
        REQUIRE(births(no_partners.children()) == births(full.children()));
        REQUIRE(births(no_partners.grandchildren()) == births(full.grandchildren()));
        REQUIRE(births(no_partners.siblings()) == births(full.siblings()));

        request.partners = true;
        request.names = false;
        const auto nameless = gen.get_family(request, seed);
        REQUIRE(nameless.members.size() == full.members.size());
        for (std::size_t i = 0; i < full.members.size(); ++i)
        {
            REQUIRE(nameless.members[i].name.empty());
            REQUIRE(nameless.members[i].birth == full.members[i].birth);
            REQUIRE(nameless.members[i].alive == full.members[i].alive);
        }

        request.names = true;
        request.up = 0;
        const auto no_parents = gen.get_family(request, seed);
        REQUIRE(births(no_parents.children()) == births(full.children()));
        REQUIRE(births(no_parents.partners()) == births(full.partners()));

        request.up = 1;
        request.siblings = false;
        const auto only_child = gen.get_family(request, seed);
        request.siblings = true;
        REQUIRE(only_child.siblings().empty());
        REQUIRE(only_child.half_siblings().empty());
        REQUIRE(births(only_child.children()) == births(full.children()));
    }
}

TEST_CASE("unions follow the anchor's age and the country's marriages", "[stats][unions]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.up = 0;
    request.down = 0;
    request.names = false;
    request.anchor.country_code = "US";
    const auto partnered = [&](int age, int n)
    {
        request.anchor.age = age;
        int count = 0;
        int ended = 0;
        for (int i = 0; i < n; ++i)
        {
            const auto f = gen.get_family(request, 500 + static_cast<std::uint64_t>(i));
            count += f.partners().empty() ? 0 : 1;
            for (const auto& e : f.edges)
            {
                if (e.kind == dasmig::edge_kind::partner_of && e.ended != dasmig::union_end::ongoing) ++ended;
            }
        }
        return std::pair{static_cast<double>(count) / n, static_cast<double>(ended) / n};
    };
    // Nobody has a partner before the youngest age of a union.
    REQUIRE(partnered(15, 300).first == 0.0);
    const auto young = partnered(22, 1500);
    const auto middle = partnered(50, 1500);
    REQUIRE(young.first < 0.6);
    REQUIRE(middle.first > 0.75);
    REQUIRE(middle.first < 0.97);
    // Some unions end, more of them over a longer life.
    REQUIRE(middle.second > 0.15);
    REQUIRE(middle.second > young.second);
}

TEST_CASE("children follow the fertility of the country", "[stats][children]")
{
    const auto gen = make_loaded_generator();
    const auto average = [&](const std::string& country, int age)
    {
        family_request request;
        request.up = 0;
        request.names = false;
        request.anchor.country_code = country;
        request.anchor.age = age;
        double total = 0.0;
        const int n = 1500;
        for (int i = 0; i < n; ++i)
        {
            total += static_cast<double>(gen.get_family(request, 900 + static_cast<std::uint64_t>(i)).children().size());
        }
        return total / n;
    };
    REQUIRE(average("NE", 50) > 6.0);
    REQUIRE(average("DE", 50) < 1.6);
    REQUIRE(average("DE", 50) > 0.8);
    REQUIRE(average("US", 50) > 1.5);
    REQUIRE(average("US", 50) < 2.4);
    REQUIRE(average("US", 20) < 0.3);
    REQUIRE(average("US", 50) > average("US", 28));
}

TEST_CASE("half-siblings share one parent", "[generation][half]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 30;
    request.partners = false;
    request.down = 0;
    int with_half = 0;
    for (std::uint64_t seed = 1; seed <= 400; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        const auto halves = f.half_siblings();
        if (halves.empty()) continue;
        ++with_half;
        for (const auto& h : halves)
        {
            int listed = 0;
            for (const auto& e : f.edges)
            {
                if (e.kind == dasmig::edge_kind::parent_of && e.to == h.id)
                {
                    ++listed;
                    REQUIRE(e.from <= 2); // the father or the mother
                    REQUIRE(e.genetic);
                }
            }
            REQUIRE(listed == 1);
            // Half-siblings are linked to the anchor, not as full siblings.
            bool linked = false;
            for (const auto& e : f.edges)
            {
                if (e.kind == dasmig::edge_kind::sibling_of &&
                    ((e.from == 0 && e.to == h.id) || (e.to == 0 && e.from == h.id)))
                {
                    linked = true;
                    REQUIRE_FALSE(e.full);
                }
            }
            REQUIRE(linked);
        }
        check_family(f);
    }
    // About a quarter of Americans have a half-sibling.
    REQUIRE(with_half > 40);
    REQUIRE(with_half < 200);
}

TEST_CASE("a same-sex union has adopted children and no biological ones", "[generation][adoption]")
{
    auto gen = make_loaded_generator();
    gen.extend_from_string(dasmig::family_table::parameters, "key\tvalue\nsame_sex_share\t1\nadoption_factor\t1\n",
                           "same-sex.tsv");
    family_request request;
    request.up = 0;
    request.anchor.country_code = "BR";
    request.anchor.age = 55;
    int adopted = 0;
    for (std::uint64_t seed = 1; seed <= 150; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& u : f.partners()) REQUIRE(u.bio_sex == f.anchor().bio_sex);
        for (const auto& e : f.edges)
        {
            if (e.kind == dasmig::edge_kind::parent_of && f.members.at(e.to).role != relation::stepchild)
            {
                REQUIRE_FALSE(e.genetic);
                ++adopted;
            }
        }
        check_family(f);
    }
    REQUIRE(adopted > 0);
}

TEST_CASE("names follow the culture's conventions", "[names]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.up = 2;
    request.anchor.country_code = "US";
    request.anchor.age = 40;
    for (std::uint64_t seed = 1; seed <= 60; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& m : f.members)
        {
            REQUIRE_FALSE(m.name.empty());
            REQUIRE_FALSE(m.given.empty());
            REQUIRE_FALSE(m.family.empty());
            REQUIRE(m.name == m.given + " " + m.family);
        }
        // One surname: the father's family name is the anchor's, and the
        // siblings' and the paternal grandfather's too.
        const auto& father = member(f, relation::father);
        REQUIRE(f.anchor().family == father.family);
        for (const auto& s : f.siblings()) REQUIRE(s.family == father.family);
        REQUIRE(member(f, relation::paternal_grandfather).family == father.family);
        // Children of a man have his family name.
        if (f.anchor().bio_sex == dasmig::sex::male)
        {
            for (const auto& c : f.children()) REQUIRE(c.family == f.anchor().family);
        }
    }

    // A culture with a patronymic.
    request.anchor.country_code = "RU";
    request.anchor.age = 35;
    request.anchor.bio_sex = dasmig::sex::male;
    for (std::uint64_t seed = 1; seed <= 30; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        REQUIRE_FALSE(f.anchor().patronymic.empty());
        REQUIRE(f.anchor().patronymic.ends_with("ich"));
    }
}

TEST_CASE("names are asked for only when name-generator can give them", "[names][errors]")
{
    dasmig::ng empty_names;
    dasmig::relg::sources from;
    from.name = &empty_names;
    dasmig::relg gen{from};
    (void)gen.load(dasmig::dataset::lite);
    if (!gen.has_family_data())
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate)) gen.load(std::filesystem::path{candidate});
        }
    }
    REQUIRE(gen.has_data());
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 30;
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::runtime_error);
    request.names = false;
    REQUIRE_NOTHROW((void)gen.get_family(request, 1));
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

TEST_CASE("to_json, signature and string conversion", "[json]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 35;
    request.up = 2;
    const auto f = gen.get_family(request, 42);
    const auto json = f.to_json();
    REQUIRE(json.starts_with("{\"country\":\"BR\",\"reference_date\":\"2023-07-01\",\"members\":[{\"id\":0,\"role\":\"anchor\",\"sex\":\"female\",\"name\":{\"given\":"));
    REQUIRE(json.find("\"kind\":\"partner_of\"") != std::string::npos);
    REQUIRE(json.find("\"edges\":[{\"kind\":\"parent_of\"") != std::string::npos);
    REQUIRE(json.find("\"sources\":{\"fertility\":\"country\",\"unions\":\"country\"}") != std::string::npos);
    REQUIRE(json.find("\"signature\":{\"seed\":\"000000000000002a\"") != std::string::npos);
    REQUIRE(f.signature().data == gen.data_hash());

    const std::string text = f;
    REQUIRE(text.starts_with("BR family of "));
    std::ostringstream os;
    os << f;
    REQUIRE(os.str() == text);
}

// ---------------------------------------------------------------------------
// Known answers
// ---------------------------------------------------------------------------

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.data_hash() == 0xa4516485553c1cc1ULL);

    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 40;
    request.up = 2;
    request.down = 2;
    REQUIRE(gen.get_family(request, 1).to_string() == "BR family of 16 (anchor born 1982-08-15)");
    REQUIRE(gen.get_family(request, 2).to_string() == "BR family of 16 (anchor born 1983-01-14)");
    REQUIRE(gen.get_family(request, 3).to_string() == "BR family of 18 (anchor born 1982-07-05)");
    REQUIRE(gen.get_family(request, 1).to_json() == R"({"country":"BR","reference_date":"2023-07-01","members":[{"id":0,"role":"anchor","sex":"female","name":{"given":"Gisele","family":"Alves Da Silva Carolina","full":"Gisele Alves Da Silva Carolina"},"birth":"1982-08-15","alive":true,"age":40},{"id":1,"role":"father","sex":"male","name":{"given":"Carlos","family":"Bastos Carolina","full":"Carlos Bastos Carolina"},"birth":"1960-12-22","alive":true,"age":62},{"id":2,"role":"mother","sex":"female","name":{"given":"Thais","family":"Da Silva Alves Da Silva","full":"Thais Da Silva Alves Da Silva"},"birth":"1965-08-28","alive":true,"age":57},{"id":3,"role":"sibling","sex":"female","name":{"given":"Marlene","family":"Alves Da Silva Carolina","full":"Marlene Alves Da Silva Carolina"},"birth":"1986-05-28","alive":true,"age":37},{"id":4,"role":"sibling","sex":"female","name":{"given":"Ana","family":"Alves Da Silva Carolina","full":"Ana Alves Da Silva Carolina"},"birth":"1991-06-08","alive":true,"age":32},{"id":5,"role":"sibling","sex":"female","name":{"given":"Nanda","family":"Alves Da Silva Carolina","full":"Nanda Alves Da Silva Carolina"},"birth":"1994-07-21","alive":true,"age":28},{"id":6,"role":"sibling","sex":"male","name":{"given":"Isaque","family":"Alves Da Silva Carolina","full":"Isaque Alves Da Silva Carolina"},"birth":"1998-09-16","alive":true,"age":24},{"id":7,"role":"sibling","sex":"male","name":{"given":"Ney","family":"Alves Da Silva Carolina","full":"Ney Alves Da Silva Carolina"},"birth":"2003-07-21","alive":true,"age":19},{"id":8,"role":"paternal_grandfather","sex":"male","name":{"given":"Rafael","family":"Tavares Carolina","full":"Rafael Tavares Carolina"},"birth":"1921-12-25","alive":false,"death_age":92,"death":"2014-11-19"},{"id":9,"role":"paternal_grandmother","sex":"female","name":{"given":"Sirlei","family":"Rocha Bastos","full":"Sirlei Rocha Bastos"},"birth":"1931-01-13","alive":false,"death_age":76,"death":"2007-05-26"},{"id":10,"role":"maternal_grandfather","sex":"male","name":{"given":"Kaua","family":"Mello Alves Da Silva","full":"Kaua Mello Alves Da Silva"},"birth":"1931-02-15","alive":true,"age":92},{"id":11,"role":"maternal_grandmother","sex":"female","name":{"given":"Leticia","family":"Moreira Da Silva","full":"Leticia Moreira Da Silva"},"birth":"1939-03-26","alive":false,"death_age":82,"death":"2021-05-15"},{"id":12,"role":"partner","sex":"male","name":{"given":"Jose","family":"Aguiar Firmino","full":"Jose Aguiar Firmino"},"birth":"1976-08-31","alive":true,"age":46},{"id":13,"role":"child","sex":"female","name":{"given":"Mara","family":"Carolina Firmino","full":"Mara Carolina Firmino"},"birth":"2010-01-11","alive":true,"age":13},{"id":14,"role":"child","sex":"male","name":{"given":"Altair","family":"Carolina Firmino","full":"Altair Carolina Firmino"},"birth":"2010-11-24","alive":true,"age":12},{"id":15,"role":"child","sex":"male","name":{"given":"Daniel","family":"Carolina Firmino","full":"Daniel Carolina Firmino"},"birth":"2016-02-23","alive":true,"age":7}],"edges":[{"kind":"parent_of","from":1,"to":0},{"kind":"parent_of","from":2,"to":0},{"kind":"parent_of","from":8,"to":1},{"kind":"parent_of","from":9,"to":1},{"kind":"parent_of","from":10,"to":2},{"kind":"parent_of","from":11,"to":2},{"kind":"parent_of","from":1,"to":3},{"kind":"parent_of","from":2,"to":3},{"kind":"parent_of","from":1,"to":4},{"kind":"parent_of","from":2,"to":4},{"kind":"parent_of","from":1,"to":5},{"kind":"parent_of","from":2,"to":5},{"kind":"parent_of","from":1,"to":6},{"kind":"parent_of","from":2,"to":6},{"kind":"parent_of","from":1,"to":7},{"kind":"parent_of","from":2,"to":7},{"kind":"parent_of","from":12,"to":13},{"kind":"parent_of","from":0,"to":13},{"kind":"parent_of","from":12,"to":14},{"kind":"parent_of","from":0,"to":14},{"kind":"parent_of","from":12,"to":15},{"kind":"parent_of","from":0,"to":15},{"kind":"sibling_of","from":0,"to":3,"full":true},{"kind":"sibling_of","from":0,"to":4,"full":true},{"kind":"sibling_of","from":0,"to":5,"full":true},{"kind":"sibling_of","from":0,"to":6,"full":true},{"kind":"sibling_of","from":0,"to":7,"full":true},{"kind":"sibling_of","from":3,"to":4,"full":true},{"kind":"sibling_of","from":3,"to":5,"full":true},{"kind":"sibling_of","from":3,"to":6,"full":true},{"kind":"sibling_of","from":3,"to":7,"full":true},{"kind":"sibling_of","from":4,"to":5,"full":true},{"kind":"sibling_of","from":4,"to":6,"full":true},{"kind":"sibling_of","from":4,"to":7,"full":true},{"kind":"sibling_of","from":5,"to":6,"full":true},{"kind":"sibling_of","from":5,"to":7,"full":true},{"kind":"sibling_of","from":6,"to":7,"full":true},{"kind":"sibling_of","from":13,"to":14,"full":true},{"kind":"sibling_of","from":13,"to":15,"full":true},{"kind":"sibling_of","from":14,"to":15,"full":true},{"kind":"partner_of","from":0,"to":12,"start":2007,"ended":"ongoing"}],"sources":{"fertility":"country","unions":"country"},"signature":{"seed":"0000000000000001","data":"a4516485553c1cc1"}})");

    // A hash of 2,000 families with every field drawn.
    family_request any;
    any.up = 2;
    any.down = 2;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (std::uint64_t i = 1; i <= 2000; ++i)
    {
        hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(gen.get_family(any, i).to_json()));
    }
    REQUIRE(hash == 0x73e0ca6ec9a8ad47ULL);
}

// ---------------------------------------------------------------------------
// Compiled-in data
// ---------------------------------------------------------------------------

#ifdef DASMIG_RELATIONSHIP_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    dasmig::relg gen;
    REQUIRE(gen.load(dasmig::dataset::lite));
    for (const auto* candidate : {"resources/lite", "../resources/lite"})
    {
        if (std::filesystem::is_directory(candidate))
        {
            dasmig::relg from_files;
            from_files.load(std::filesystem::path{candidate});
            REQUIRE(from_files.data_hash() == gen.data_hash());
            REQUIRE(json_of(from_files.get_family(family_request{}, 5)) == json_of(gen.get_family(family_request{}, 5)));
            break;
        }
    }
}
#endif

// ---------------------------------------------------------------------------
// Batch generation
// ---------------------------------------------------------------------------

TEST_CASE("family batch: items equal the single calls for any thread count", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = family_request{};
    const auto batch = gen.get_family_batch(request, {.count = 200, .threads = 1}, 77);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_family(request, dasmig::core::item_seed(77, i)).to_json());
    }

    // A range is computed directly, on any number of threads.
    const auto tail = gen.get_family_batch(request, {.count = 100, .first = 100, .threads = 4}, 77);
    REQUIRE(tail.size() == 100);
    for (std::size_t i = 0; i < tail.size(); ++i)
    {
        REQUIRE(tail[i].to_json() == batch[100 + i].to_json());
    }
    for (const std::size_t threads : {2U, 7U, 0U})
    {
        const auto other = gen.get_family_batch(request, {.count = 200, .threads = threads}, 77);
        REQUIRE(other.size() == batch.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            REQUIRE(other[i].to_json() == batch[i].to_json());
        }
    }

    // Without a seed the batch still has the requested size.
    REQUIRE(gen.get_family_batch(request, {.count = 5}).size() == 5);
}

TEST_CASE("family batch: streaming gives the same items in order", "[batch]")
{
    auto gen = make_loaded_generator();
    const auto request = family_request{};
    const auto batch = gen.get_family_batch(request, {.count = 300, .first = 10, .threads = 3}, 5);
    std::uint64_t next = 10;
    std::size_t seen = 0;
    gen.for_each_family(request, {.count = 300, .first = 10, .threads = 3}, 5,
                        [&](std::uint64_t index, const family& item)
                        {
                            REQUIRE(index == next);
                            REQUIRE(item.to_json() == batch[seen].to_json());
                            ++next;
                            ++seen;
                        });
    REQUIRE(seen == batch.size());
}

TEST_CASE("family batch: known answer of 10,000 items", "[batch][kat]")
{
    auto gen = make_loaded_generator();
    const auto request = family_request{};
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_family(request, {.count = 10000, .threads = 4}, 1,
                        [&](std::uint64_t, const family& item)
                        { hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(item.to_json())); });
    REQUIRE(hash == 0x2d48cf6eccc678a1ULL);
}

// ---------------------------------------------------------------------------
// Input validation
// ---------------------------------------------------------------------------

TEST_CASE("anchor age outside 0 to 122 throws", "[validation]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    for (int age : {-5, 123, 150})
    {
        request.anchor.age = age;
        REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
    }
    request.anchor.age = 0;
    REQUIRE_NOTHROW(gen.get_family(request, 1));
}

TEST_CASE("country codes ignore case and unknown ones throw", "[validation]")
{
    const auto gen = make_loaded_generator();
    family_request lower;
    lower.anchor.country_code = "br";
    family_request upper;
    upper.anchor.country_code = "BR";
    REQUIRE(json_of(gen.get_family(lower, 5)) == json_of(gen.get_family(upper, 5)));

    // Fully fixed anchors skip birth-generator's draw, so the code is checked here.
    family_request fixed;
    fixed.anchor.country_code = "ZZ";
    fixed.anchor.bio_sex = dasmig::sex::female;
    fixed.anchor.birth = std::chrono::year_month_day{std::chrono::year{1990}, std::chrono::month{1}, std::chrono::day{1}};
    REQUIRE_THROWS_AS(gen.get_family(fixed, 1), std::invalid_argument);
    fixed.anchor.country_code = "xx1";
    REQUIRE_THROWS_AS(gen.get_family(fixed, 1), std::invalid_argument);
}

TEST_CASE("a fixed birth that makes the anchor older than 122 throws", "[validation]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.bio_sex = dasmig::sex::male;
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1900}, std::chrono::month{1}, std::chrono::day{1}};
    request.reference_date = std::chrono::year_month_day{std::chrono::year{2023}, std::chrono::month{6}, std::chrono::day{1}};
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Named anchors, known relatives and expansion
// ---------------------------------------------------------------------------

TEST_CASE("a named anchor keeps the name and the relatives follow it", "[names][anchor]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.bio_sex = dasmig::sex::female;
    request.anchor.age = 30;
    request.anchor.name = dasmig::person_name{"Ada", "", "Lovelace"};
    request.up = 2;
    request.down = 1;
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        REQUIRE(f.anchor().given == "Ada");
        REQUIRE(f.anchor().family == "Lovelace");
        REQUIRE(f.anchor().name == "Ada Lovelace");
        // Her father and siblings carry her family name.
        REQUIRE(member(f, relation::father).family == "Lovelace");
        for (const auto& s : f.siblings()) REQUIRE(s.family == "Lovelace");
        REQUIRE(member(f, relation::paternal_grandfather).family == "Lovelace");
    }
}

TEST_CASE("a named anchor of a double-surname culture splits the surnames", "[names][anchor]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "ES";
    request.anchor.bio_sex = dasmig::sex::male;
    request.anchor.age = 35;
    request.anchor.name = dasmig::person_name{"Juan", "", "Garcia Perez"};
    for (std::uint64_t seed = 1; seed <= 20; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        REQUIRE(f.anchor().name == "Juan Garcia Perez");
        // The father passed on the first surname, the mother the second.
        REQUIRE(member(f, relation::father).name.find("Garcia") != std::string::npos);
        REQUIRE(member(f, relation::mother).name.find("Perez") != std::string::npos);
        for (const auto& s : f.siblings()) REQUIRE(s.family == "Garcia Perez");
    }
}

TEST_CASE("a name without a given name throws", "[names][validation]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.name = dasmig::person_name{"", "", "Smith"};
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
}

TEST_CASE("name_culture decides the culture of the names", "[names]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 30;
    request.name_culture = dasmig::ng::to_culture(L"ru");
    bool patronymic = false;
    for (std::uint64_t seed = 1; seed <= 10; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        patronymic = patronymic || !f.anchor().patronymic.empty();
    }
    REQUIRE(patronymic);
}

TEST_CASE("no one has a repeated surname in a double-surname culture", "[names]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "ES";
    request.up = 2;
    request.down = 2;
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& m : f.members)
        {
            const auto space = m.family.find(' ');
            if (space == std::string::npos) continue;
            INFO(m.name << " (seed " << seed << ")");
            REQUIRE(m.family.substr(0, space) != m.family.substr(space + 1));
        }
    }
}

TEST_CASE("twins share a birth day, parents and the twin flag", "[twins]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "NG";
    request.anchor.age = 45;
    request.down = 2;
    int pairs = 0;
    for (std::uint64_t seed = 1; seed <= 400; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& m : f.members)
        {
            if (!m.twin) continue;
            ++pairs;
            bool has_twin = false;
            for (const auto& e : f.edges)
            {
                if (e.kind != dasmig::edge_kind::sibling_of || !e.full) continue;
                if (e.from != m.id && e.to != m.id) continue;
                const auto& other = f.members.at(e.from == m.id ? e.to : e.from);
                if (days(other.birth) == days(m.birth)) has_twin = true;
            }
            REQUIRE(has_twin);
        }
    }
    REQUIRE(pairs > 20);
}

TEST_CASE("half-siblings of different unions of a parent are not each other's full siblings", "[half]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 20;
    bool apart = false;
    for (std::uint64_t seed = 1; seed <= 3000 && !apart; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& e : f.edges)
        {
            if (e.kind != dasmig::edge_kind::sibling_of || e.full) continue;
            const auto& a = f.members.at(e.from);
            const auto& b = f.members.at(e.to);
            if (a.role == relation::half_sibling && b.role == relation::half_sibling) apart = true;
        }
    }
    REQUIRE(apart);
}

namespace
{
const person& by_name(const family& f, const std::string& name)
{
    for (const auto& m : f.members)
    {
        if (m.name == name) return m;
    }
    FAIL("no member named " << name);
    return f.members.front();
}
} // namespace

TEST_CASE("expanding a child keeps the parents and siblings of the family", "[expand]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    request.anchor.age = 45;
    request.up = 1;
    request.down = 1;
    bool checked = false;
    for (std::uint64_t seed = 1; seed <= 60 && !checked; ++seed)
    {
        const auto base = gen.get_family(request, seed);
        const auto kids = base.children();
        if (kids.size() < 2 || base.partners().empty()) continue;
        const auto& kid = kids.front();
        family_request around;
        around.up = 1;
        around.down = 1;
        const auto g = gen.expand(base, kid.id, around, 99);
        REQUIRE(g.anchor().name == kid.name);
        REQUIRE(days(g.anchor().birth) == days(kid.birth));
        REQUIRE(g.reference_date == base.reference_date);
        // The parents are the anchor of the base and the partner.
        const auto parents = g.parents();
        REQUIRE(parents.size() == 2);
        const std::set<std::string> parent_names{parents[0].name, parents[1].name};
        REQUIRE(parent_names.contains(base.anchor().name));
        REQUIRE(parent_names.contains(base.partners().front().name));
        // The siblings are the other children, with their names and births.
        REQUIRE(g.siblings().size() == kids.size() - 1);
        for (const auto& s : g.siblings())
        {
            const auto& original = by_name(base, s.name);
            REQUIRE(days(s.birth) == days(original.birth));
            REQUIRE(s.bio_sex == original.bio_sex);
        }
        checked = true;
    }
    REQUIRE(checked);
}

TEST_CASE("expanding a parent keeps the child and the other parent", "[expand]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 20;
    request.up = 1;
    request.down = 0;
    const auto base = gen.get_family(request, 3);
    const auto father = member(base, relation::father);
    family_request around;
    around.up = 0;
    around.down = 1;
    const auto g = gen.expand(base, father.id, around, 5);
    REQUIRE(g.anchor().name == father.name);
    // The anchor of the base is among the father's children.
    bool found = false;
    for (const auto& c : g.children()) found = found || c.name == base.anchor().name;
    REQUIRE(found);
}

TEST_CASE("expanding is deterministic and rejects a member that is not there", "[expand]")
{
    const auto gen = make_loaded_generator();
    const auto base = gen.get_family(family_request{}, 11);
    REQUIRE(json_of(gen.expand(base, 1, family_request{}, 2)) == json_of(gen.expand(base, 1, family_request{}, 2)));
    REQUIRE_THROWS_AS(gen.expand(base, base.members.size(), family_request{}, 2), std::invalid_argument);
}

TEST_CASE("known relatives are used as they are", "[expand][known]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "DE";
    request.anchor.bio_sex = dasmig::sex::male;
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1990}, std::chrono::month{5}, std::chrono::day{17}};
    dasmig::known_person mum;
    mum.bio_sex = dasmig::sex::female;
    mum.birth = std::chrono::year_month_day{std::chrono::year{1962}, std::chrono::month{3}, std::chrono::day{2}};
    mum.death = std::chrono::year_month_day{std::chrono::year{2015}, std::chrono::month{8}, std::chrono::day{9}};
    mum.name = dasmig::person_name{"Greta", "", "Weber"};
    request.anchor.mother = mum;
    request.anchor.siblings = std::vector<dasmig::known_sibling>{};
    request.anchor.unions = std::vector<dasmig::known_union>{};
    for (std::uint64_t seed = 1; seed <= 10; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        const auto& m = member(f, relation::mother);
        REQUIRE(m.name == "Greta Weber");
        REQUIRE(days(m.birth) == days(mum.birth));
        REQUIRE_FALSE(m.alive);
        REQUIRE(days(*m.death) == days(*mum.death));
        REQUIRE(f.siblings().empty());
        REQUIRE(f.half_siblings().empty());
        REQUIRE(f.partners().empty());
        REQUIRE(f.children().empty());
    }
    // A known parent born after the child is rejected.
    request.anchor.mother->birth = std::chrono::year_month_day{std::chrono::year{2000}, std::chrono::month{1}, std::chrono::day{1}};
    REQUIRE_THROWS_AS(gen.get_family(request, 1), std::invalid_argument);
}

TEST_CASE("children of two women get names that follow the first parent's family", "[names][union]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "RU";
    request.anchor.bio_sex = dasmig::sex::female;
    request.anchor.birth = std::chrono::year_month_day{std::chrono::year{1985}, std::chrono::month{2}, std::chrono::day{3}};
    request.anchor.name = dasmig::person_name{"Anna", "Ivanovna", "Petrova"};
    dasmig::known_union u;
    u.partner = dasmig::known_person{.bio_sex = dasmig::sex::female,
                                     .birth = std::chrono::year_month_day{std::chrono::year{1986}, std::chrono::month{6}, std::chrono::day{4}},
                                     .name = dasmig::person_name{"Olga", "Sergeevna", "Smirnova"}};
    u.start = std::chrono::year_month_day{std::chrono::year{2010}, std::chrono::month{1}, std::chrono::day{1}};
    u.children.push_back({.person = {.bio_sex = dasmig::sex::male,
                                     .birth = std::chrono::year_month_day{std::chrono::year{2014}, std::chrono::month{1}, std::chrono::day{1}}},
                          .genetic = false});
    u.children.push_back({.person = {.bio_sex = dasmig::sex::female,
                                     .birth = std::chrono::year_month_day{std::chrono::year{2016}, std::chrono::month{1}, std::chrono::day{1}}},
                          .genetic = false});
    request.anchor.unions = std::vector<dasmig::known_union>{u};
    const auto f = gen.get_family(request, 4);
    const auto kids = f.children();
    REQUIRE(kids.size() == 2);
    // The son takes a masculine form of Petrova, the daughter Petrova.
    REQUIRE(kids[0].family == "Petrov");
    REQUIRE(kids[1].family == "Petrova");
    REQUIRE_FALSE(kids[0].patronymic.empty());
    REQUIRE(f.partners().front().name == "Olga Sergeevna Smirnova");
}

// ---------------------------------------------------------------------------
// Stepchildren
// ---------------------------------------------------------------------------

TEST_CASE("a partner's children by an earlier union are stepchildren", "[step]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 45;
    int found = 0;
    for (std::uint64_t seed = 1; seed <= 1500; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& s : f.stepchildren())
        {
            ++found;
            // Their parent is a partner of the anchor and the anchor is not their parent.
            std::vector<std::size_t> parents;
            for (const auto& e : f.edges)
            {
                if (e.kind == dasmig::edge_kind::parent_of && e.to == s.id) parents.push_back(e.from);
            }
            REQUIRE(parents.size() == 1);
            const auto& parent = f.members.at(parents.front());
            REQUIRE(parent.role == relation::partner);
            // Born before the union with the anchor began.
            const auto union_edge = f.union_with(parent.id);
            REQUIRE(union_edge.has_value());
            REQUIRE(static_cast<int>(s.birth.year()) <= *union_edge->start);
            // The partner's family name, not the anchor's.
            REQUIRE_FALSE(s.name.empty());
        }
        // Stepchildren and the children of the same partner are half-siblings of one another.
        for (const auto& e : f.edges)
        {
            if (e.kind != dasmig::edge_kind::sibling_of) continue;
            const auto a = f.members.at(e.from).role;
            const auto b = f.members.at(e.to).role;
            if ((a == relation::stepchild) != (b == relation::stepchild) && (a == relation::child || b == relation::child))
            {
                REQUIRE_FALSE(e.full);
            }
        }
    }
    REQUIRE(found > 50);
}

TEST_CASE("stepchildren are left out without partners or children", "[step]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 45;
    request.partners = false;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) REQUIRE(gen.get_family(request, seed).stepchildren().empty());
    request.partners = true;
    request.down = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) REQUIRE(gen.get_family(request, seed).stepchildren().empty());
}

TEST_CASE("expanding the anchor keeps the partners' stepchildren", "[step][expand]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 45;
    for (std::uint64_t seed = 1; seed <= 1500; ++seed)
    {
        const auto base = gen.get_family(request, seed);
        if (base.stepchildren().empty()) continue;
        family_request again = request;
        const auto g = gen.expand(base, 0, again, 77);
        REQUIRE(g.stepchildren().size() == base.stepchildren().size());
        REQUIRE(g.children().size() == base.children().size());
        REQUIRE(g.partners().size() == base.partners().size());
        for (std::size_t i = 0; i < g.stepchildren().size(); ++i)
        {
            REQUIRE(g.stepchildren()[i].name == base.stepchildren()[i].name);
        }
        return;
    }
    FAIL("no family with stepchildren");
}

// ---------------------------------------------------------------------------
// Stepparents and households
// ---------------------------------------------------------------------------

TEST_CASE("stepparents are off by default and change no other member", "[stepparent]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 30;
    request.up = 2;
    request.down = 2;
    int with = 0;
    for (std::uint64_t seed = 1; seed <= 600; ++seed)
    {
        const auto plain = gen.get_family(request, seed);
        REQUIRE(plain.stepparents().empty());
        auto asked = request;
        asked.stepparents = true;
        const auto f = gen.get_family(asked, seed);
        REQUIRE(f.members.size() >= plain.members.size());
        // The stepparents (and their children) take the last ids, so every other
        // member keeps its id and its data; a half-sibling who is a stepparent's
        // child is named after the stepparent.
        const auto stepparent_child = [&](std::size_t id)
        {
            return std::ranges::any_of(f.edges, [&](const dasmig::edge& e)
                                       { return e.kind == dasmig::edge_kind::parent_of && e.to == id &&
                                                (f.members[e.from].role == relation::stepfather ||
                                                 f.members[e.from].role == relation::stepmother); });
        };
        for (std::size_t i = 0; i < plain.members.size(); ++i)
        {
            REQUIRE(f.members[i].role == plain.members[i].role);
            if (!stepparent_child(i)) REQUIRE(f.members[i].name == plain.members[i].name);
            REQUIRE(f.members[i].birth == plain.members[i].birth);
        }
        if (!f.stepparents().empty()) ++with;
    }
    REQUIRE(with > 10);
    REQUIRE(with < 200);
}

TEST_CASE("a stepparent is a parent's partner who joined while the anchor was a child", "[stepparent]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 40;
    request.stepparents = true;
    int found = 0;
    int ended = 0;
    for (std::uint64_t seed = 1; seed <= 1500; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        for (const auto& s : f.stepparents())
        {
            ++found;
            REQUIRE(((s.role == relation::stepfather) == (s.bio_sex == dasmig::sex::male)));
            REQUIRE_FALSE(s.name.empty());
            // The partner of the father or the mother.
            const auto link = f.union_with(s.id);
            REQUIRE(link.has_value());
            const auto& parent = f.members.at(link->from);
            REQUIRE((parent.role == relation::father || parent.role == relation::mother));
            REQUIRE(std::ranges::any_of(f.edges, [&](const dasmig::edge& e)
                                        { return e.kind == dasmig::edge_kind::step_parent_of && e.from == s.id && e.to == 0; }));
            // It began after the anchor's birth, while the parent was alive.
            REQUIRE(*link->start >= static_cast<int>(f.anchor().birth.year()));
            REQUIRE(*link->start >= static_cast<int>(parent.birth.year()) + 12);
            if (link->ended == dasmig::union_end::ongoing)
            {
                REQUIRE(s.alive);
                REQUIRE(parent.alive);
            }
            else
            {
                ++ended;
                REQUIRE(*link->end >= *link->start);
                if (link->ended == dasmig::union_end::death) REQUIRE((!s.alive || !parent.alive));
            }
            // The anchor's real parents stay the only parent_of edges into the anchor.
            for (const auto& e : f.edges)
            {
                if (e.kind == dasmig::edge_kind::parent_of && e.to == 0) REQUIRE(e.from != s.id);
            }
        }
    }
    REQUIRE(found > 50);
    REQUIRE(ended > 5);
}

TEST_CASE("stepparents need parents", "[stepparent]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.stepparents = true;
    request.up = 0;
    for (std::uint64_t seed = 1; seed <= 200; ++seed) REQUIRE(gen.get_family(request, seed).stepparents().empty());
}

TEST_CASE("stepparents are more common where more parents divorce", "[stepparent]")
{
    const auto gen = make_loaded_generator();
    const auto share = [&](const char* country)
    {
        family_request request;
        request.anchor.country_code = country;
        request.anchor.age = 30;
        request.stepparents = true;
        request.names = false;
        int with = 0;
        for (std::uint64_t seed = 1; seed <= 2000; ++seed) with += gen.get_family(request, seed).stepparents().empty() ? 0 : 1;
        return with;
    };
    REQUIRE(share("US") > share("NG"));
}

TEST_CASE("expanding a family with stepparents keeps the anchor's own unions", "[stepparent][expand]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 45;
    request.stepparents = true;
    for (std::uint64_t seed = 1; seed <= 400; ++seed)
    {
        const auto base = gen.get_family(request, seed);
        if (base.partners().empty()) continue; // a partner the base left out is drawn again
        const auto g = gen.expand(base, 0, request, 7);
        REQUIRE(g.partners().size() == base.partners().size());
        REQUIRE(g.children().size() == base.children().size());
    }
}

// The parents of a member, by id.
static std::vector<std::size_t> parents_of(const family& f, std::size_t id)
{
    std::vector<std::size_t> out;
    for (const auto& e : f.edges)
    {
        if (e.kind == dasmig::edge_kind::parent_of && e.to == id) out.push_back(e.from);
    }
    return out;
}

static bool is_stepparent(const person& m)
{
    return m.role == relation::stepfather || m.role == relation::stepmother;
}

TEST_CASE("a stepparent joins after the parents' last child together, while the anchor is a child", "[stepparent]")
{
    const auto gen = make_loaded_generator();
    for (const bool siblings : {true, false})
    {
        family_request request;
        request.anchor.country_code = "US";
        request.anchor.age = 40;
        request.stepparents = true;
        request.siblings = siblings;
        request.names = false;
        int found = 0;
        for (std::uint64_t seed = 1; seed <= 1500; ++seed)
        {
            const auto f = gen.get_family(request, seed);
            int last_together = static_cast<int>(f.anchor().birth.year());
            for (const auto& s : f.siblings()) last_together = std::max(last_together, static_cast<int>(s.birth.year()));
            for (const auto& s : f.stepparents())
            {
                ++found;
                const auto link = *f.union_with(s.id);
                REQUIRE(*link.start >= last_together);
                REQUIRE(*link.start <= static_cast<int>(f.anchor().birth.year()) + 18);
            }
        }
        REQUIRE(found > 100);
    }
    // Asking for siblings does not change the stepparents.
    family_request request;
    request.anchor.country_code = "DE";
    request.stepparents = true;
    request.down = 0;
    request.partners = false;
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        auto lone = request;
        lone.siblings = false;
        const auto a = gen.get_family(request, seed).stepparents();
        const auto b = gen.get_family(lone, seed).stepparents();
        REQUIRE(a.size() == b.size());
        for (std::size_t i = 0; i < a.size(); ++i)
        {
            REQUIRE(a[i].birth == b[i].birth);
            REQUIRE(a[i].name == b[i].name);
        }
    }
}

TEST_CASE("a parent's children with a stepparent are the anchor's half-siblings", "[stepparent][half]")
{
    const auto gen = make_loaded_generator();
    const auto share_with_joint = [&](const char* country)
    {
        family_request request;
        request.anchor.country_code = country;
        request.anchor.age = 40;
        request.stepparents = true;
        int unions = 0;
        int with_joint = 0;
        for (std::uint64_t seed = 1; seed <= 3000; ++seed)
        {
            const auto f = gen.get_family(request, seed);
            for (const auto& s : f.stepparents())
            {
                const auto link = *f.union_with(s.id);
                const auto parent = link.from;
                if (s.bio_sex == f.members[parent].bio_sex) continue;
                ++unions;
                bool joint = false;
                for (const auto& h : f.half_siblings())
                {
                    const auto ps = parents_of(f, h.id);
                    if (std::ranges::find(ps, s.id) == ps.end()) continue;
                    joint = true;
                    // The parent and the stepparent, born while they were together.
                    REQUIRE(ps.size() == 2);
                    REQUIRE(std::ranges::find(ps, parent) != ps.end());
                    REQUIRE(static_cast<int>(h.birth.year()) >= *link.start);
                    if (link.end) REQUIRE(static_cast<int>(h.birth.year()) <= *link.end);
                    REQUIRE_FALSE(h.name.empty());
                    // Named after both: the stepfather's family name where children take the father's.
                    if (s.role == relation::stepfather && std::string{country} == "US") REQUIRE(h.family == s.family);
                }
                with_joint += joint ? 1 : 0;
            }
            check_family(f);
        }
        REQUIRE(unions > 50);
        return static_cast<double>(with_joint) / unions;
    };
    // About half of remarriages with stepchildren have a child together.
    const double us = share_with_joint("US");
    REQUIRE(us > 0.35);
    REQUIRE(us < 0.7);
    REQUIRE(share_with_joint("DE") > 0.25);
}

TEST_CASE("stepsiblings are a stepparent's children from before the union", "[stepparent][stepsibling]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 35;
    request.stepparents = true;
    int found = 0;
    for (std::uint64_t seed = 1; seed <= 3000; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        const auto first_step = f.stepparents().empty() ? f.members.size() : f.stepparents().front().id;
        for (const auto& s : f.stepsiblings())
        {
            ++found;
            REQUIRE(s.id > first_step);
            REQUIRE_FALSE(s.name.empty());
            // One listed parent, a stepparent, and born before that union began.
            const auto ps = parents_of(f, s.id);
            REQUIRE(ps.size() == 1);
            const auto& sp = f.members.at(ps.front());
            REQUIRE(is_stepparent(sp));
            REQUIRE(static_cast<int>(s.birth.year()) <= *f.union_with(sp.id)->start);
            // Not the anchor's sibling, but a half-sibling of the stepparent's children with a parent.
            for (const auto& e : f.edges)
            {
                if (e.kind != dasmig::edge_kind::sibling_of || (e.from != s.id && e.to != s.id)) continue;
                const auto other = e.from == s.id ? e.to : e.from;
                REQUIRE(other != 0);
                if (f.members[other].role == relation::half_sibling) REQUIRE_FALSE(e.full);
            }
        }
        check_family(f);
    }
    REQUIRE(found > 50);

    // Not without stepparents, nor without siblings.
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        auto plain = request;
        plain.stepparents = false;
        REQUIRE(gen.get_family(plain, seed).stepsiblings().empty());
        auto lone = request;
        lone.siblings = false;
        const auto f = gen.get_family(lone, seed);
        REQUIRE(f.stepsiblings().empty());
        REQUIRE(f.half_siblings().empty());
    }
}

TEST_CASE("a child lives with a stepparent's children only in the stepparent's home", "[stepparent][household]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 12;
    request.stepparents = true;
    request.home = true;
    request.names = false;
    int together = 0;
    for (std::uint64_t seed = 1; seed <= 4000; ++seed)
    {
        const auto f = gen.get_family(request, seed);
        const auto in_home = [&](std::size_t id) { return std::ranges::find(f.home->members, id) != f.home->members.end(); };
        for (const auto& s : f.stepsiblings())
        {
            if (!in_home(s.id)) continue;
            ++together;
            REQUIRE(s.age < 18);
            REQUIRE(in_home(parents_of(f, s.id).front()));
        }
    }
    REQUIRE(together > 5);
}

TEST_CASE("expanding a parent's child with a stepparent keeps both", "[stepparent][expand]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "US";
    request.anchor.age = 30;
    request.stepparents = true;
    int expanded = 0;
    for (std::uint64_t seed = 1; seed <= 800 && expanded < 20; ++seed)
    {
        const auto base = gen.get_family(request, seed);
        for (const auto& h : base.half_siblings())
        {
            const auto ps = parents_of(base, h.id);
            if (ps.size() != 2) continue;
            ++expanded;
            family_request around;
            const auto g = gen.expand(base, h.id, around, 11);
            REQUIRE(g.anchor().birth == h.birth);
            REQUIRE(g.anchor().name == h.name);
            std::set<std::chrono::sys_days> want;
            for (const auto p : ps) want.insert(std::chrono::sys_days{base.members[p].birth});
            std::set<std::chrono::sys_days> got;
            for (const auto& p : g.parents()) got.insert(std::chrono::sys_days{p.birth});
            REQUIRE(got == want);
            break;
        }
    }
    REQUIRE(expanded >= 10);
}

TEST_CASE("a household is off by default and changes nothing else", "[household]")
{
    const auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "BR";
    for (std::uint64_t seed = 1; seed <= 300; ++seed)
    {
        const auto plain = gen.get_family(request, seed);
        REQUIRE_FALSE(plain.home.has_value());
        REQUIRE(plain.to_json().find("\"household\"") == std::string::npos);
        auto asked = request;
        asked.home = true;
        auto f = gen.get_family(asked, seed);
        REQUIRE(f.home.has_value());
        REQUIRE(f.to_json().find("\"household\":{\"type\":\"") != std::string::npos);
        f.home.reset();
        REQUIRE(f.to_json() == plain.to_json());
    }
}

TEST_CASE("a household holds living members, the anchor first, with no repeats", "[household]")
{
    const auto gen = make_loaded_generator();
    for (const char* country : {"US", "BR", "NG", "DE", "JP", "IN"})
    {
        family_request request;
        request.anchor.country_code = country;
        request.home = true;
        request.stepparents = true;
        request.up = 2;
        request.names = false;
        for (std::uint64_t seed = 1; seed <= 800; ++seed)
        {
            const auto f = gen.get_family(request, seed);
            const auto& h = *f.home;
            REQUIRE(h.members.front() == 0);
            std::set<std::size_t> unique(h.members.begin(), h.members.end());
            REQUIRE(unique.size() == h.members.size());
            for (const auto id : h.members)
            {
                REQUIRE(id < f.members.size());
                REQUIRE(f.members[id].alive);
            }
            using dasmig::household_type;
            switch (h.type)
            {
            case household_type::one_person: REQUIRE(h.members.size() == 1); break;
            case household_type::couple: REQUIRE(h.members.size() == 2); break;
            case household_type::nuclear:
            case household_type::single_parent:
            case household_type::with_parents:
            case household_type::multi_generation: REQUIRE(h.members.size() >= 2); break;
            case household_type::other: break;
            }
        }
    }
}

TEST_CASE("a child lives with a living parent and an adult with an ongoing partner", "[household]")
{
    const auto gen = make_loaded_generator();
    int children = 0;
    int couples = 0;
    for (std::uint64_t seed = 1; seed <= 1500; ++seed)
    {
        family_request request;
        request.anchor.country_code = "US";
        request.home = true;
        request.names = false;
        request.anchor.age = seed % 2 == 0 ? 10 : 40;
        const auto f = gen.get_family(request, seed);
        const auto in_home = [&](std::size_t id) { return std::ranges::find(f.home->members, id) != f.home->members.end(); };
        if (f.anchor().age < 18)
        {
            ++children;
            bool parent_alive = false;
            for (const auto& m : f.members) parent_alive |= (m.role == relation::father || m.role == relation::mother) && m.alive;
            if (parent_alive)
            {
                REQUIRE(f.home->type != dasmig::household_type::other);
                const bool with_parent = std::ranges::any_of(
                    f.members, [&](const person& m) { return (m.role == relation::father || m.role == relation::mother) && in_home(m.id); });
                REQUIRE(with_parent);
            }
            else
            {
                REQUIRE(f.home->type == dasmig::household_type::other);
            }
        }
        else
        {
            for (const auto& e : f.edges)
            {
                if (e.kind == dasmig::edge_kind::partner_of && e.from == 0 && e.ended == dasmig::union_end::ongoing)
                {
                    ++couples;
                    REQUIRE(in_home(e.to));
                }
            }
            // The children of an ongoing union who are under 18 live there too.
            for (const auto& m : f.members)
            {
                if (m.role != relation::child || !m.alive || m.age >= 18) continue;
                for (const auto& e : f.edges)
                {
                    if (e.kind != dasmig::edge_kind::parent_of || e.to != m.id || e.from == 0) continue;
                    if (f.union_with(e.from) && f.union_with(e.from)->ended == dasmig::union_end::ongoing) REQUIRE(in_home(m.id));
                }
            }
        }
    }
    REQUIRE(children > 300);
    REQUIRE(couples > 100);
}

TEST_CASE("unpartnered adults live alone more where one-person households are common", "[household]")
{
    const auto gen = make_loaded_generator();
    const auto alone = [&](const char* country)
    {
        family_request request;
        request.anchor.country_code = country;
        request.anchor.age = 45;
        request.home = true;
        request.names = false;
        request.partners = true;
        request.down = 0;
        int n = 0;
        for (std::uint64_t seed = 1; seed <= 3000; ++seed)
            n += gen.get_family(request, seed).home->type == dasmig::household_type::one_person ? 1 : 0;
        return n;
    };
    REQUIRE(alone("DE") > alone("BR"));
    REQUIRE(alone("DE") > 3 * alone("NG") / 2);
}

TEST_CASE("households are deterministic and agree across batches", "[household]")
{
    auto gen = make_loaded_generator();
    family_request request;
    request.anchor.country_code = "JP";
    request.home = true;
    request.stepparents = true;
    const auto a = gen.get_family(request, 99);
    REQUIRE(a.to_json() == gen.get_family(request, 99).to_json());
    const auto batch = gen.get_family_batch(request, {.count = 40, .threads = 2}, 5);
    for (std::size_t i = 0; i < batch.size(); ++i)
        REQUIRE(batch[i].to_json() == gen.get_family(request, dasmig::core::item_seed(5, i)).to_json());
}

TEST_CASE("family batch with stepparents and households: known answer of 10,000 items", "[batch][kat][household]")
{
    auto gen = make_loaded_generator();
    family_request request;
    request.stepparents = true;
    request.home = true;
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_family(request, {.count = 10000, .threads = 4}, 1,
                        [&](std::uint64_t, const family& item)
                        { hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(item.to_json())); });
    REQUIRE(hash == 0x6a55d380c8f9ef39ULL);
}
