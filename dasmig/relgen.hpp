// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_RELGEN_HPP
#define DASMIG_RELGEN_HPP

#include <dasmig/birthgen.hpp>
#include <dasmig/namegen.hpp>
#include "core.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef DASMIG_RELATIONSHIP_EMBED
#include "relgen_data.hpp"
#endif

/// @file relgen.hpp
/// @brief Relationship Generator library — believable families around a person: parents, siblings, partners and children, with ages and names for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 1);

namespace dasmig
{

/// @brief A name the caller already has, as plain words.
///
/// `family` holds the family name; for a culture with two surnames (Spain,
/// Portugal) two words are read as the two surnames.
struct person_name
{
    std::string given;      ///< Given name; not empty.
    std::string patronymic; ///< Patronymic, for the cultures that have one; empty for none.
    std::string family;     ///< Family name; empty for none.
};

/// @brief Someone the caller already knows: used to build the family of a
///        member of a family that exists, so that both agree.
///
/// A person who is alive has no `death`; one who died before the reference
/// date has it.
struct known_person
{
    sex bio_sex{sex::male};                       ///< Biological sex.
    std::chrono::year_month_day birth{};          ///< Date of birth.
    std::optional<std::chrono::year_month_day> death{}; ///< Date of death, for the dead. // NOLINT(readability-redundant-member-init)
    std::optional<person_name> name{};            ///< The name, or drawn to match the relatives'. // NOLINT(readability-redundant-member-init)
    std::vector<known_person> parents{};          ///< This person's own parents, when known: the anchor's grandparents (at most one of each sex). // NOLINT(readability-redundant-member-init)
};

/// @brief A brother or sister the caller already knows.
struct known_sibling
{
    known_person person{};      ///< Who they are.
    bool full{true};            ///< Both parents are shared; otherwise only `through`.
    sex through{sex::male};     ///< For a half-sibling: the shared parent's sex.
};

/// @brief A child the caller already knows.
struct known_child
{
    known_person person{}; ///< Who they are.
    bool genetic{true};    ///< By birth rather than by adoption.
};

/// @brief A union of the anchor that the caller already knows, with the
///        children it had.
struct known_union
{
    std::optional<known_person> partner{};                    ///< The partner; none when only the children are known. // NOLINT(readability-redundant-member-init)
    std::chrono::year_month_day start{};                      ///< When the union began.
    std::optional<std::chrono::year_month_day> end{};         ///< When it ended; none if it is ongoing. // NOLINT(readability-redundant-member-init)
    std::vector<known_child> children{};                      ///< The children of this union. // NOLINT(readability-redundant-member-init)
    std::vector<known_person> stepchildren{};                 ///< The partner's children by an earlier union. // NOLINT(readability-redundant-member-init)
};

/// @brief The person a family is built around, as plain values.
///
/// Whatever is not given is drawn. A fixed `birth` wins over `age`.
///
/// The relatives (`father`, `mother`, `siblings`, `unions`) are for building
/// the family of a member of a family that exists (see relg::expand()): a
/// relative that is given is used as it is, and when a list is given no one
/// else is drawn for it, even if the list is empty.
struct anchor_spec
{
    /// @brief ISO 3166-1 alpha-2 code, or drawn by population.
    std::optional<std::string> country_code{}; // NOLINT(readability-redundant-member-init)
    /// @brief Biological sex, or drawn.
    std::optional<sex> bio_sex{}; // NOLINT(readability-redundant-member-init)
    /// @brief Date of birth, or drawn.
    std::optional<std::chrono::year_month_day> birth{}; // NOLINT(readability-redundant-member-init)
    /// @brief Age in completed years at the reference date, or drawn.
    std::optional<int> age{}; // NOLINT(readability-redundant-member-init)
    /// @brief The anchor's name; the parents, siblings and children are named
    ///        to match it. Drawn when omitted.
    std::optional<person_name> name{}; // NOLINT(readability-redundant-member-init)
    /// @brief The anchor's father, if known. Needs `up >= 1` to appear.
    std::optional<known_person> father{}; // NOLINT(readability-redundant-member-init)
    /// @brief The anchor's mother, if known. Needs `up >= 1` to appear.
    std::optional<known_person> mother{}; // NOLINT(readability-redundant-member-init)
    /// @brief The anchor's siblings and half-siblings, if known; none are
    ///        drawn when this is set. Needs `up >= 1` and `siblings`.
    std::optional<std::vector<known_sibling>> siblings{}; // NOLINT(readability-redundant-member-init)
    /// @brief The anchor's unions, if known (oldest first); none are drawn
    ///        when this is set, and the children are the ones listed.
    std::optional<std::vector<known_union>> unions{}; // NOLINT(readability-redundant-member-init)
};

/// @brief What a family call asks for.
///
/// Every generation call takes one; `family_request{}` asks for the
/// defaults. Choices are never stored on the generator, so one call cannot
/// change the next.
struct family_request
{
    /// @brief The person the family is around; omitted fields are drawn.
    anchor_spec anchor{};
    /// @brief Generations above the anchor: 0 for none, 1 for parents, 2 for
    ///        grandparents too.
    int up{1};
    /// @brief Generations below the anchor: 0 for none, 1 for children, 2 for
    ///        grandchildren too.
    int down{1};
    /// @brief Whether to include the anchor's siblings (and half-siblings).
    ///        Siblings come from the parents, so `up = 0` gives none.
    bool siblings{true};
    /// @brief Whether to include the anchor's partners, current and former.
    ///        Children come from the unions either way.
    bool partners{true};
    /// @brief Whether to give everyone a name (needs name-generator's data).
    bool names{true};
    /// @brief The culture the names follow; the anchor's country's by default.
    ///        Needs `names`; a culture with no names loaded leaves the family
    ///        without names.
    std::optional<culture> name_culture{}; // NOLINT(readability-redundant-member-init)
    /// @brief Population-weighted (true) or uniform country, when drawn.
    bool weighted{true};
    /// @brief The date ages and who is alive are computed at (1 July 2023,
    ///        the date of the UN estimates, by default).
    std::chrono::year_month_day reference_date{birth_request::default_reference_date};
};

/// @brief How a member is related to the anchor.
enum class relation : std::uint8_t
{
    anchor,               ///< The person the family is built around.
    father,               ///< The anchor's father.
    mother,               ///< The anchor's mother.
    sibling,              ///< A full brother or sister.
    half_sibling,         ///< A half-brother or half-sister, through one parent.
    paternal_grandfather, ///< The father's father.
    paternal_grandmother, ///< The father's mother.
    maternal_grandfather, ///< The mother's father.
    maternal_grandmother, ///< The mother's mother.
    partner,              ///< A partner of the anchor, current or former.
    child,                ///< A child of the anchor, by birth or adoption.
    grandchild,           ///< A child of one of the anchor's children.
    stepchild             ///< A child of a partner by an earlier union, not the anchor's.
};

/// @brief The name of a relation as it appears in JSON (`paternal_grandfather`).
/// @param r The relation.
/// @return The name.
[[nodiscard]] constexpr std::string_view relation_name(relation r) noexcept
{
    switch (r)
    {
    case relation::anchor: return "anchor";
    case relation::father: return "father";
    case relation::mother: return "mother";
    case relation::sibling: return "sibling";
    case relation::half_sibling: return "half_sibling";
    case relation::paternal_grandfather: return "paternal_grandfather";
    case relation::paternal_grandmother: return "paternal_grandmother";
    case relation::maternal_grandfather: return "maternal_grandfather";
    case relation::maternal_grandmother: return "maternal_grandmother";
    case relation::partner: return "partner";
    case relation::child: return "child";
    case relation::grandchild: return "grandchild";
    case relation::stepchild: return "stepchild";
    }
    return "unknown";
}

/// @brief A person in a family: who they are to the anchor, their name and
///        when they live.
struct person
{
    std::size_t id{0};                       ///< Index in family::members.
    relation role{relation::anchor};         ///< How the person is related to the anchor.
    sex bio_sex{sex::male};                  ///< Biological sex.
    std::string given;                       ///< Given name; empty without names.
    std::string patronymic;                  ///< Patronymic, for the cultures that have one.
    std::string family;                      ///< Family name (both of a double surname).
    std::string name;                        ///< The full name, in the culture's order.
    std::chrono::year_month_day birth{};     ///< Date of birth.
    bool twin{false};                        ///< Born on the same day as a full sibling (or the anchor's).
    bool alive{true};                        ///< Alive at the reference date.
    std::uint8_t age{0};                     ///< Age in completed years at the reference date, or at death.
    std::optional<std::chrono::year_month_day> death{}; ///< Date of death, for the dead. // NOLINT(readability-redundant-member-init)
};

/// @brief How two members are linked.
enum class edge_kind : std::uint8_t
{
    parent_of,  ///< `from` is a parent of `to`.
    sibling_of, ///< `from` and `to` share a parent; `from` is the older.
    partner_of  ///< `from` (the anchor) and `to` are, or were, partners.
};

/// @brief How a union ended.
enum class union_end : std::uint8_t
{
    ongoing, ///< It has not ended at the reference date.
    divorce, ///< The partners separated.
    death    ///< The partner died.
};

/// @brief The name of an ending as it appears in JSON.
/// @param e The ending.
/// @return `ongoing`, `divorce` or `death`.
[[nodiscard]] constexpr std::string_view union_end_name(union_end e) noexcept
{
    switch (e)
    {
    case union_end::ongoing: return "ongoing";
    case union_end::divorce: return "divorce";
    case union_end::death: return "death";
    }
    return "unknown";
}

/// @brief A link between two members, by id.
struct edge
{
    edge_kind kind{edge_kind::parent_of}; ///< What the link is.
    std::size_t from{0};                  ///< First member.
    std::size_t to{0};                    ///< Second member.
    bool full{true};                      ///< For siblings: both parents are shared.
    bool genetic{true};                   ///< For parents: the child is the parent's by birth, not by adoption.
    std::optional<int> start{};           ///< For partners: the year the union began.
    std::optional<int> end{};             ///< For partners: the year it ended, if it has.
    union_end ended{union_end::ongoing};  ///< For partners: how it ended.
};

/// @brief Where a table's values came from for the family's country.
enum class data_source : std::uint8_t
{
    country,   ///< The country's own row.
    subregion, ///< The median of its UN subregion.
    world      ///< The world median.
};

/// @brief The name of a data source as it appears in JSON.
/// @param s The source.
/// @return `country`, `subregion` or `world`.
[[nodiscard]] constexpr std::string_view data_source_name(data_source s) noexcept
{
    switch (s)
    {
    case data_source::country: return "country";
    case data_source::subregion: return "subregion";
    case data_source::world: return "world";
    }
    return "unknown";
}

/// @brief Return type for family generation.
///
/// Holds the members (the anchor is `members[0]`) and the links between
/// them, and supports streaming a short summary via operator<< and JSON
/// output via to_json().
class family
{
  public:
    std::string country_code;                 ///< The anchor's country; the family lives there.
    std::chrono::year_month_day reference_date{}; ///< The date ages and who is alive refer to.
    std::vector<person> members;              ///< Everyone; `members[i].id == i`.
    std::vector<edge> edges;                  ///< Parent, sibling and partner links.
    data_source fertility_source{data_source::country}; ///< Where the fertility figures came from.
    data_source union_source{data_source::country};     ///< Where the marriage figures came from.

    /// @brief The person the family is built around.
    /// @return `members[0]`.
    [[nodiscard]] const person& anchor() const { return members.front(); }

    /// @brief The members with a given role.
    /// @param r The role.
    /// @return Copies of them, in id order.
    [[nodiscard]] std::vector<person> with_role(relation r) const
    {
        std::vector<person> out;
        for (const auto& m : members)
        {
            if (m.role == r) out.push_back(m);
        }
        return out;
    }

    /// @brief The anchor's parents.
    /// @return Father then mother, as present.
    [[nodiscard]] std::vector<person> parents() const
    {
        auto out = with_role(relation::father);
        for (auto& m : with_role(relation::mother)) out.push_back(std::move(m));
        return out;
    }

    /// @brief The anchor's full siblings, oldest first.
    /// @return The siblings.
    [[nodiscard]] std::vector<person> siblings() const { return with_role(relation::sibling); }

    /// @brief The anchor's half-siblings, oldest first.
    /// @return The half-siblings.
    [[nodiscard]] std::vector<person> half_siblings() const { return with_role(relation::half_sibling); }

    /// @brief The anchor's grandparents.
    /// @return Paternal then maternal, father's side first.
    [[nodiscard]] std::vector<person> grandparents() const
    {
        std::vector<person> out;
        for (const auto r : {relation::paternal_grandfather, relation::paternal_grandmother,
                             relation::maternal_grandfather, relation::maternal_grandmother})
        {
            for (auto& m : with_role(r)) out.push_back(std::move(m));
        }
        return out;
    }

    /// @brief The anchor's partners, in the order the unions began.
    /// @return The partners, current and former.
    [[nodiscard]] std::vector<person> partners() const { return with_role(relation::partner); }

    /// @brief The anchor's children, oldest first.
    /// @return The children, by birth and by adoption.
    [[nodiscard]] std::vector<person> children() const { return with_role(relation::child); }

    /// @brief The anchor's grandchildren, oldest first.
    /// @return The grandchildren.
    [[nodiscard]] std::vector<person> grandchildren() const { return with_role(relation::grandchild); }

    /// @brief The children of the anchor's partners by earlier unions.
    /// @return The stepchildren, oldest first.
    [[nodiscard]] std::vector<person> stepchildren() const { return with_role(relation::stepchild); }

    /// @brief The union with a partner.
    /// @param partner_id The partner's id.
    /// @return The link, or nothing if the member is not a partner.
    [[nodiscard]] std::optional<edge> union_with(std::size_t partner_id) const
    {
        for (const auto& e : edges)
        {
            if (e.kind == edge_kind::partner_of && e.to == partner_id) return e;
        }
        return std::nullopt;
    }

    /// @brief Retrieve the random seed used to generate this family.
    /// @return The call seed; pass it to get_family(request, seed) with
    ///         the same request and data to replay this family.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this family was generated from.
    /// @return The replay signature; `data` is relg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Human-readable summary string.
    /// @return For example `BR family of 5 (anchor born 1983-05-20)`.
    [[nodiscard]] std::string to_string() const
    {
        return std::format("{} family of {} (anchor born {})", country_code, members.size(),
                           iso_date(members.front().birth));
    }

    /// @brief Write this family as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("country", country_code)
            .field("reference_date", iso_date(reference_date))
            .key("members")
            .begin_array();
        for (const auto& m : members)
        {
            w.begin_object().field("id", m.id).field("role", relation_name(m.role)).field("sex", core::sex_name(m.bio_sex));
            if (!m.name.empty())
            {
                w.key("name").begin_object().field("given", m.given);
                if (!m.patronymic.empty()) w.field("patronymic", m.patronymic);
                w.field("family", m.family).field("full", m.name).end_object();
            }
            w.field("birth", iso_date(m.birth));
            if (m.twin) w.field("twin", true);
            w.field("alive", m.alive);
            if (m.alive)
            {
                w.field("age", static_cast<int>(m.age));
            }
            else
            {
                w.field("death_age", static_cast<int>(m.age)).field("death", iso_date(*m.death));
            }
            w.end_object();
        }
        w.end_array().key("edges").begin_array();
        for (const auto& e : edges)
        {
            w.begin_object();
            switch (e.kind)
            {
            case edge_kind::parent_of:
                w.field("kind", "parent_of").field("from", e.from).field("to", e.to);
                if (!e.genetic) w.field("genetic", false);
                break;
            case edge_kind::sibling_of:
                w.field("kind", "sibling_of").field("from", e.from).field("to", e.to).field("full", e.full);
                break;
            case edge_kind::partner_of:
                w.field("kind", "partner_of").field("from", e.from).field("to", e.to).field("start", *e.start);
                if (e.end) w.field("end", *e.end);
                w.field("ended", union_end_name(e.ended));
                break;
            }
            w.end_object();
        }
        w.end_array()
            .key("sources")
            .begin_object()
            .field("fertility", data_source_name(fertility_source))
            .field("unions", data_source_name(union_source))
            .end_object()
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This family as a JSON document.
    /// @return Compact JSON, for example
    ///   `{"country":"BR","reference_date":"2023-07-01","members":[...],"edges":[...],...}`.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return Summary string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return to_string();
    }

    /// @brief Stream the summary to an output stream.
    /// @param os Output stream.
    /// @param v The family to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const family& v)
    {
        os << v.to_string();
        return os;
    }

    /// @brief A date as `YYYY-MM-DD`.
    /// @param d The date.
    /// @return ISO 8601 text.
    [[nodiscard]] static std::string iso_date(const std::chrono::year_month_day& d)
    {
        return std::format("{:04}-{:02}-{:02}", static_cast<int>(d.year()),
                           static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
    }

  private:
    core::seed_signature _signature;
    friend class relg;
};

/// @brief The tables of the family data, for extending it.
enum class family_table : std::uint8_t
{
    regions,    ///< `regions.tsv`: the UN subregion of each country.
    fertility,  ///< `fertility.tsv`: total fertility rate (now and by period) and fertility by the mother's age.
    unions,     ///< `unions.tsv`: age at first marriage, never married and divorced shares.
    households, ///< `households.tsv`: household size and types.
    defaults,   ///< `defaults.tsv`: the median of every column per subregion, and the world.
    parameters  ///< `parameters.tsv`: the model's constants.
};

/// @brief Relationship Generator: builds believable families around a person.
///
/// A family is drawn from demography: the mother's age at the anchor's birth
/// from her country's fertility by age, the father from the typical
/// difference in marriage ages, the number of siblings from the total
/// fertility rate of the anchor's birth year (the family size seen from a
/// child is size-biased, so a family with more children is more likely to be
/// the anchor's), and who is alive from a Gompertz survival curve that
/// matches the country's life expectancy. Everything is drawn from named
/// streams split from the call seed (`"anchor"`, `"parents"`, `"mother"`,
/// `"father"`, `"siblings"`, `"grandparents"`), so asking for grandparents
/// or for no siblings does not change the rest. Output is identical on every
/// compiler and platform (dasmig-core).
///
/// The anchor, the country's age pyramid and life expectancy come from
/// birth-generator (the singleton by default, or an instance passed in).
///
/// Can be used as a singleton via instance() or constructed
/// independently.
///
/// @par Thread safety
/// get_family(request, seed) is const and only calls const members of
/// birth-generator, so it may run concurrently. get_family(request) draws
/// from the generator's engine and needs external synchronisation on the
/// same instance; so do load() and extend().
class relg
{
  public:
    /// @brief The library instances a family is drawn from.
    struct sources
    {
        bthg* birth{&bthg::instance()}; ///< Anchor, age structure and life expectancy.
        ng* name{&ng::instance()};      ///< Names.
    };

    /// @brief Default constructor — creates an empty generator with no
    ///        data, drawing from the birth-generator singleton.
    relg() = default;

    /// @brief A generator with no data that draws from the given library
    ///        instances (which must outlive it).
    /// @param from The instances to use.
    explicit relg(sources from) : _from{from} {}

    relg(const relg&) = delete;            ///< Not copyable.
    relg& operator=(const relg&) = delete; ///< Not copyable.
    relg(relg&&) noexcept = default;       ///< Movable.
    relg& operator=(relg&&) noexcept = default; ///< Movable.
    ~relg() = default;                       ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static relg& instance()
    {
        static relg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random family.
    ///        Use family_request{} for the defaults.
    /// @param request What to generate.
    /// @return The family.
    /// @throws std::runtime_error If this generator or birth-generator has no
    ///         data loaded.
    /// @throws std::invalid_argument If the request is invalid.
    [[nodiscard]] family get_family(const family_request& request)
    {
        return get_family(request, draw_seed_());
    }

    /// @brief Generate a deterministic family.
    /// @param request What to generate.
    /// @param call_seed Seed; the same request, seed and data give the same
    ///        result. Does not touch the instance engine.
    /// @return The family.
    /// @throws std::runtime_error If this generator or birth-generator has no
    ///         data loaded, or names are requested and name-generator has
    ///         none.
    /// @throws std::invalid_argument If `up` or `down` is not 0 to 2, the
    ///         anchor's age is outside 0 to 122, its birth is after the
    ///         reference date or makes it older than 122, or its country
    ///         code (matched without regard to case) has no data in
    ///         birth-generator.
    [[nodiscard]] family get_family(const family_request& request, std::uint64_t call_seed) const
    {
        require_data_();
        if (request.up < 0 || request.up > 2)
        {
            throw std::invalid_argument("family_request.up must be 0, 1 or 2");
        }
        if (request.down < 0 || request.down > 2)
        {
            throw std::invalid_argument("family_request.down must be 0, 1 or 2");
        }
        if (request.names && !_from.name->has_resources())
        {
            throw std::runtime_error("relationship-generator: names were requested and name-generator has no data loaded");
        }
        const core::rng call{call_seed};
        const double ref_day = static_cast<double>(days_(request.reference_date));
        check_known_(request.anchor);

        const anchor_info anchor = draw_anchor_(request, call.split("anchor").seed(), ref_day);
        const country_model& cm = lookup_(anchor.country);

        family f;
        f._signature = {.seed = call_seed, .data = data_hash()};
        f.country_code = anchor.country;
        f.reference_date = request.reference_date;
        f.fertility_source = cm.fertility.source;
        f.union_source = cm.unions.source;

        // The anchor is alive by definition: a lifespan no one reaches.
        const auto& spec = request.anchor;
        const life anchor_life{.birth = anchor.birth_day, .lifespan = never_dies_, .s = anchor.s};
        std::vector<raw_member> raw;
        push_(raw, relation::anchor, anchor_life, "anchor");
        raw[0].fixed = spec.name;

        if (request.up >= 1)
        {
            const known_parents known{spec.father ? &*spec.father : nullptr, spec.mother ? &*spec.mother : nullptr};
            const bool known_siblings = spec.siblings.has_value();
            auto lin = draw_lineage_(cm, anchor.country, call.split("parents"), anchor.birth_day, ref_day,
                                     request.siblings && !known_siblings, known);
            struct half_rec
            {
                life l;
                std::size_t parent;
                std::string_view label;
                std::int64_t other;
                bool father;
            };
            std::vector<half_rec> halves;
            const auto father_slot = raw.size();
            const auto mother_slot = raw.size() + 1;
            if (request.siblings && known_siblings)
            {
                for (std::size_t i = 0; i < spec.siblings->size(); ++i)
                {
                    const auto& ks = spec.siblings->at(i);
                    const auto l = life_of_(ks.person, ref_day, static_cast<int>(i));
                    if (ks.full)
                    {
                        lin.siblings.push_back(l);
                    }
                    else
                    {
                        const bool via_father = ks.through == sex::male;
                        halves.push_back({l, via_father ? father_slot : mother_slot, via_father ? "father" : "mother",
                                          none_, via_father});
                    }
                }
            }
            // Siblings take ids oldest first; each was drawn from a stream of its
            // own, so the order does not change what is drawn.
            std::ranges::sort(lin.siblings, [](const life& x, const life& y) { return x.birth < y.birth; });
            const auto father = push_(raw, relation::father, lin.father, "father");
            const auto mother = push_(raw, relation::mother, lin.mother, "mother");
            if (spec.father) raw[father].fixed = spec.father->name;
            if (spec.mother) raw[mother].fixed = spec.mother->name;
            raw[father].name_child = 0;
            raw[mother].name_child = 0;
            raw[0].parents = {static_cast<std::int64_t>(father), static_cast<std::int64_t>(mother)};
            for (const auto& s : lin.siblings)
            {
                const auto id = push_(raw, relation::sibling, s, "sibling." + std::to_string(s.index));
                raw[id].parents = raw[0].parents;
                if (known_siblings) raw[id].fixed = spec.siblings->at(static_cast<std::size_t>(s.index)).person.name;
            }
            if (request.siblings)
            {
                if (!known_siblings)
                {
                    const auto half = call.split("half-siblings");
                    // The father's and the mother's other children: the other parent is not a member.
                    const struct
                    {
                        std::size_t parent;
                        std::string_view label;
                        bool father;
                    } sides[] = {{father, "father", true}, {mother, "mother", false}};
                    for (const auto& side : sides)
                    {
                        for (const auto& k : draw_half_siblings_(cm, anchor.country, raw.at(side.parent).life, side.father,
                                                                 side.father ? std::vector<double>{} : lin.mother_births,
                                                                 ref_day, half.split(side.label)))
                        {
                            halves.push_back({k, side.parent, side.label, none_, side.father});
                        }
                    }
                }
                // Half-siblings born within a few years of one another through the same
                // parent are the children of one other parent; later ones, of another.
                std::ranges::stable_sort(halves, [](const half_rec& x, const half_rec& y) { return x.l.birth < y.l.birth; });
                std::int64_t next_other = unlisted_ + 1000;
                std::array<std::int64_t, 2> current{none_, none_};
                std::array<double, 2> last{0.0, 0.0};
                for (auto& h : halves)
                {
                    const std::size_t side = h.father ? 0 : 1;
                    if (current[side] == none_ || h.l.birth - last[side] > half_cluster_years_ * year_days_)
                    {
                        current[side] = next_other++;
                    }
                    last[side] = h.l.birth;
                    h.other = current[side];
                    const auto id = push_(raw, relation::half_sibling, h.l,
                                          "half." + std::string{h.label} + "." + std::to_string(h.l.index));
                    raw[id].parents = h.father
                                          ? std::array<std::int64_t, 2>{static_cast<std::int64_t>(h.parent), h.other}
                                          : std::array<std::int64_t, 2>{h.other, static_cast<std::int64_t>(h.parent)};
                    if (known_siblings) raw[id].fixed = spec.siblings->at(static_cast<std::size_t>(h.l.index)).person.name;
                }
            }
            if (request.up >= 2)
            {
                const auto grand = call.split("grandparents");
                const struct
                {
                    std::size_t parent;
                    std::string_view label;
                    relation grandfather;
                    relation grandmother;
                    const std::optional<known_person>* given;
                } sides[] = {{father, "father", relation::paternal_grandfather, relation::paternal_grandmother, &spec.father},
                             {mother, "mother", relation::maternal_grandfather, relation::maternal_grandmother, &spec.mother}};
                for (const auto& side : sides)
                {
                    known_parents above{nullptr, nullptr};
                    if (side.given->has_value())
                    {
                        for (const auto& gp : (*side.given)->parents)
                        {
                            (gp.bio_sex == sex::male ? above.father : above.mother) = &gp;
                        }
                    }
                    const auto g = draw_lineage_(cm, anchor.country, grand.split(side.label),
                                                 raw.at(side.parent).life.birth, ref_day, false, above);
                    const auto gf = push_(raw, side.grandfather, g.father, "gp." + std::string{side.label} + ".m");
                    const auto gm = push_(raw, side.grandmother, g.mother, "gp." + std::string{side.label} + ".f");
                    if (above.father != nullptr) raw[gf].fixed = above.father->name;
                    if (above.mother != nullptr) raw[gm].fixed = above.mother->name;
                    raw[gf].name_child = static_cast<std::int64_t>(side.parent);
                    raw[gm].name_child = static_cast<std::int64_t>(side.parent);
                    raw[side.parent].parents = {static_cast<std::int64_t>(gf), static_cast<std::int64_t>(gm)};
                }
            }
        }

        std::vector<union_rec> unions;
        if (spec.unions)
        {
            for (std::size_t j = 0; j < spec.unions->size(); ++j)
            {
                unions.push_back(union_of_(spec.unions->at(j), j, anchor.s, ref_day));
            }
        }
        else if (request.partners || request.down >= 1)
        {
            unions = draw_unions_(cm, anchor.country, anchor_life, ref_day, call.split("unions"));
        }
        // The member id of each partner, or a token for one who is not listed.
        std::vector<std::int64_t> partner_slot;
        std::vector<std::size_t> partner_ids; // by union; none_ for one with no partner member
        for (std::size_t j = 0; j < unions.size(); ++j)
        {
            if (request.partners && unions[j].has_partner)
            {
                const auto id = push_(raw, relation::partner, unions[j].partner, "partner." + std::to_string(j));
                raw[id].fixed = unions[j].partner_name;
                partner_ids.push_back(id);
                partner_slot.push_back(static_cast<std::int64_t>(id));
            }
            else
            {
                partner_ids.push_back(no_member_);
                partner_slot.push_back(unlisted_ + 10 + static_cast<std::int64_t>(j));
            }
        }

        // The partners' children by earlier unions.
        if (request.down >= 1 && request.partners)
        {
            const auto step = call.split("stepchildren");
            std::vector<std::pair<std::size_t, life>> stepkids; // union, child
            for (std::size_t j = 0; j < unions.size(); ++j)
            {
                if (!unions[j].has_partner) continue;
                if (spec.unions)
                {
                    for (std::size_t i = 0; i < spec.unions->at(j).stepchildren.size(); ++i)
                    {
                        stepkids.emplace_back(j, life_of_(spec.unions->at(j).stepchildren[i], ref_day, static_cast<int>(i)));
                    }
                    continue;
                }
                for (const auto& k : draw_stepchildren_(cm, anchor.country, unions[j], ref_day, step.split(static_cast<std::uint64_t>(j))))
                {
                    stepkids.emplace_back(j, k);
                }
            }
            std::ranges::stable_sort(stepkids, [](const auto& x, const auto& y) { return x.second.birth < y.second.birth; });
            for (const auto& [j, k] : stepkids)
            {
                const auto partner = partner_ids.at(j);
                const auto token = unlisted_ + 500 + static_cast<std::int64_t>(j);
                const auto id = push_(raw, relation::stepchild, k, "step." + std::to_string(j) + "." + std::to_string(k.index));
                raw[id].parents = raw.at(partner).life.s == sex::male
                                      ? std::array<std::int64_t, 2>{static_cast<std::int64_t>(partner), token}
                                      : std::array<std::int64_t, 2>{token, static_cast<std::int64_t>(partner)};
                if (spec.unions) raw[id].fixed = spec.unions->at(j).stepchildren.at(static_cast<std::size_t>(k.index)).name;
            }
        }

        if (request.down >= 1)
        {
            auto kids = spec.unions ? known_children_(*spec.unions, ref_day)
                                    : draw_union_children_(cm, anchor.country, anchor_life, unions, ref_day,
                                                           call.split("children"));
            std::ranges::stable_sort(kids, [](const child_rec& x, const child_rec& y) { return x.life.birth < y.life.birth; });
            const std::size_t first_child = raw.size();
            for (const auto& k : kids)
            {
                const auto& u = unions.at(static_cast<std::size_t>(k.union_index));
                const std::int64_t other = partner_slot.at(static_cast<std::size_t>(k.union_index));
                // The father's slot first; in a same-sex union, the anchor's.
                const bool anchor_first = u.same_sex || anchor.s == sex::male;
                const auto id = push_(raw, relation::child, k.life,
                                      "child." + std::to_string(k.union_index) + "." + std::to_string(k.life.index));
                raw[id].parents = anchor_first ? std::array<std::int64_t, 2>{0, other} : std::array<std::int64_t, 2>{other, 0};
                raw[id].genetic = k.genetic;
                raw[id].fixed = k.fixed;
            }
            if (request.down >= 2)
            {
                const auto grand = call.split("grandchildren");
                std::vector<std::size_t> parents_of_grandchildren;
                for (std::size_t c = first_child; c < raw.size(); ++c) parents_of_grandchildren.push_back(c);
                std::vector<std::pair<std::size_t, life>> grandchildren;
                for (const auto c : parents_of_grandchildren)
                {
                    const auto kids2 = draw_grandchildren_(cm, anchor.country, raw.at(c).life, ref_day, grand.split(raw.at(c).key));
                    for (const auto& k : kids2) grandchildren.emplace_back(c, k);
                }
                std::ranges::stable_sort(grandchildren, [](const auto& x, const auto& y) { return x.second.birth < y.second.birth; });
                for (const auto& [c, k] : grandchildren)
                {
                    const auto token = unlisted_ + 100 + static_cast<std::int64_t>(c);
                    const auto id = push_(raw, relation::grandchild, k, "grand." + raw.at(c).key + "." + std::to_string(k.index));
                    raw[id].parents = raw.at(c).life.s == sex::male
                                          ? std::array<std::int64_t, 2>{static_cast<std::int64_t>(c), token}
                                          : std::array<std::int64_t, 2>{token, static_cast<std::int64_t>(c)};
                }
            }
        }

        // Parent links, then sibling links (members that share a parent, oldest
        // first, full when both parents are shared), then the anchor's unions.
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            for (const auto p : raw[i].parents)
            {
                if (p >= 0 && p < unlisted_)
                {
                    f.edges.push_back({.kind = edge_kind::parent_of, .from = static_cast<std::size_t>(p), .to = i,
                                       .full = true, .genetic = raw[i].genetic});
                }
            }
        }
        std::vector<bool> twin(raw.size(), false);
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            for (std::size_t k = i + 1; k < raw.size(); ++k)
            {
                const auto& a = raw[i].parents;
                const auto& b = raw[k].parents;
                const auto shares = [&](std::int64_t x) { return x != none_ && (x == b[0] || x == b[1]); };
                if (!shares(a[0]) && !shares(a[1])) continue;
                const bool full = (a[0] == b[0] && a[1] == b[1]) || (a[0] == b[1] && a[1] == b[0]);
                if (full && std::floor(raw[i].life.birth) == std::floor(raw[k].life.birth)) twin[i] = twin[k] = true;
                const bool older = raw[i].life.birth <= raw[k].life.birth;
                f.edges.push_back({.kind = edge_kind::sibling_of, .from = older ? i : k, .to = older ? k : i,
                                   .full = full});
            }
        }
        for (std::size_t j = 0; j < partner_ids.size(); ++j)
        {
            if (partner_ids[j] == no_member_) continue;
            const auto& u = unions[j];
            edge e{.kind = edge_kind::partner_of, .from = 0, .to = partner_ids[j]};
            e.start = static_cast<int>(from_days_(u.start).year());
            if (u.ended != union_end::ongoing) e.end = static_cast<int>(from_days_(u.end).year());
            e.ended = u.ended;
            f.edges.push_back(e);
        }

        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            f.members.push_back(to_person_(raw[i], i, ref_day));
            f.members.back().twin = twin[i];
        }
        if (request.names) assign_names_(f.members, raw, call.split("names"), anchor.country, request.name_culture);
        return f;
    }

    /// @brief Generate the family of a member of a family, so that the two agree.
    ///
    /// The member's own birth, sex, name and country are kept, and so are the
    /// relatives `base` already has for the member: their parents (and the
    /// grandparents, if `base` has them), siblings and half-siblings, and the
    /// partners and children that `base` lists. Whatever `base` does not hold
    /// is drawn, as get_family() draws it: a partner, say, whom `base` left
    /// out is drawn again, and children whose other parent `base` left out
    /// are kept without that partner.
    ///
    /// Only what `base` lists is kept: a member that `base` generated with
    /// `partners = false` or `down = 0` has its partners or children drawn
    /// again. The request's `anchor` is ignored (`member` takes its place),
    /// and so is its `reference_date` (that of `base` is used); `up`, `down`,
    /// `siblings`, `partners`, `names`, `name_culture` and `weighted` apply.
    ///
    /// @param base The family the member belongs to.
    /// @param member The member's id (`base.members[member]`).
    /// @param request What to generate around the member.
    /// @param call_seed Seed; the same base, member, request and seed give the
    ///        same family.
    /// @return The member's family; its anchor is the member.
    /// @throws std::invalid_argument If `member` is not an id of `base`.
    [[nodiscard]] family expand(const family& base, std::size_t member, family_request request,
                                std::uint64_t call_seed) const
    {
        if (member >= base.members.size()) throw std::invalid_argument("expand: the member is not in the family");
        request.reference_date = base.reference_date;
        request.anchor = anchor_of_(base, member);
        return get_family(request, call_seed);
    }

    /// @brief Generate a random family of a member of a family; see the
    ///        seeded overload.
    /// @param base The family the member belongs to.
    /// @param member The member's id.
    /// @param request What to generate around the member.
    /// @return The member's family.
    [[nodiscard]] family expand(const family& base, std::size_t member, const family_request& request)
    {
        return expand(base, member, request, draw_seed_());
    }

    // -- Batch generation -------------------------------------------------

    /// @brief Generate a batch of family results from a seed drawn like
    ///        get_family() draws its own.
    ///
    /// Item `i` is `get_family(request, core::item_seed(batch_seed, options.first + i))`
    /// for the drawn batch seed; see the seeded overload.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @return The results, in index order.
    [[nodiscard]] std::vector<family> get_family_batch(const family_request& request,
                                                  const core::batch_options& options)
    {
        return get_family_batch(request, options, draw_seed_());
    }

    /// @brief Generate a batch of family results, deterministically.
    ///
    /// Item `i` is exactly `get_family(request, core::item_seed(batch_seed,
    /// options.first + i))`, so an item replays on its own, a range can be
    /// generated directly, two batches that follow each other equal one, and
    /// the output does not depend on `options.threads`.
    ///
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness (see
    ///        core::unique_rule; the rule must be made for `family`).
    /// @param batch_seed Seed of the whole batch.
    /// @return The results, in index order.
    /// @throws std::invalid_argument If the request or options are invalid.
    /// @throws std::runtime_error If no data has been loaded, or a unique
    ///         batch runs out of attempts.
    [[nodiscard]] std::vector<family> get_family_batch(const family_request& request,
                                                  const core::batch_options& options,
                                                  std::uint64_t batch_seed) const
    {
        return core::run_batch(options, batch_seed, [&](std::uint64_t item_seed)
                               { return get_family(request, item_seed); });
    }

    /// @brief Stream a batch of family results to a sink without holding the
    ///        whole batch.
    ///
    /// Generates the same items as get_family_batch() and calls
    /// `sink(index, item)` in index order on the calling thread; `index`
    /// includes `options.first`.
    ///
    /// @tparam Sink Callable taking `(std::uint64_t, family&)`; for example
    ///         core::jsonl_writer or core::csv_writer.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param batch_seed Seed of the whole batch.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_family(const family_request& request, const core::batch_options& options,
                        std::uint64_t batch_seed, Sink&& sink) const
    {
        core::for_each_batch(options, batch_seed,
                             [&](std::uint64_t item_seed) { return get_family(request, item_seed); },
                             std::forward<Sink>(sink));
    }

    /// @brief Stream a batch of family results with a batch seed drawn like
    ///        get_family() draws its own.
    /// @tparam Sink Callable taking `(std::uint64_t, family&)`.
    /// @param request What every item is generated from.
    /// @param options Count, first index, threads and uniqueness.
    /// @param sink Receives the items.
    template <typename Sink>
    void for_each_family(const family_request& request, const core::batch_options& options,
                        Sink&& sink)
    {
        for_each_family(request, options, draw_seed_(), std::forward<Sink>(sink));
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether this generator and birth-generator have data.
    /// @return True when a family can be generated.
    [[nodiscard]] bool has_data() const { return has_own_data_() && _from.birth->has_data(); }

    /// @brief Check whether the family tables are loaded (birth-generator
    ///        is not asked).
    /// @return True when fertility, parameters and defaults are non-empty.
    [[nodiscard]] bool has_family_data() const { return has_own_data_(); }

    /// @brief The countries with figures of their own in any table.
    /// @return ISO 3166-1 alpha-2 codes, sorted.
    [[nodiscard]] std::vector<std::string> countries() const
    {
        std::vector<std::string> out;
        out.reserve(_model.countries.size());
        for (const auto& [code, _] : _model.countries) out.push_back(code);
        std::ranges::sort(out);
        return out;
    }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// The directory must contain `regions.tsv`, `fertility.tsv`,
    /// `unions.tsv`, `defaults.tsv` and `parameters.tsv`; `households.tsv`
    /// is read when present. A path without them is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return;
        for (const auto t : required_tables_)
        {
            if (!std::filesystem::is_regular_file(dir / file_name_(t), ec)) return;
        }
        apply_(read_directory_(dir), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_RELATIONSHIP_EMBED`, otherwise searches `resources/`, `../resources`,
    /// `relationship-generator/resources` and `../relationship-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(family_table::fertility)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)},
                                                     embedded_files_(), root);
        }
        for (const auto t : required_tables_)
        {
            if (!docs.at(index_(t))) return false;
        }
        apply_(std::move(docs), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of the table files (see family_table); all are
    /// applied together or, if any fails, none. A file must have one of those
    /// names.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                (void)extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected one of the family tables (regions.tsv, fertility.tsv, "
                                    "unions.tsv, households.tsv, defaults.tsv, parameters.tsv), got " +
                                    path.filename().string());
    }

    /// @brief Add a data layer to one table.
    ///
    /// Rows are keyed by the table's key column (`cca2`, `subregion` or
    /// `key`). In the default override mode a row replaces the loaded row with
    /// the same key and new keys are added; a row with `disabled` set to 1
    /// removes its key.
    ///
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did (rows added, replaced, removed, warnings).
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(family_table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text (for example from a
    ///        WebAssembly host).
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(family_table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(family_table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data: this generator's tables and
    ///        birth-generator's data. Recorded in each result's signature().
    /// @return A hash that changes when either does; 0 when this
    ///         generator's tables are empty.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t own = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            own = core::hash_combine(own, t.data_hash());
        }
        if (!any) return 0;
        return core::hash_combine(core::hash_combine(own, _from.birth->data_hash()), _from.name->data_hash());
    }

  private:
    using table = family_table;

    // -- Tables -------------------------------------------------------------

    static constexpr std::string_view library_name_ = "relationship-generator";
    static constexpr std::size_t table_count_ = 6;
    static constexpr std::array<table, table_count_> all_tables_{table::regions,    table::fertility,
                                                                 table::unions,     table::households,
                                                                 table::defaults,   table::parameters};
    static constexpr std::array<table, 5> required_tables_{table::regions, table::fertility, table::unions,
                                                           table::defaults, table::parameters};
    static constexpr std::array<std::string_view, 8> age_columns_{
        "pasfr_10_14", "pasfr_15_19", "pasfr_20_24", "pasfr_25_29",
        "pasfr_30_34", "pasfr_35_39", "pasfr_40_44", "pasfr_45_49"};
    static constexpr std::array<int, 15> tfr_years_{1950, 1955, 1960, 1965, 1970, 1975, 1980, 1985,
                                                    1990, 1995, 2000, 2005, 2010, 2015, 2020};
    static constexpr double year_days_ = 365.2425;
    static constexpr double euler_gamma_ = 0.57721566490153286;

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;
    using cells = std::unordered_map<std::string, double>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        switch (t)
        {
        case table::regions: return "regions.tsv";
        case table::fertility: return "fertility.tsv";
        case table::unions: return "unions.tsv";
        case table::households: return "households.tsv";
        case table::defaults: return "defaults.tsv";
        case table::parameters: return "parameters.tsv";
        }
        return "";
    }

    // Every numeric column of the demography tables, by table.
    static std::vector<std::string> numeric_columns_(table t)
    {
        std::vector<std::string> cols;
        const auto fertility = [&]
        {
            cols.emplace_back("tfr");
            cols.emplace_back("mac");
            for (const auto c : age_columns_) cols.emplace_back(c);
            for (const int y : tfr_years_) cols.push_back("tfr_" + std::to_string(y));
        };
        const auto unions = [&]
        {
            for (const char* c : {"smam_m", "smam_f", "never_m_45_49", "never_f_45_49", "divorced_m_45_49",
                                  "divorced_f_45_49"})
            {
                cols.emplace_back(c);
            }
        };
        const auto households = [&]
        {
            for (const char* c : {"size", "one_person", "single_parent", "multi_generation"}) cols.emplace_back(c);
        };
        switch (t)
        {
        case table::fertility: fertility(); break;
        case table::unions: unions(); break;
        case table::households: households(); break;
        case table::defaults:
            fertility();
            unions();
            households();
            break;
        case table::regions:
        case table::parameters: break;
        }
        return cols;
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::regions: return {.key = {"cca2"}, .required = {"subregion"}, .optional = {}};
        case table::fertility:
        {
            core::table_schema s{.key = {"cca2"}, .required = {"tfr", "mac"}, .optional = {}};
            for (const auto c : age_columns_) s.required.emplace_back(c);
            for (const int y : tfr_years_) s.optional.push_back("tfr_" + std::to_string(y));
            return s;
        }
        case table::unions:
        {
            core::table_schema s{.key = {"cca2"}, .required = {}, .optional = {"year"}};
            for (auto& c : numeric_columns_(t)) s.optional.push_back(std::move(c));
            return s;
        }
        case table::households:
        {
            core::table_schema s{.key = {"cca2"}, .required = {}, .optional = {"year", "source"}};
            for (auto& c : numeric_columns_(t)) s.optional.push_back(std::move(c));
            return s;
        }
        case table::defaults:
        {
            core::table_schema s{.key = {"subregion"}, .required = {}, .optional = {"countries"}};
            for (auto& c : numeric_columns_(t)) s.optional.push_back(std::move(c));
            return s;
        }
        case table::parameters: return {.key = {"key"}, .required = {"value"}, .optional = {"note"}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::regions)}, core::layered_table{schema_(table::fertility)},
                core::layered_table{schema_(table::unions)},  core::layered_table{schema_(table::households)},
                core::layered_table{schema_(table::defaults)}, core::layered_table{schema_(table::parameters)}};
    }

    // -- Model --------------------------------------------------------------

    /// The constants of the model; the shipped `parameters.tsv` repeats these.
    struct parameters
    {
        double min_mother_age{12.0};
        double max_mother_age{49.9};
        double min_father_age{15.0};
        double max_father_age{80.0};
        double father_gap_sd{3.5};
        double max_gap_mean{8.0};
        double conception_lead{0.75};
        double birth_spacing{0.75};
        double gompertz_beta{0.09};
        double adult_intercept{26.0};
        double adult_slope{0.7};
        double child_q15_scale{0.012};
        double child_q15_decay{11.0};
        double dispersion_intercept{0.2};
        double dispersion_slope{0.27};
        double dispersion_min{0.3};
        double max_siblings{14.0};
        double male_birth_share{0.512};
        double min_union_age{16.0};
        double union_age_sd{4.5};
        double max_unions{3.0};
        double same_sex_share{0.03};
        double remarry_share{0.45};
        double divorce_duration_mean{9.0};
        double remarry_gap_mean{3.0};
        double union_fertility{1.0};
        double union_fertility_offset{0.2};
        double union_early_max{3.0};
        double later_union_factor{0.6};
        double adoption_factor{0.15};
        double half_sibling_factor{0.6};
        double half_sibling_extra{0.5};
        double married_name_share{0.8};
        double max_children{12.0};
        double twin_share{0.012};
        double stepchild_factor{0.6};
    };

    struct param_def
    {
        std::string_view key;
        double parameters::*member;
    };

    static constexpr std::array<param_def, 36> param_table_{{
        {"min_mother_age", &parameters::min_mother_age},
        {"max_mother_age", &parameters::max_mother_age},
        {"min_father_age", &parameters::min_father_age},
        {"max_father_age", &parameters::max_father_age},
        {"father_gap_sd", &parameters::father_gap_sd},
        {"max_gap_mean", &parameters::max_gap_mean},
        {"conception_lead", &parameters::conception_lead},
        {"birth_spacing", &parameters::birth_spacing},
        {"gompertz_beta", &parameters::gompertz_beta},
        {"adult_intercept", &parameters::adult_intercept},
        {"adult_slope", &parameters::adult_slope},
        {"child_q15_scale", &parameters::child_q15_scale},
        {"child_q15_decay", &parameters::child_q15_decay},
        {"dispersion_intercept", &parameters::dispersion_intercept},
        {"dispersion_slope", &parameters::dispersion_slope},
        {"dispersion_min", &parameters::dispersion_min},
        {"max_siblings", &parameters::max_siblings},
        {"male_birth_share", &parameters::male_birth_share},
        {"min_union_age", &parameters::min_union_age},
        {"union_age_sd", &parameters::union_age_sd},
        {"max_unions", &parameters::max_unions},
        {"same_sex_share", &parameters::same_sex_share},
        {"remarry_share", &parameters::remarry_share},
        {"divorce_duration_mean", &parameters::divorce_duration_mean},
        {"remarry_gap_mean", &parameters::remarry_gap_mean},
        {"union_fertility", &parameters::union_fertility},
        {"union_fertility_offset", &parameters::union_fertility_offset},
        {"union_early_max", &parameters::union_early_max},
        {"later_union_factor", &parameters::later_union_factor},
        {"adoption_factor", &parameters::adoption_factor},
        {"half_sibling_factor", &parameters::half_sibling_factor},
        {"half_sibling_extra", &parameters::half_sibling_extra},
        {"married_name_share", &parameters::married_name_share},
        {"max_children", &parameters::max_children},
        {"twin_share", &parameters::twin_share},
        {"stepchild_factor", &parameters::stepchild_factor},
    }};

    struct fertility_row
    {
        double tfr{0.0};
        std::vector<std::pair<int, double>> history; // year, tfr, ascending, ending with the current one
        std::optional<core::discrete> by_age;        // the eight five-year groups from 10-14
        data_source source{data_source::country};
    };

    struct union_row
    {
        double smam_m{0.0};
        double smam_f{0.0};
        double never_m{0.0};    // percent still never married at 45 to 49
        double never_f{0.0};
        double divorced_m{0.0}; // percent divorced or separated at 45 to 49
        double divorced_f{0.0};
        data_source source{data_source::country};
    };

    struct country_model
    {
        fertility_row fertility;
        union_row unions;
    };

    struct model
    {
        parameters params;
        std::unordered_map<std::string, country_model> countries;
        country_model world;
        bool ready{false};
    };

    static void check_number_(const core::tsv_row& row, std::string_view column)
    {
        if (row.has(column))
        {
            const double v = row.get<double>(column);
            if (!std::isfinite(v)) throw row.error(column, "must be a finite number");
        }
    }

    static void validate_(table t, const core::tsv_row& row)
    {
        switch (t)
        {
        case table::regions:
            if (row.get<std::string>("subregion").empty()) throw row.error("subregion", "must not be empty");
            return;
        case table::parameters:
        {
            const auto key = row.get<std::string>("key");
            if (std::ranges::none_of(param_table_, [&](const param_def& d) { return d.key == key; }))
            {
                throw row.error("key", "unknown parameter '" + key + "'");
            }
            const double v = row.get<double>("value");
            if (!std::isfinite(v) || v < 0.0) throw row.error("value", "must be a non-negative number");
            return;
        }
        case table::fertility:
        {
            const double tfr = row.get<double>("tfr");
            if (!(tfr > 0.0)) throw row.error("tfr", "must be positive");
            double sum = 0.0;
            for (const auto c : age_columns_)
            {
                const double v = row.get<double>(c);
                if (!(v >= 0.0)) throw row.error(c, "must not be negative");
                sum += v;
            }
            if (!(sum > 0.0)) throw row.error("pasfr_10_14", "the age distribution must not be all zero");
            break;
        }
        case table::unions:
        case table::households:
        case table::defaults: break;
        }
        const auto schema = schema_(t);
        for (const auto& c : numeric_columns_(t))
        {
            if (std::ranges::find(schema.optional, c) != schema.optional.end() ||
                std::ranges::find(schema.required, c) != schema.required.end())
            {
                check_number_(row, c);
            }
        }
    }

    static cells read_cells_(const core::tsv_row& row, table t)
    {
        cells out;
        const auto& schema_cols = schema_(t);
        for (const auto& c : numeric_columns_(t))
        {
            const bool known = std::ranges::find(schema_cols.optional, c) != schema_cols.optional.end() ||
                               std::ranges::find(schema_cols.required, c) != schema_cols.required.end();
            if (known && row.has(c)) out[c] = row.get<double>(c);
        }
        return out;
    }

    static std::unordered_map<std::string, cells> read_table_(const core::layered_table& tbl, table t)
    {
        std::unordered_map<std::string, cells> out;
        const std::string key = t == table::defaults ? "subregion" : "cca2";
        for (const auto& row : tbl.rows()) out[row.get<std::string>(key)] = read_cells_(row, t);
        return out;
    }

    // The first of the three sources that has the column, and which it was.
    struct lookup_cells
    {
        const cells* country;
        const cells* subregion;
        const cells* world;

        [[nodiscard]] double value(const std::string& column) const
        {
            for (const cells* c : {country, subregion, world})
            {
                if (c == nullptr) continue;
                const auto it = c->find(column);
                if (it != c->end()) return it->second;
            }
            throw std::runtime_error("relationship-generator: no value for '" + column +
                                     "' in the country, its subregion or the world row of defaults.tsv");
        }
    };

    static fertility_row make_fertility_(const lookup_cells& l, data_source source)
    {
        fertility_row r;
        r.source = source;
        r.tfr = l.value("tfr");
        std::vector<double> weights;
        for (const auto c : age_columns_) weights.push_back(l.value(std::string{c}));
        r.by_age.emplace(weights);
        for (const int y : tfr_years_)
        {
            try
            {
                r.history.emplace_back(y, l.value("tfr_" + std::to_string(y)));
            }
            catch (const std::runtime_error&)
            {
                // A period without a figure is skipped; the others interpolate across it.
            }
        }
        r.history.emplace_back(2023, r.tfr);
        return r;
    }

    static union_row make_union_(const lookup_cells& l, data_source source)
    {
        return {.smam_m = l.value("smam_m"),
                .smam_f = l.value("smam_f"),
                .never_m = l.value("never_m_45_49"),
                .never_f = l.value("never_f_45_49"),
                .divorced_m = l.value("divorced_m_45_49"),
                .divorced_f = l.value("divorced_f_45_49"),
                .source = source};
    }

    static model build_model_(const tables& tbls)
    {
        model m;
        for (const auto& row : tbls[index_(table::parameters)].rows())
        {
            const auto key = row.get<std::string>("key");
            const double v = row.get<double>("value");
            for (const auto& d : param_table_)
            {
                if (d.key == key) m.params.*d.member = v;
            }
        }
        if (m.params.gompertz_beta <= 0.0 || m.params.dispersion_min <= 0.0 || m.params.child_q15_decay <= 0.0 || m.params.birth_spacing <= 0.0 ||
            m.params.min_mother_age >= m.params.max_mother_age || m.params.male_birth_share > 1.0 ||
            m.params.twin_share < 0.0 || m.params.twin_share > 0.5 || m.params.stepchild_factor < 0.0)
        {
            throw std::runtime_error("relationship-generator: parameters.tsv has an impossible value");
        }

        std::unordered_map<std::string, std::string> region;
        for (const auto& row : tbls[index_(table::regions)].rows())
        {
            region[row.get<std::string>("cca2")] = row.get<std::string>("subregion");
        }
        const auto defaults = read_table_(tbls[index_(table::defaults)], table::defaults);
        const auto fert = read_table_(tbls[index_(table::fertility)], table::fertility);
        const auto uni = read_table_(tbls[index_(table::unions)], table::unions);
        const auto world = defaults.find("World");
        if (world == defaults.end()) return m; // not ready: has_own_data_() reports it

        const auto resolve = [&](const std::string& code, const std::unordered_map<std::string, cells>& own,
                                 table t) -> std::pair<lookup_cells, data_source>
        {
            const auto sub = region.find(code);
            const auto sub_cells = sub == region.end() ? defaults.end() : defaults.find(sub->second);
            const auto mine = own.find(code);
            lookup_cells l{.country = mine == own.end() ? nullptr : &mine->second,
                           .subregion = sub_cells == defaults.end() ? nullptr : &sub_cells->second,
                           .world = &world->second};
            (void)t;
            data_source src = data_source::world;
            if (l.country != nullptr) src = data_source::country;
            else if (l.subregion != nullptr) src = data_source::subregion;
            return {l, src};
        };

        std::vector<std::string> codes;
        for (const auto& [code, _] : region) codes.push_back(code);
        for (const auto& [code, _] : fert) codes.push_back(code);
        for (const auto& [code, _] : uni) codes.push_back(code);
        std::ranges::sort(codes);
        codes.erase(std::ranges::unique(codes).begin(), codes.end());

        for (const auto& code : codes)
        {
            const auto [fl, fs] = resolve(code, fert, table::fertility);
            const auto [ul, us] = resolve(code, uni, table::unions);
            m.countries.emplace(code, country_model{.fertility = make_fertility_(fl, fs), .unions = make_union_(ul, us)});
        }
        const lookup_cells only_world{.country = nullptr, .subregion = nullptr, .world = &world->second};
        m.world = {.fertility = make_fertility_(only_world, data_source::world),
                   .unions = make_union_(only_world, data_source::world)};
        m.ready = true;
        return m;
    }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec))
            {
                docs.at(index_(t)) = core::tsv_document::load(file);
            }
        }
        return docs;
    }

    void apply_(documents docs, core::layer_mode mode, bool reset)
    {
        tables next = _tables;
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        auto built = build_model_(next);
        _tables = std::move(next);
        _model = std::move(built);
    }

    // -- State --------------------------------------------------------------

    sources _from{};
    tables _tables{make_tables_()};
    model _model;
    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit relg(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_RELATIONSHIP_EMBED
        return embedded::relgen::files;
#else
        return {};
#endif
    }

    std::uint64_t draw_seed_() { return _engine(); }

    [[nodiscard]] bool has_own_data_() const { return _model.ready; }

    void require_data_() const
    {
        if (!has_data()) throw std::runtime_error("relationship-generator: this generator or birth-generator has no data loaded");
    }

    [[nodiscard]] const country_model& lookup_(const std::string& code) const
    {
        const auto it = _model.countries.find(code);
        return it == _model.countries.end() ? _model.world : it->second;
    }

    // -- Expansion ----------------------------------------------------------

    // A member of a family as a known person; with `above`, the parents too.
    [[nodiscard]] static known_person known_of_(const family& f, std::size_t id, bool above)
    {
        const auto& m = f.members.at(id);
        known_person k;
        k.bio_sex = m.bio_sex;
        k.birth = m.birth;
        k.death = m.death;
        if (!m.name.empty()) k.name = person_name{m.given, m.patronymic, m.family};
        if (above)
        {
            for (const auto& e : f.edges)
            {
                if (e.kind == edge_kind::parent_of && e.to == id) k.parents.push_back(known_of_(f, e.from, false));
            }
        }
        return k;
    }

    [[nodiscard]] static std::vector<std::size_t> parents_in_(const family& f, std::size_t id)
    {
        std::vector<std::size_t> out;
        for (const auto& e : f.edges)
        {
            if (e.kind == edge_kind::parent_of && e.to == id) out.push_back(e.from);
        }
        return out;
    }

    [[nodiscard]] static std::chrono::year_month_day mid_year_(int year)
    {
        return {std::chrono::year{year}, std::chrono::July, std::chrono::day{1}};
    }

    [[nodiscard]] static std::chrono::year_month_day year_before_(const std::chrono::year_month_day& d)
    {
        return std::chrono::year_month_day{std::chrono::sys_days{d} - std::chrono::days{365}};
    }

    // The anchor spec of a member: the member's own data and the relatives
    // the family holds.
    [[nodiscard]] static anchor_spec anchor_of_(const family& f, std::size_t id)
    {
        const auto& m = f.members.at(id);
        anchor_spec a;
        a.country_code = f.country_code;
        a.bio_sex = m.bio_sex;
        a.birth = m.birth;
        if (!m.name.empty()) a.name = person_name{m.given, m.patronymic, m.family};

        const auto parents = parents_in_(f, id);
        if (!parents.empty())
        {
            // A single parent takes the slot of its sex; two take them in the family's order.
            if (parents.size() == 1)
            {
                (f.members.at(parents[0]).bio_sex == sex::male ? a.father : a.mother) = known_of_(f, parents[0], true);
            }
            else
            {
                a.father = known_of_(f, parents[0], true);
                a.mother = known_of_(f, parents[1], true);
            }
            std::vector<known_sibling> sibs;
            for (const auto& e : f.edges)
            {
                if (e.kind != edge_kind::sibling_of || (e.from != id && e.to != id)) continue;
                const auto other = e.from == id ? e.to : e.from;
                known_sibling ks;
                ks.person = known_of_(f, other, false);
                ks.full = e.full;
                if (!e.full)
                {
                    const auto theirs = parents_in_(f, other);
                    for (const auto p : parents)
                    {
                        if (std::ranges::find(theirs, p) != theirs.end()) ks.through = f.members.at(p).bio_sex;
                    }
                }
                sibs.push_back(std::move(ks));
            }
            a.siblings = std::move(sibs);
        }

        // Unions: the anchor's are listed as edges; another member's are the
        // other parents of the children listed for them.
        std::vector<known_union> unions;
        const auto add_child = [&](known_union& u, std::size_t child)
        {
            known_child c{known_of_(f, child, false), true};
            for (const auto& e : f.edges)
            {
                if (e.kind == edge_kind::parent_of && e.to == child && e.from == id) c.genetic = e.genetic;
            }
            u.children.push_back(std::move(c));
        };
        std::vector<std::pair<std::optional<std::size_t>, std::size_t>> kids; // other parent (if listed), child
        for (const auto& e : f.edges)
        {
            if (e.kind != edge_kind::parent_of || e.from != id) continue;
            std::optional<std::size_t> other;
            for (const auto p : parents_in_(f, e.to))
            {
                if (p != id) other = p;
            }
            kids.emplace_back(other, e.to);
        }
        if (id == 0)
        {
            for (const auto& e : f.edges)
            {
                if (e.kind != edge_kind::partner_of) continue;
                known_union u;
                u.partner = known_of_(f, e.to, false);
                u.start = mid_year_(*e.start);
                if (e.ended != union_end::ongoing)
                {
                    u.end = (e.ended == union_end::death && u.partner->death) ? *u.partner->death : mid_year_(*e.end);
                }
                for (const auto& [other, child] : kids)
                {
                    if (other && *other == e.to) add_child(u, child);
                }
                // The partner's other children.
                for (const auto& pe : f.edges)
                {
                    if (pe.kind != edge_kind::parent_of || pe.from != e.to) continue;
                    if (f.members.at(pe.to).role == relation::stepchild) u.stepchildren.push_back(known_of_(f, pe.to, false));
                }
                unions.push_back(std::move(u));
            }
            known_union loose;
            for (const auto& [other, child] : kids)
            {
                if (!other) add_child(loose, child);
            }
            if (!loose.children.empty())
            {
                loose.start = year_before_(loose.children.front().person.birth);
                unions.push_back(std::move(loose));
            }
        }
        else
        {
            std::vector<std::optional<std::size_t>> seen;
            for (const auto& [other, child] : kids)
            {
                if (std::ranges::find(seen, other) != seen.end()) continue;
                seen.push_back(other);
                known_union u;
                if (other) u.partner = known_of_(f, *other, false);
                for (const auto& [o2, c2] : kids)
                {
                    if (o2 == other) add_child(u, c2);
                }
                u.start = year_before_(u.children.front().person.birth);
                if (u.partner && u.partner->death) u.end = u.partner->death;
                unions.push_back(std::move(u));
            }
        }
        if (!unions.empty()) a.unions = std::move(unions);
        return a;
    }

    // -- Drawing ------------------------------------------------------------

    static constexpr double never_dies_ = 1000.0;
    static constexpr double max_age_ = 122.0;
    static constexpr double child_age_ = 15.0;
    static constexpr std::int64_t none_ = -1;
    static constexpr std::size_t no_member_ = static_cast<std::size_t>(-1);
    // Half-siblings through one parent born this many years apart or less have the same other parent.
    static constexpr double half_cluster_years_ = 4.0;
    // A parent slot at or above this is a person who is not a member (the other
    // parent of a half-sibling, a partner left out of the list).
    static constexpr std::int64_t unlisted_ = 100000;

    /// A life: the birth as a day count since 1970-01-01 and the age at death
    /// in years. The person is alive at a day if the death falls after it.
    struct life
    {
        double birth{0.0};
        double lifespan{0.0};
        sex s{sex::male};
        int index{0}; // the draw that made this life, which names its streams
    };

    struct raw_member
    {
        relation role{relation::anchor};
        life life;
        std::string key;                                    // names the stream of the person's name
        std::array<std::int64_t, 2> parents{none_, none_};  // father (or first) and mother (or second), as ids or tokens
        std::int64_t name_child{none_};                     // for a parent: the member whose name theirs follows
        bool genetic{true};
        std::optional<person_name> fixed{};                 // NOLINT(readability-redundant-member-init) a name given by the caller
    };

    struct anchor_info
    {
        std::string country;
        sex s{sex::male};
        double birth_day{0.0};
    };

    struct lineage
    {
        life father;
        life mother;
        std::vector<life> siblings;
        std::vector<double> mother_births; // the mother's births (days): the anchor's and the siblings'
    };

    // Parents the caller knows; the other is drawn to match.
    struct known_parents
    {
        const known_person* father;
        const known_person* mother;
    };

    struct union_rec
    {
        life partner;
        double start{0.0};
        double end{0.0};
        union_end ended{union_end::ongoing};
        bool same_sex{false};
        bool has_partner{true}; // false for a union known by its children alone
        std::optional<person_name> partner_name{}; // NOLINT(readability-redundant-member-init)
    };

    struct child_rec
    {
        life life;
        int union_index{0};
        bool genetic{true};
        std::optional<person_name> fixed{}; // NOLINT(readability-redundant-member-init)
    };

    static std::int64_t days_(const std::chrono::year_month_day& d)
    {
        return std::chrono::sys_days{d}.time_since_epoch().count();
    }

    static std::chrono::year_month_day from_days_(double day)
    {
        return std::chrono::year_month_day{
            std::chrono::sys_days{std::chrono::days{static_cast<std::int64_t>(std::floor(day))}}};
    }

    static double clamp_(double v, double lo, double hi) { return std::min(std::max(v, lo), hi); }

    // A day `years` before or after another, the product fenced against FMA.
    static double back_(double day, double years) { return day - core::math::fenced(years * year_days_); }
    static double forward_(double day, double years) { return day + core::math::fenced(years * year_days_); }

    // Completed years from a birth to a date.
    static int calendar_age_(const std::chrono::year_month_day& birth, const std::chrono::year_month_day& at)
    {
        int age = static_cast<int>(at.year()) - static_cast<int>(birth.year());
        if (at.month() < birth.month() || (at.month() == birth.month() && at.day() < birth.day())) --age;
        return std::max(age, 0);
    }

    static std::size_t push_(std::vector<raw_member>& raw, relation role, const life& l, std::string key)
    {
        raw.push_back({.role = role, .life = l, .key = std::move(key)});
        return raw.size() - 1;
    }

    static person to_person_(const raw_member& m, std::size_t id, double ref_day)
    {
        person p;
        p.id = id;
        p.role = m.role;
        p.bio_sex = m.life.s;
        p.birth = from_days_(m.life.birth);
        const double death_day = forward_(m.life.birth, m.life.lifespan);
        p.alive = death_day > ref_day;
        if (p.alive)
        {
            p.age = static_cast<std::uint8_t>(std::min(calendar_age_(p.birth, from_days_(ref_day)), 255));
        }
        else
        {
            p.death = from_days_(death_day);
            p.age = static_cast<std::uint8_t>(std::min(calendar_age_(p.birth, *p.death), 255));
        }
        return p;
    }

    // Names of the people the caller gave need a given name.
    static void check_name_(const std::optional<person_name>& n)
    {
        if (n && n->given.empty()) throw std::invalid_argument("a known person's name needs a given name");
    }

    static void check_person_(const known_person& k)
    {
        check_name_(k.name);
        for (const auto& parent : k.parents) check_person_(parent);
    }

    static void check_known_(const anchor_spec& a)
    {
        check_name_(a.name);
        if (a.father) check_person_(*a.father);
        if (a.mother) check_person_(*a.mother);
        for (const auto& s : a.siblings.value_or(std::vector<known_sibling>{})) check_person_(s.person);
        for (const auto& u : a.unions.value_or(std::vector<known_union>{}))
        {
            if (u.partner) check_person_(*u.partner);
            for (const auto& c : u.children) check_person_(c.person);
        }
    }

    // A life from someone the caller knows: dead before the reference date if
    // a death is given, alive otherwise.
    [[nodiscard]] static life life_of_(const known_person& k, double ref_day, int index)
    {
        if (!k.birth.ok()) throw std::invalid_argument("a known person's birth is not a date");
        const double birth = static_cast<double>(days_(k.birth));
        double lifespan = never_dies_;
        if (k.death)
        {
            if (!k.death->ok()) throw std::invalid_argument("a known person's death is not a date");
            const double d = static_cast<double>(days_(*k.death));
            if (d < birth) throw std::invalid_argument("a known person died before being born");
            // Just past the day, so that the date of death comes back as given.
            if (d < ref_day) lifespan = ((d - birth) / year_days_) + 1e-6;
        }
        return {.birth = birth, .lifespan = lifespan, .s = k.bio_sex, .index = index};
    }

    // A union the caller knows, as a drawn one.
    [[nodiscard]] static union_rec union_of_(const known_union& k, std::size_t j, sex anchor_sex, double ref_day)
    {
        if (!k.start.ok()) throw std::invalid_argument("a known union's start is not a date");
        union_rec u;
        if (k.partner)
        {
            u.partner = life_of_(*k.partner, ref_day, static_cast<int>(j));
            u.partner_name = k.partner->name;
            u.same_sex = k.partner->bio_sex == anchor_sex;
        }
        else
        {
            u.partner = {.birth = 0.0, .lifespan = never_dies_, .s = anchor_sex == sex::male ? sex::female : sex::male,
                         .index = static_cast<int>(j)};
            u.has_partner = false;
        }
        u.start = static_cast<double>(days_(k.start));
        u.end = ref_day;
        if (k.end)
        {
            if (!k.end->ok()) throw std::invalid_argument("a known union's end is not a date");
            const double e = static_cast<double>(days_(*k.end));
            if (e < u.start) throw std::invalid_argument("a known union ends before it begins");
            if (e < ref_day)
            {
                u.end = e;
                const bool partner_died =
                    k.partner && k.partner->death && static_cast<double>(days_(*k.partner->death)) <= e;
                u.ended = partner_died ? union_end::death : union_end::divorce;
            }
        }
        return u;
    }

    // The children of known unions.
    [[nodiscard]] static std::vector<child_rec> known_children_(const std::vector<known_union>& unions, double ref_day)
    {
        std::vector<child_rec> out;
        for (std::size_t j = 0; j < unions.size(); ++j)
        {
            for (std::size_t i = 0; i < unions[j].children.size(); ++i)
            {
                const auto& c = unions[j].children[i];
                out.push_back({.life = life_of_(c.person, ref_day, static_cast<int>(i)),
                               .union_index = static_cast<int>(j),
                               .genetic = c.genetic,
                               .fixed = c.person.name});
            }
        }
        return out;
    }

    [[nodiscard]] anchor_info draw_anchor_(const family_request& request, std::uint64_t seed, double ref_day) const
    {
        const auto& spec = request.anchor;
        if (spec.birth && !spec.birth->ok()) throw std::invalid_argument("family_request.anchor.birth is not a date");
        if (spec.age && (*spec.age < 0 || *spec.age > static_cast<int>(max_age_)))
        {
            throw std::invalid_argument("family_request.anchor.age must be 0 to 122");
        }
        std::optional<std::string> country;
        if (spec.country_code)
        {
            std::string code = *spec.country_code;
            for (char& c : code) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            const auto known = _from.birth->countries();
            if (std::find(known.begin(), known.end(), code) == known.end())
            {
                throw std::invalid_argument("relationship-generator: unknown country code: " + *spec.country_code);
            }
            country = std::move(code);
        }
        anchor_info a;
        if (spec.birth && country && spec.bio_sex)
        {
            if (calendar_age_(*spec.birth, from_days_(ref_day)) > static_cast<int>(max_age_))
            {
                throw std::invalid_argument("the anchor would be older than 122 at the reference date");
            }
            a.country = *country;
            a.s = *spec.bio_sex;
            a.birth_day = static_cast<double>(days_(*spec.birth));
        }
        else
        {
            birth_request br;
            br.country_code = country;
            br.bio_sex = spec.bio_sex;
            br.weighted = request.weighted;
            br.reference_date = request.reference_date;
            if (spec.birth)
            {
                br.year = year_t{static_cast<std::uint16_t>(static_cast<int>(spec.birth->year()))};
            }
            else if (spec.age)
            {
                const auto age = static_cast<std::uint8_t>(*spec.age);
                br.ages = age_range{.min = age, .max = age};
            }
            const auto b = _from.birth->get_birth(br, seed);
            a.country = b.country_code;
            a.s = b.bio_sex;
            a.birth_day = static_cast<double>(days_(
                spec.birth ? *spec.birth
                           : std::chrono::year_month_day{std::chrono::year{b.year},
                                                         std::chrono::month{b.month}, std::chrono::day{b.day}}));
        }
        if (a.birth_day > ref_day) throw std::invalid_argument("the anchor is born after the reference date");
        return a;
    }

    // Age at death of a person known to be alive until `alive_until`.
    //
    // Two parts. Childhood: the chance of dying before 15 is `q15`, from the
    // life expectancy (about 1% at 80 and 14% at 53), and a child who dies
    // does so early (a squared uniform age). Adulthood: from 15, a Gompertz
    // curve, S(x) = exp(-c (e^(beta x) - 1)), whose mean age at death is the
    // typical one for that life expectancy (`adult_intercept + adult_slope *
    // e0`, a line through the Coale-Demeny west model life tables) and whose
    // mode sits euler_gamma / beta above its mean. Fitting the curve to the
    // life expectancy at birth instead would put adults' deaths far too early
    // where child deaths are common. Inverting S(x) / S(alive_until) = u gives
    // the adult draw.
    [[nodiscard]] life draw_life_(core::rng r, const std::string& country, sex s, double birth_day,
                                  double alive_until) const
    {
        const auto& p = _model.params;
        const double e0 = _from.birth->life_expectancy(country, s);
        const double q15 = std::min(0.9, p.child_q15_scale * core::math::exp((80.0 - e0) / p.child_q15_decay));
        const double beta = p.gompertz_beta;
        const double adult_mean = p.adult_intercept + core::math::fenced(p.adult_slope * e0);
        const double mode = adult_mean + (euler_gamma_ / beta);
        const double c = core::math::exp(-(beta * mode));
        const double child_roll = core::uniform01(r);
        const double within = core::uniform01(r);
        const double u = 1.0 - core::uniform01(r); // (0, 1]
        if (alive_until < child_age_ && child_roll < q15 * ((child_age_ - alive_until) / child_age_))
        {
            return {.birth = birth_day,
                    .lifespan = alive_until + core::math::fenced(((child_age_ - alive_until) * within) * within),
                    .s = s};
        }
        const double from = std::max(alive_until, child_age_);
        const double grown = core::math::exp(beta * from) - (core::math::log(u) / c);
        const double lifespan = core::math::log(grown) / beta;
        return {.birth = birth_day, .lifespan = clamp_(lifespan, from, max_age_), .s = s};
    }

    // A mother's age at a birth, from the five-year group weights and uniform
    // within the group, inside the fertile years.
    [[nodiscard]] double draw_mother_age_(const fertility_row& f, core::rng& r) const
    {
        const auto& p = _model.params;
        double a = p.min_mother_age;
        for (int i = 0; i < 12; ++i)
        {
            const double group = 10.0 + (5.0 * static_cast<double>((*f.by_age)(r)));
            a = group + core::math::fenced(5.0 * core::uniform01(r));
            if (a >= p.min_mother_age && a <= p.max_mother_age) return a;
        }
        return clamp_(a, p.min_mother_age, p.max_mother_age);
    }

    // The total fertility rate in a (fractional) year, between the periods on file.
    [[nodiscard]] static double tfr_at_(const fertility_row& f, double year)
    {
        const auto& h = f.history;
        if (year <= static_cast<double>(h.front().first)) return h.front().second;
        for (std::size_t i = 1; i < h.size(); ++i)
        {
            if (year <= static_cast<double>(h[i].first))
            {
                const double y0 = static_cast<double>(h[i - 1].first);
                const double t = (year - y0) / (static_cast<double>(h[i].first) - y0);
                return h[i - 1].second + core::math::fenced(t * (h[i].second - h[i - 1].second));
            }
        }
        return h.back().second;
    }

    // Marsaglia and Tsang (2000) for a gamma variate; a shape below 1 uses the
    // shape + 1 variate times u^(1 / shape).
    [[nodiscard]] static double gamma_(core::rng& r, double shape, double scale)
    {
        if (shape < 1.0)
        {
            const double u = 1.0 - core::uniform01(r); // (0, 1]
            return gamma_(r, shape + 1.0, scale) * core::math::exp(core::math::log(u) / shape);
        }
        const double d = shape - (1.0 / 3.0);
        const double c = 1.0 / std::sqrt(9.0 * d);
        for (;;)
        {
            const double x = core::normal(r, 0.0, 1.0);
            const double t = 1.0 + core::math::fenced(c * x);
            if (t <= 0.0) continue;
            const double v = t * t * t;
            const double u = core::uniform01(r);
            if (u <= 0.0) continue;
            const double bound = core::math::fenced(0.5 * x * x) + d - core::math::fenced(d * v) +
                                 core::math::fenced(d * core::math::log(v));
            if (core::math::log(u) < bound) return d * v * scale;
        }
    }

    // Knuth's multiplication method.
    [[nodiscard]] static int poisson_(core::rng& r, double lambda)
    {
        const double limit = core::math::exp(-std::min(lambda, 40.0));
        double p = 1.0;
        int k = 0;
        do
        {
            ++k;
            p *= core::uniform01(r);
        } while (p > limit && k < 200);
        return k - 1;
    }

    [[nodiscard]] static double exponential_(core::rng& r, double mean)
    {
        return -(mean * core::math::log(1.0 - core::uniform01(r)));
    }

    // How spread out family sizes are, as variance over mean, for a mean size.
    [[nodiscard]] double dispersion_(double mean) const
    {
        const auto& p = _model.params;
        return std::max(p.dispersion_min, p.dispersion_intercept + (p.dispersion_slope * mean));
    }

    // The number of children of a family of mean size `mean`.
    // The spread of family sizes, as variance over mean `d`, grows with
    // fertility: families are close in size when few children are born
    // (d < 1) and some are much larger when many are (d > 1).
    //  - d < 0.9: binomial, with p = 1 - d and n = mean / p;
    //  - d near 1: Poisson;
    //  - d > 1.05: negative binomial, a Poisson with a gamma-distributed
    //    mean of shape k = mean / (d - 1).
    [[nodiscard]] int family_size_(core::rng& r, double mean) const
    {
        if (mean <= 0.0) return 0;
        const double d = dispersion_(mean);
        if (d < 0.9)
        {
            const int n = std::max(1, static_cast<int>(std::lround(mean / (1.0 - d))));
            const double prob = std::min(1.0, mean / static_cast<double>(n));
            int size = 0;
            for (int i = 0; i < n; ++i) size += core::bernoulli(r, prob) ? 1 : 0;
            return size;
        }
        if (d <= 1.05) return poisson_(r, mean);
        const double k = mean / (d - 1.0);
        return poisson_(r, gamma_(r, k, mean / k));
    }

    // The number of siblings of a child of a family whose mean size is `mean`.
    //
    // Seen from a child, a family is size-biased: a family of n children is n
    // times as likely to be the one drawn, so the siblings are one fewer than
    // a size-biased family size. The size-biased forms of the three
    // distributions of family_size_() are
    //  - binomial: 1 + Binomial(n - 1, p), so the siblings are Binomial(n - 1, p);
    //  - Poisson: 1 + Poisson(mean), so the siblings are Poisson(mean);
    //  - negative binomial: 1 + Poisson(gamma(k + 1, mean / k)).
    [[nodiscard]] int sibling_count_(core::rng& r, double mean) const
    {
        const double d = dispersion_(mean);
        if (d < 0.9)
        {
            const int n = std::max(1, static_cast<int>(std::lround(mean / (1.0 - d))));
            const double prob = std::min(1.0, mean / static_cast<double>(n));
            int siblings = 0;
            for (int i = 1; i < n; ++i) siblings += core::bernoulli(r, prob) ? 1 : 0;
            return siblings;
        }
        if (d <= 1.05) return poisson_(r, mean);
        const double k = mean / (d - 1.0);
        return poisson_(r, gamma_(r, k + 1.0, mean / k));
    }

    // The parents of a person born on `child_birth`, and optionally the
    // person's brothers and sisters. Parents the caller knows are used as
    // they are; the other is drawn to sit at the usual gap from them.
    [[nodiscard]] lineage draw_lineage_(const country_model& cm, const std::string& country, const core::rng& r,
                                        double child_birth, double ref_day, bool with_siblings,
                                        const known_parents& known) const
    {
        const auto& p = _model.params;
        lineage out;
        auto age_rng = r.split("mother-age");
        double mother_age = draw_mother_age_(cm.fertility, age_rng);
        auto gap_rng = r.split("father-gap");
        const double mean_gap = clamp_(cm.unions.smam_m - cm.unions.smam_f, 0.0, p.max_gap_mean);
        double father_age = clamp_(mother_age + core::normal(gap_rng, mean_gap, p.father_gap_sd),
                                   p.min_father_age, p.max_father_age);
        const double drawn_gap = father_age - mother_age;
        std::optional<life> known_mother;
        std::optional<life> known_father;
        if (known.mother != nullptr)
        {
            known_mother = life_of_(*known.mother, ref_day, 0);
            mother_age = (child_birth - known_mother->birth) / year_days_;
        }
        if (known.father != nullptr)
        {
            known_father = life_of_(*known.father, ref_day, 0);
            father_age = (child_birth - known_father->birth) / year_days_;
        }
        if (known_mother && !known_father) father_age = clamp_(mother_age + drawn_gap, p.min_father_age, p.max_father_age);
        if (known_father && !known_mother) mother_age = clamp_(father_age - drawn_gap, p.min_mother_age, p.max_mother_age);
        if ((known_mother && mother_age <= 0.0) || (known_father && father_age <= 0.0))
        {
            throw std::invalid_argument("a known parent is not born before the child");
        }
        out.mother = known_mother ? *known_mother
                                  : draw_life_(r.split("mother-life"), country, sex::female,
                                               back_(child_birth, mother_age), mother_age);
        out.father = known_father ? *known_father
                                  : draw_life_(r.split("father-life"), country, sex::male,
                                               back_(child_birth, father_age),
                                               std::max(0.0, father_age - p.conception_lead));
        out.mother_births.push_back(child_birth);
        if (!with_siblings) return out;

        auto count_rng = r.split("sibling-count");
        const double mean = tfr_at_(cm.fertility, 1970.0 + (child_birth / year_days_));
        const int wanted = std::min(sibling_count_(count_rng, mean), static_cast<int>(p.max_siblings));

        // Each sibling is placed by the mother's age at the birth, and left out
        // if no age fits: before she could have a child, after she died or the
        // father died, after the reference date, or too close to another birth.
        std::vector<double> mother_ages{mother_age};
        const auto siblings = r.split("siblings");
        for (int i = 0; i < wanted; ++i)
        {
            auto ri = siblings.split(static_cast<std::uint64_t>(i));
            for (int attempt = 0; attempt < 12; ++attempt)
            {
                const double a = draw_mother_age_(cm.fertility, ri);
                const double birth = forward_(child_birth, a - mother_age);
                const double fa = father_age + (a - mother_age);
                if (a >= out.mother.lifespan || birth > ref_day || fa < p.min_father_age || fa > p.max_father_age ||
                    fa - p.conception_lead >= out.father.lifespan)
                {
                    continue;
                }
                if (std::ranges::any_of(mother_ages, [&](double e) { return std::abs(a - e) < p.birth_spacing; }))
                {
                    continue;
                }
                mother_ages.push_back(a);
                out.mother_births.push_back(birth);
                const sex s = core::bernoulli(ri, p.male_birth_share) ? sex::male : sex::female;
                auto l = draw_life_(ri.split("life"), country, s, birth, 0.0);
                l.index = i;
                out.siblings.push_back(l);
                if (const auto t = twin_of_(ri, country, l)) out.siblings.push_back(*t);
                break;
            }
        }
        // The anchor's own twin: a person is twice as likely to be one of a
        // pair as a delivery is to be a twin delivery.
        auto twin_rng = r.split("anchor-twin");
        if (p.max_siblings >= 1.0 && core::bernoulli(twin_rng, 2.0 * p.twin_share))
        {
            const sex s = core::bernoulli(twin_rng, p.male_birth_share) ? sex::male : sex::female;
            auto l = draw_life_(twin_rng.split("life"), country, s, child_birth, 0.0);
            l.index = 1000;
            out.siblings.push_back(l);
        }
        return out;
    }

    // Another child born with `l`, with the chance of a twin delivery.
    [[nodiscard]] std::optional<life> twin_of_(core::rng& r, const std::string& country, const life& l) const
    {
        const auto& p = _model.params;
        if (!core::bernoulli(r, p.twin_share)) return std::nullopt;
        const sex s = core::bernoulli(r, p.male_birth_share) ? sex::male : sex::female;
        auto t = draw_life_(r.split("twin"), country, s, l.birth, 0.0);
        t.index = 1000 + l.index;
        return t;
    }

    // Children of one person alone, the other parent being left out: placed by
    // the parent's age at each birth (a mother's from the fertility by age, a
    // father's from a mother's age and the usual gap), and left out if no age
    // fits (after the reference date or the parent's death, or, for a mother,
    // too close to another of her births, `mother_births`).
    [[nodiscard]] std::vector<life> draw_offspring_(const country_model& cm, const std::string& country,
                                                    const life& parent, int count,
                                                    std::vector<double> mother_births, double ref_day,
                                                    const core::rng& r) const
    {
        const auto& p = _model.params;
        std::vector<life> out;
        const bool female = parent.s == sex::female;
        const double mean_gap = clamp_(cm.unions.smam_m - cm.unions.smam_f, 0.0, p.max_gap_mean);
        for (int i = 0; i < count; ++i)
        {
            auto ri = r.split(static_cast<std::uint64_t>(i));
            for (int attempt = 0; attempt < 12; ++attempt)
            {
                const double am = draw_mother_age_(cm.fertility, ri);
                const double pa = female ? am
                                         : clamp_(am + core::normal(ri, mean_gap, p.father_gap_sd), p.min_father_age,
                                                  p.max_father_age);
                const double birth = forward_(parent.birth, pa);
                if (birth > ref_day || (female ? pa >= parent.lifespan : pa - p.conception_lead >= parent.lifespan))
                {
                    continue;
                }
                if (female && std::ranges::any_of(mother_births, [&](double e)
                                                  { return std::abs(birth - e) < p.birth_spacing * year_days_; }))
                {
                    continue;
                }
                if (female) mother_births.push_back(birth);
                const sex s = core::bernoulli(ri, p.male_birth_share) ? sex::male : sex::female;
                auto l = draw_life_(ri.split("life"), country, s, birth, 0.0);
                l.index = i;
                out.push_back(l);
                if (const auto t = twin_of_(ri, country, l)) out.push_back(*t);
                break;
            }
        }
        return out;
    }

    // The half-siblings a parent gives the anchor with someone else: some of
    // the parents' unions end in a separation, and a share of those parents
    // have children with another partner (divorced share among the ever
    // married, times `half_sibling_factor`).
    [[nodiscard]] std::vector<life> draw_half_siblings_(const country_model& cm, const std::string& country,
                                                        const life& parent, bool father,
                                                        std::vector<double> mother_births, double ref_day,
                                                        const core::rng& r) const
    {
        const auto& p = _model.params;
        const double never = father ? cm.unions.never_m : cm.unions.never_f;
        const double divorced = father ? cm.unions.divorced_m : cm.unions.divorced_f;
        const double chance = clamp_(p.half_sibling_factor * divorced / std::max(100.0 - never, 10.0), 0.0, 0.9);
        auto roll = r.split("roll");
        if (!core::bernoulli(roll, chance)) return {};
        const int count = std::min(1 + poisson_(roll, p.half_sibling_extra), static_cast<int>(p.max_siblings));
        return draw_offspring_(cm, country, parent, count, std::move(mother_births), ref_day, r.split("births"));
    }

    // The anchor's grandchildren through one child.
    [[nodiscard]] std::vector<life> draw_grandchildren_(const country_model& cm, const std::string& country,
                                                        const life& child, double ref_day, const core::rng& r) const
    {
        const auto& p = _model.params;
        auto count_rng = r.split("count");
        const double mean = tfr_at_(cm.fertility, 1970.0 + (child.birth / year_days_) + 28.0);
        const int count = std::min(family_size_(count_rng, mean), static_cast<int>(p.max_children));
        return draw_offspring_(cm, country, child, count, {}, ref_day, r.split("births"));
    }

    // The children a partner had before the union began, with someone else:
    // the partner's divorced share among the ever married, times
    // `stepchild_factor`, gives the chance, and the births are placed by the
    // partner's age and end before the union.
    [[nodiscard]] std::vector<life> draw_stepchildren_(const country_model& cm, const std::string& country,
                                                       const union_rec& u, double ref_day, const core::rng& r) const
    {
        const auto& p = _model.params;
        const bool male = u.partner.s == sex::male;
        const double never = male ? cm.unions.never_m : cm.unions.never_f;
        const double divorced = male ? cm.unions.divorced_m : cm.unions.divorced_f;
        const double chance = clamp_(p.stepchild_factor * divorced / std::max(100.0 - never, 10.0), 0.0, 0.9);
        auto roll = r.split("roll");
        if (!core::bernoulli(roll, chance)) return {};
        const int count = std::min(1 + poisson_(roll, p.half_sibling_extra), static_cast<int>(p.max_children));
        return draw_offspring_(cm, country, u.partner, count, {}, std::min(u.start - 0.75 * year_days_, ref_day),
                               r.split("births"));
    }

    // The anchor's partners, in order. Whether the anchor ever has one comes
    // from the share still never married at 45 to 49; the first union begins
    // around the mean age at first marriage; the partner is of the other sex
    // unless the union is same-sex (`same_sex_share`), older or younger by the
    // marriage-age gap; a union ends in a separation with the divorced share
    // among the ever married, after a spell, or at the partner's death; after
    // an ending the anchor may begin another (`remarry_share`).
    [[nodiscard]] std::vector<union_rec> draw_unions_(const country_model& cm, const std::string& country,
                                                      const life& a, double ref_day, const core::rng& r) const
    {
        const auto& p = _model.params;
        std::vector<union_rec> out;
        const bool male = a.s == sex::male;
        const double never = male ? cm.unions.never_m : cm.unions.never_f;
        const double divorced = male ? cm.unions.divorced_m : cm.unions.divorced_f;
        auto ever = r.split("ever");
        if (core::bernoulli(ever, clamp_(never / 100.0, 0.0, 1.0))) return out;
        const double p_divorce = clamp_(divorced / std::max(100.0 - never, 10.0), 0.0, 0.7);
        const double mean_gap = clamp_(cm.unions.smam_m - cm.unions.smam_f, 0.0, p.max_gap_mean);
        const double smam = male ? cm.unions.smam_m : cm.unions.smam_f;
        auto age_rng = r.split("start-age");
        // Unions begin earlier than first marriages where people cohabit first:
        // by half the years the mean age at marriage is above 21, up to `union_early_max`.
        const double earlier = clamp_(0.5 * (smam - 21.0), 0.0, p.union_early_max);
        double start = forward_(a.birth, clamp_(core::normal(age_rng, smam - earlier, p.union_age_sd),
                                                p.min_union_age, 60.0));
        for (int j = 0; j < static_cast<int>(p.max_unions) && start <= ref_day; ++j)
        {
            const auto uj = r.split("union").split(static_cast<std::uint64_t>(j));
            union_rec u;
            u.start = start;
            auto sex_rng = uj.split("sex");
            u.same_sex = core::bernoulli(sex_rng, p.same_sex_share);
            const sex ps = u.same_sex ? a.s : (male ? sex::female : sex::male);
            auto gap_rng = uj.split("gap");
            const double signed_gap = u.same_sex ? 0.0 : (male ? -mean_gap : mean_gap);
            const double my_age = (start - a.birth) / year_days_;
            const double partner_age =
                std::max(p.min_union_age, my_age + core::normal(gap_rng, signed_gap, p.father_gap_sd));
            u.partner = draw_life_(uj.split("life"), country, ps, back_(start, partner_age), partner_age);
            const double partner_death = forward_(u.partner.birth, u.partner.lifespan);
            u.end = ref_day;
            auto end_rng = uj.split("end");
            const bool separates = core::bernoulli(end_rng, p_divorce);
            const double separation = forward_(start, 0.5 + exponential_(end_rng, p.divorce_duration_mean));
            if (separates && separation < std::min(partner_death, ref_day))
            {
                u.end = separation;
                u.ended = union_end::divorce;
            }
            else if (partner_death <= ref_day)
            {
                u.end = partner_death;
                u.ended = union_end::death;
            }
            out.push_back(u);
            if (u.ended == union_end::ongoing) break;
            auto again = uj.split("remarry");
            if (!core::bernoulli(again, p.remarry_share)) break;
            start = forward_(u.end, 0.5 + exponential_(again, p.remarry_gap_mean));
        }
        return out;
    }

    // The anchor's children by their unions. A different-sex union has the
    // woman's children born inside it (her age at each birth from the
    // fertility by age, left out if no age fits: the father dead nine months
    // before, her or his age out of range, too close to another of her births,
    // or outside the union) and a mean size that is the fertility rate of the
    // year it began, a later union's scaled by `later_union_factor`. A
    // same-sex union has adopted children instead, a fraction of that mean
    // (`adoption_factor`), born up to two years before it began.
    [[nodiscard]] std::vector<child_rec> draw_union_children_(const country_model& cm, const std::string& country,
                                                              const life& a, const std::vector<union_rec>& unions,
                                                              double ref_day, const core::rng& r) const
    {
        const auto& p = _model.params;
        std::vector<child_rec> out;
        std::vector<double> anchor_births; // days; the anchor's, when she is the mother, across unions
        for (std::size_t j = 0; j < unions.size(); ++j)
        {
            const auto& u = unions[j];
            const auto uj = r.split("union").split(static_cast<std::uint64_t>(j));
            const double window_end = std::min(u.end, ref_day);
            double mean = (tfr_at_(cm.fertility, 1970.0 + (u.start / year_days_)) + p.union_fertility_offset) *
                          p.union_fertility;
            if (j > 0) mean *= p.later_union_factor;
            auto count_rng = uj.split("count");
            if (u.same_sex)
            {
                const int n = std::min(family_size_(count_rng, mean * p.adoption_factor), static_cast<int>(p.max_children));
                const double lo = back_(u.start, 2.0);
                for (int i = 0; i < n && window_end > lo; ++i)
                {
                    auto ci = uj.split("adopted").split(static_cast<std::uint64_t>(i));
                    const double birth = lo + core::math::fenced(core::uniform01(ci) * (window_end - lo));
                    const sex s = core::bernoulli(ci, p.male_birth_share) ? sex::male : sex::female;
                    auto l = draw_life_(ci.split("life"), country, s, birth, 0.0);
                    l.index = i;
                    out.push_back({.life = l, .union_index = static_cast<int>(j), .genetic = false});
                }
                continue;
            }
            const life& mother = a.s == sex::female ? a : u.partner;
            const life& father = a.s == sex::male ? a : u.partner;
            std::vector<double> local_births;
            std::vector<double>& births = a.s == sex::female ? anchor_births : local_births;
            const int n = std::min(family_size_(count_rng, mean), static_cast<int>(p.max_children));
            for (int i = 0; i < n; ++i)
            {
                auto ci = uj.split("child").split(static_cast<std::uint64_t>(i));
                for (int attempt = 0; attempt < 24; ++attempt)
                {
                    const double am = draw_mother_age_(cm.fertility, ci);
                    const double birth = forward_(mother.birth, am);
                    if (birth < u.start || birth > window_end || am >= mother.lifespan) continue;
                    const double fa = (birth - father.birth) / year_days_;
                    if (fa < p.min_father_age || fa > p.max_father_age || fa - p.conception_lead >= father.lifespan)
                    {
                        continue;
                    }
                    if (std::ranges::any_of(births, [&](double e)
                                            { return std::abs(birth - e) < p.birth_spacing * year_days_; }))
                    {
                        continue;
                    }
                    births.push_back(birth);
                    const sex s = core::bernoulli(ci, p.male_birth_share) ? sex::male : sex::female;
                    auto l = draw_life_(ci.split("life"), country, s, birth, 0.0);
                    l.index = i;
                    out.push_back({.life = l, .union_index = static_cast<int>(j), .genetic = true});
                    if (const auto t = twin_of_(ci, country, l))
                    {
                        out.push_back({.life = *t, .union_index = static_cast<int>(j), .genetic = true});
                    }
                    break;
                }
            }
        }
        return out;
    }

    // Names, from name-generator, by the culture of the anchor's country (or
    // the one asked for): the anchor is drawn, or follows the parents when
    // they have names and she or he has not; a parent is named after the
    // child (get_parent_name); a child after both parents (get_child_name),
    // with a made-up other parent where one is not a member; a partner is
    // drawn, and a wife may take the anchor's name (get_married_name,
    // `married_name_share`). A name the caller gave is used as it is, and the
    // others follow it. Members with no culture for the country are left
    // without names, but for the ones the caller named.
    void assign_names_(std::vector<person>& people, const std::vector<raw_member>& raw, const core::rng& rng,
                       const std::string& country, const std::optional<culture>& asked) const
    {
        culture c = culture::any;
        if (asked)
        {
            c = *asked;
        }
        else
        {
            std::string lower = country;
            std::ranges::transform(lower, lower.begin(),
                                   [](char ch) { return static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch); });
            c = ng::to_culture(core::from_utf8(lower));
        }
        if (c != culture::any)
        {
            // A culture whose lists are not loaded in the tier (name-generator's lite
            // has fewer than its full tier) leaves the whole family without names.
            try
            {
                assign_names_unchecked_(people, raw, rng, c);
                return;
            }
            catch (const std::invalid_argument&)
            {
                for (auto& p : people) p.given = p.patronymic = p.family = p.name = {};
            }
        }
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            if (!raw[i].fixed) continue;
            const auto& n = *raw[i].fixed;
            people[i].given = n.given;
            people[i].patronymic = n.patronymic;
            people[i].family = n.family;
            people[i].name = n.given;
            for (const auto* part : {&n.patronymic, &n.family})
            {
                if (!part->empty()) people[i].name += " " + *part;
            }
        }
    }

    // The words of a name the caller gave, split into surnames by the culture.
    [[nodiscard]] static std::vector<std::wstring> surnames_of_(const std::string& family, const naming_convention& cv)
    {
        std::vector<std::wstring> out;
        if (family.empty() || !cv.family_name) return out;
        std::vector<std::string> words;
        std::string word;
        for (const char ch : family)
        {
            if (ch == ' ')
            {
                if (!word.empty()) words.push_back(std::move(word));
                word.clear();
            }
            else
            {
                word += ch;
            }
        }
        if (!word.empty()) words.push_back(std::move(word));
        if (cv.surnames == 2 && words.size() == 2)
        {
            for (const auto& w : words) out.push_back(core::from_utf8(w));
        }
        else
        {
            out.push_back(core::from_utf8(family));
        }
        return out;
    }

    void assign_names_unchecked_(std::vector<person>& people, const std::vector<raw_member>& raw, const core::rng& rng,
                                 culture c) const
    {
        const auto gender_of = [](sex s) { return s == sex::male ? gender::m : gender::f; };
        const auto seed_of = [&](const std::string& key) { return rng.split(key).seed(); };
        const ng& names = *_from.name;
        const auto cv = names.convention(c);
        const auto full = [&](sex s, const std::string& key)
        { return names.get_full_name({.gender = gender_of(s), .culture = c}, seed_of(key)); };
        std::vector<std::optional<name>> nm(raw.size());
        std::unordered_map<std::int64_t, name> made_up;
        const auto slot = [&](std::int64_t token, sex s) -> const name&
        {
            if (token >= 0 && token < unlisted_) return *nm.at(static_cast<std::size_t>(token));
            auto it = made_up.find(token);
            if (it == made_up.end()) it = made_up.emplace(token, full(s, "other." + std::to_string(token))).first;
            return it->second;
        };
        // The first parent's slot is a father's. Where a woman holds it (a
        // same-sex union), a man of her family stands in, so that the culture's
        // convention (a patronymic, a masculine surname) applies as it does.
        const auto first_slot = [&](std::int64_t token) -> const name&
        {
            if (token >= 0 && token < unlisted_ && raw.at(static_cast<std::size_t>(token)).life.s == sex::female)
            {
                const auto key = -token - 2;
                auto it = made_up.find(key);
                if (it == made_up.end())
                {
                    it = made_up
                             .emplace(key, names.get_parent_name(*nm.at(static_cast<std::size_t>(token)), gender::m,
                                                                 seed_of("stand-in." + raw.at(static_cast<std::size_t>(token)).key)))
                             .first;
                }
                return it->second;
            }
            return slot(token, sex::male);
        };

        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            if (!raw[i].fixed) continue;
            const auto& n = *raw[i].fixed;
            if (n.given.empty()) throw std::invalid_argument("a name needs a given name");
            nm[i] = names.compose_name({.gender = gender_of(raw[i].life.s),
                                        .given = core::from_utf8(n.given),
                                        .patronymic = core::from_utf8(n.patronymic),
                                        .family = surnames_of_(n.family, cv)},
                                       c, seed_of(raw[i].key));
        }

        if (!nm[0])
        {
            // Parents with names give the anchor a name that follows theirs.
            const auto f = raw[0].parents[0];
            const auto m = raw[0].parents[1];
            const bool parents = f >= 0 && f < unlisted_ && m >= 0 && m < unlisted_;
            if (parents && (nm[static_cast<std::size_t>(f)] || nm[static_cast<std::size_t>(m)]))
            {
                const auto fi = static_cast<std::size_t>(f);
                const auto mi = static_cast<std::size_t>(m);
                if (!nm[fi]) nm[fi] = full(raw[fi].life.s, raw[fi].key);
                if (!nm[mi]) nm[mi] = full(raw[mi].life.s, raw[mi].key);
                nm[0] = names.get_child_name(*nm[fi], *nm[mi], {.gender = gender_of(raw[0].life.s)}, seed_of(raw[0].key));
            }
            else
            {
                nm[0] = full(raw[0].life.s, raw[0].key);
            }
        }
        for (std::size_t i = 1; i < raw.size(); ++i)
        {
            if (nm[i]) continue;
            const auto& m = raw[i];
            switch (m.role)
            {
            case relation::anchor: break;
            case relation::father:
            case relation::mother:
            case relation::paternal_grandfather:
            case relation::paternal_grandmother:
            case relation::maternal_grandfather:
            case relation::maternal_grandmother:
                nm[i] = names.get_parent_name(*nm.at(static_cast<std::size_t>(m.name_child)), gender_of(m.life.s),
                                              seed_of(m.key));
                break;
            case relation::partner:
            {
                nm[i] = full(m.life.s, m.key);
                auto married = rng.split(m.key + ".married");
                if (raw[0].life.s == sex::male && m.life.s == sex::female &&
                    core::bernoulli(married, _model.params.married_name_share))
                {
                    nm[i] = names.get_married_name(*nm[i], *nm[0]);
                }
                break;
            }
            case relation::sibling:
            case relation::half_sibling:
            case relation::child:
            case relation::grandchild:
            case relation::stepchild:
                nm[i] = names.get_child_name(first_slot(m.parents[0]), slot(m.parents[1], sex::female),
                                             {.gender = gender_of(m.life.s)}, seed_of(m.key));
                break;
            }
        }
        for (std::size_t i = 0; i < raw.size(); ++i)
        {
            const auto& n = *nm[i];
            people[i].given = core::to_utf8(n.given());
            people[i].patronymic = core::to_utf8(n.patronymic());
            people[i].family = core::to_utf8(n.family());
            people[i].name = core::to_utf8(static_cast<std::wstring>(n));
        }
    }
};

} // namespace dasmig

#endif // DASMIG_RELGEN_HPP
