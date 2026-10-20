#include "../dasmig/relgen.hpp"
#include <cstdint>
#include <iostream>

int main()
{
    // The singleton loads the lite tier (compiled in, or from resources/).
    auto& gen = dasmig::relg::instance();
    if (!gen.has_data())
    {
        std::cerr << "Could not load the family data.\n";
        return 1;
    }

    std::cout << "Random:\n";
    for (int i = 0; i < 3; ++i)
    {
        std::cout << "  " << gen.get_family(dasmig::family_request{}) << '\n';
    }

    // A Brazilian woman of 50, with her parents and grandparents, her partners,
    // her children and her grandchildren.
    dasmig::family_request request;
    request.anchor.country_code = "BR";
    request.anchor.bio_sex = dasmig::sex::female;
    request.anchor.age = 50;
    request.up = 2;
    request.down = 2;

    const auto family = gen.get_family(request, std::uint64_t{1});
    std::cout << "\nSeed 1:\n";
    for (const auto& m : family.members)
    {
        std::cout << "  " << dasmig::relation_name(m.role) << ": " << m.name << ", born "
                  << dasmig::family::iso_date(m.birth) << (m.alive ? ", alive at " : ", died at ")
                  << static_cast<int>(m.age) << '\n';
    }
    for (const auto& e : family.edges)
    {
        if (e.kind != dasmig::edge_kind::partner_of) continue;
        std::cout << "  union with " << family.members.at(e.to).name << " from " << *e.start;
        if (e.end) std::cout << " to " << *e.end << " (" << dasmig::union_end_name(e.ended) << ")";
        std::cout << '\n';
    }
    std::cout << "\nJSON:\n  " << family.to_json() << '\n';

    // The family of her first child, which agrees with hers: the same parents
    // and the same names.
    if (const auto children = family.children(); !children.empty())
    {
        dasmig::family_request around;
        around.up = 1;
        around.down = 1;
        const auto theirs = gen.expand(family, children.front().id, around, std::uint64_t{2});
        std::cout << "\nFamily of " << theirs.anchor().name << ":\n";
        for (const auto& m : theirs.members)
        {
            std::cout << "  " << dasmig::relation_name(m.role) << ": " << m.name << '\n';
        }
    }

    // A person you know: the parents and siblings take her family name.
    dasmig::family_request known;
    known.anchor.country_code = "US";
    known.anchor.bio_sex = dasmig::sex::female;
    known.anchor.age = 30;
    known.anchor.name = dasmig::person_name{.given = "Ada", .patronymic = "", .family = "Lovelace"};
    const auto ada = gen.get_family(known, std::uint64_t{3});
    std::cout << "\nAda's family:\n";
    for (const auto& m : ada.members)
    {
        std::cout << "  " << dasmig::relation_name(m.role) << ": " << m.name << '\n';
    }
    return 0;
}
