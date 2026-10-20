# Data and Method

Where the data in `resources/` comes from, how it is built, and what the model leaves out (rule 6 of the dasmig standard).

The tables below drive the whole family: the anchor, parents, grandparents, siblings and half-siblings, partners, stepchildren, children and grandchildren. `households.tsv` is shipped, loaded and layered but the model does not read it yet: a household (who lives together) is not part of a family in this version, and its single-parent and multi-generation shares are kept for a later one.

## Tiers

| Tier | Countries in the tables | Notes |
| --- | --- | --- |
| `lite` | 193 independent states with fertility (192 in `unions`, 170 in `households`), 194 in `regions` | Vatican City has no data in any source |
| `full` | 235 countries and territories with fertility (229 in `unions`, 188 in `households`), 249 in `regions` | The territories with UN estimates |

A country missing from a table, or with an empty cell, falls back to the `defaults.tsv` row of its UN subregion, and the result says so. The two tiers share one `defaults.tsv` built from all countries, so a fallback is the same in both.

## Sources

All three sources are United Nations Population Division data made available under CC BY 3.0 IGO.

| File | Columns | Source |
| --- | --- | --- |
| `regions.tsv` | `subregion` | The UN subregion of each country: [REST Countries](https://restcountries.com/) v3.1. It says which row of `defaults.tsv` a country falls back to |
| `fertility.tsv` | `tfr` | Total fertility rate, 2023: [UN World Population Prospects 2024](https://population.un.org/wpp/) (via the `PPgp/wpp2024` R package data, `tfr.txt`) |
| | `pasfr_10_14` … `pasfr_45_49` | Percentage of fertility by five-year age group of the mother, 2023: WPP 2024 (`percentASFR5.txt`), renormalised to sum to exactly 100 |
| | `tfr_1950` … `tfr_2020` | The same rate for each fifth year since 1950 (`tfr.txt`), to size the family of an older person |
| | `mac` | Mean age at childbearing: computed from the `pasfr` columns, using the middle of each age group |
| `unions.tsv` | `smam_m`, `smam_f` | Singulate mean age at marriage by sex, latest series: [UN World Marriage Data 2019](https://www.un.org/development/desa/pd/content/world-marriage-data) (`SMAM`) |
| | `never_m_45_49`, `never_f_45_49` | 100 minus the percentage ever married at age 45 to 49, latest series (`EVER_MARRIED`) |
| | `divorced_m_45_49`, `divorced_f_45_49` | Percentage of all men or women aged 45 to 49 who are divorced or separated, latest series that reports divorce (`MARITAL_STATUS_BY_AGE`): `Divorced` plus `Separated`, or the joined category where a source has only that |
| | `year` | Last year of the women's SMAM series |
| `households.tsv` | `size` | Average household size (number of members) |
| | `one_person`, `single_parent`, `multi_generation` | Percentage of households that are one-person, single parent with children, and multi-generation |
| | `year`, `source` | Reference year and survey family (DHS, MICS, IPUMS, DYB, LFS or other) of the row used |
| | | [UN Database on Household Size and Composition 2026](https://population.un.org/Household/index.html) (UN DESA/POP/2026/DC/NO.15): the survey with the most of these indicators filled in, then the latest |
| `defaults.tsv` | every numeric column above | Median over the countries of a UN subregion (the REST Countries `subregion`), with `World` as the median over all countries; `countries` is how many countries it covers. Derived, not curated |
| `parameters.tsv` | `key`, `value`, `note` | The model's constants, all curated (see the Generation Model); the file is layered like the others and an unknown key is an error |
| Country metadata | | REST Countries v3.1: ISO codes, M49 numbers, subregion and independence |

`scripts/prepare_demography.py` downloads the sources (cached in `scripts/.cache`, which is not committed) and writes both tiers:

```bash
python scripts/prepare_demography.py              # both tiers
python scripts/prepare_demography.py --tier lite  # one tier
python scripts/prepare_demography.py --report     # also print the sanity report
```

The report lists the values outside a plausible range (for example `tfr` outside 0.7 to 8, or a mean age at childbearing outside 22 to 34) and the independent states that fall back to a default. After changing the data, run `make embed` and update the known-answer tests (the data hash and results change).

## Generation Model

`get_family(request, seed)` draws a family around an anchor. Each part comes from a stream split from the call seed by name, so asking for grandparents or for no siblings does not change the rest.

1. **Anchor.** What the request fixes (country, sex, birth date, age) is kept; the rest comes from `birth-generator` for the country, with its age pyramid and population weights, and the date ages are counted at (1 July 2023 unless the request says otherwise).
2. **Mother.** Her age at the anchor's birth: one of the eight five-year groups from 10 to 49, by `pasfr` (the share of the country's births each group has), uniform inside the group, and inside the fertile years (12 to 49.9).
3. **Father.** The mother's age plus a difference drawn around the gap between the country's men's and women's mean age at first marriage (`smam_m - smam_f`, between 0 and 8 years), with a standard deviation of 3.5 years and a floor of 15.
4. **Siblings.** The number depends on the mean family size `m`, the total fertility rate of the anchor's birth year (interpolated between the five-year figures). Seen from a child, a family is size-biased: a family of *n* children is *n* times as likely to be the anchor's, so the siblings are a size-biased family size minus one. How much families differ in size grows with fertility, as the variance over the mean `d = 0.2 + 0.27 m` (not below 0.3): below 0.9, a binomial family size; near 1, Poisson; above 1.05, a negative binomial (a Poisson whose mean is gamma-distributed). Each sibling is then placed by the mother's age at the birth and left out if no age fits in twelve tries: before she was 12 or after 49.9, after she died, after the father died (he must be alive nine months before a birth), after the reference date, or less than nine months from another of her births. A birth is male with probability 0.512.
5. **Who is alive.** Each person's age at death is drawn given that they were alive when they last had to be (the mother at the birth, the father nine months before it, a child at birth). Childhood: the chance of dying before 15 is `0.012 exp((80 - e0) / 11)`, where `e0` is the country's life expectancy at birth for the sex (from `birth-generator`), and a child who dies does so early. Adulthood: from 15, a Gompertz curve (`exp(-c (exp(beta x) - 1))`, `beta = 0.09`) whose mean age at death is `26 + 0.7 e0`, a line through the Coale-Demeny west model life tables (61 at `e0` 50, 82 at `e0` 80). Fitting the curve to `e0` itself puts adult deaths far too early where child deaths are common: a 25-year-old Nigerian had a living father 37% of the time, against 62% now. The dead have a date of death before the reference date.
6. **Grandparents.** The same draw for the father's parents and the mother's parents, without their siblings.
7. **Half-siblings.** A parent has children with someone else with the divorced share of the country's men or women among the ever married, times 0.6; then one child plus a Poisson number with mean 0.5, each placed like a sibling. The other parent is not a member (a name is made up for naming the children). Half-siblings through one parent born within four years of one another have the same other parent, so they are full siblings of one another; a later one has another.
8. **Partners.** Whether the anchor ever has one comes from the share of men or women still never married at 45 to 49. The first union begins around the country's mean age at first marriage, earlier by half the years it is above 21 (at most 3: cohabitation comes before marriage where marriage is late), with a standard deviation of 4.5 and a floor of 16. The partner is of the other sex, or the same with probability 0.03 (curated), and older or younger by the gap between men's and women's mean marriage ages. A union ends in a separation with the divorced share among the ever married, after half a year plus an exponential time with mean 9 years, unless the partner dies or the reference date comes first; a separation or a death can be followed, with probability 0.45, by another union half a year plus an exponential time with mean 3 years later, up to three. Unions are ongoing only while the partner is alive.
9. **Children.** In a union of a woman and a man, the woman's children are born inside it: her age at each birth from the fertility by age, left out if no age fits (the father dead nine months before, out of range, too close to another birth of hers, or outside the union). The mean number is the total fertility rate of the year the union began plus 0.2, a later union's times 0.6, drawn like a family size (a binomial, Poisson or negative binomial by the spread of family sizes, as for siblings, but not size-biased). A same-sex union has adopted children instead, 15% of that mean, born from two years before it began to its end; they are linked by non-genetic `parent_of` edges.
10. **Grandchildren.** Through each child, the same placement of births with a mean size from the fertility rate twenty-eight years after the child's birth; the other parent is not a member.
11. **Stepchildren.** A partner has children from before the union with the partner's divorced share among the ever married, times `stepchild_factor` (0.6); one child plus a Poisson number with mean 0.5, each born at least nine months before the union began. They are linked to the partner only (`parent_of`), and to the anchor's children by the partner as half-siblings. They are drawn for the anchor's partners only when partners and children are asked for.
12. **Twins.** Each birth is a twin birth with probability `twin_share` (0.012): another child of the same parents on the same day, with a sex and life of its own. The anchor has a twin with twice that probability (a person is twice as likely to be one of a pair as a delivery is to be a twin delivery). A member born the same day as a full sibling has `twin` set.
13. **Names.** From `name-generator`, by the culture of the anchor's country (or `name_culture`): the anchor is drawn or given, a parent is named after the child (`get_parent_name`), a sibling, child, stepchild or grandchild after both parents (`get_child_name`, with a made-up other parent where one is not a member), a partner is drawn and a wife may take the anchor's name (probability 0.8; `get_married_name`). Where a given anchor has parents with names and the anchor has none, the anchor is named after them. In a same-sex union of two women the first parent holds the father's place, so a man of her family stands in for the convention (a masculine surname, a patronymic). A country whose culture has no lists in the loaded tier of `name-generator` gets no names, but for the ones given.

**Known relatives and expansion.** `anchor_spec` can hold the anchor's name, father, mother, siblings and unions as plain values. A relative that is given is used as it is (a parent's date of birth fixes the anchor's gap to the parent; the other parent and the siblings are drawn around it), and a list that is given (siblings, unions) is the whole list, so no one else is drawn for it. `relg::expand(family, member, request, seed)` builds this from a family: the member becomes the anchor and keeps the parents, grandparents, siblings, partners, stepchildren and children the family lists, so the two agree.

Every family keeps these rules, which the property tests check on tens of thousands of families: a mother is 12 to 50 at each birth and a father at least 15; no child is born after its mother's death or later than nine months after its father's; a mother's births are at least nine months apart; nobody is born after the reference date or dies after it; siblings are numbered oldest first.

The constants are in `parameters.tsv`. The ages, `birth_spacing`, `conception_lead`, the mortality lines and the dispersion line are curated (rule 6), and the file's `note` column says what each is.

## Known Limitations

- **Cohort effects are partly modelled.** Family size follows the fertility rate of the anchor's birth year, but the age pattern of fertility (`pasfr`) and life expectancy are today's: a 70-year-old's mother is given today's age at childbearing, and the parents' survival comes from today's life expectancy, which makes parents of older people in countries that were poor then live too long. Life expectancy by year is in WPP (`e0B.txt`) and could replace the second.
- **Grandparents live in the anchor's country**, and a family never moves.
- **Dead siblings** include children who died before 15, so a sibling count is the number of children the mother had, alive or not.
- **Unions are not marriages.** First-marriage figures set when unions begin, with an allowance for cohabitation, so Germans aged 30 are partnered 44% of the time against about 60% in surveys, and a union of any kind counts as one for children. Divorced and separated shares include ended consensual unions where a source counts them.
- **Calibration.** The mean number of children is the total fertility rate of the year the union began plus 0.2, because the rate of one year understates what the women then young have in all; the offset was set against the children of people aged 50 (Americans 1.9, Brazilians 2.3, Germans 1.2, Nigeriens 7.3 in the model). Low-fertility countries still come out low and childlessness high (a third of Germans aged 50).
- **Not modelled:** stepparents (a parent who remarries), same-sex couples' biological children, births outside unions, polygamy, siblings of the partner, households (who lives together; `households.tsv` is unused), and moves between countries. A half-sibling's other parent, a stepchild's other parent and a grandchild's other parent are not members.
- **Names:** the family has the names of one culture, the country's unless the request says another. A name the caller gives is split into surnames by words: two words are two surnames in a culture with two (Spain, Portugal), and a family name of any other shape is kept whole.
- **Expansion keeps what a family lists.** A member's partners and children are kept only if the family listed them (a family made with `partners = false` or `down = 0` has none to keep, and they are drawn again). A partner whom a family left out is not recreated, but the children of the union are kept. A union's dates are known to the year, so the start and end of a kept union are the middle of that year.
- **Twins** are of the same sex or not at random, with no allowance for identical twins being more common in some groups.

- The marriage figures are a snapshot of the latest census or survey for each country, which can be decades old (`year` says which). SMAM is a stock-based estimate of the mean age at first marriage, not a count of weddings.
- "Divorced or separated" includes people in a consensual union that ended wherever a source counts those as separated, so Caribbean and Latin American figures read high (Cuba, the Dominican Republic and El Salvador are above 25% of women aged 45 to 49). It is a share of the population, not a divorce rate.
- When a country has several surveys for the same year, the last row read wins.
- The household survey families differ in what they can measure (tabulations from the Demographic Yearbook have no multi-generation figure), so some cells are empty and fall back to the subregion default.
- Subregions are the REST Countries names, which follow the UN geoscheme loosely (for example "Southeast Europe"); a country whose subregion is missing falls back to `World`.
- Same-sex unions, adoption, stepchildren and twins have no open demographic source here per country; their rates are curated and recorded as such in `parameters.tsv`.
