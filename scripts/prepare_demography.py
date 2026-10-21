#!/usr/bin/env python3
"""Download demographic data and produce the TSV files for relationship-generator.

Sources (all CC BY 3.0 IGO):
  1. REST Countries v3.1 (GitLab): ISO codes, M49 numbers, UN subregion, independence
  2. UN World Population Prospects 2024 (PPgp/wpp2024 GitHub repo, 2023 estimates):
     total fertility rate (tfr.txt) and the distribution of fertility by
     five-year age group of the mother (percentASFR5.txt)
  3. UN World Marriage Data 2019 (POP/DB/Marr/Rev2019): singulate mean age at
     marriage, ever married and divorced or separated shares at 45 to 49
  4. UN Database on Household Size and Composition 2026 (UN DESA/POP/2026/DC/NO.15):
     household size, one-person, single-parent and multi-generation households

Produces per tier (lite: independent states, full: all countries and territories):
  resources/{tier}/regions.tsv      the UN subregion of each country (for the defaults)
  resources/{tier}/fertility.tsv    tfr (now and every five years since 1950), mean age at
                                    childbearing, fertility by mother's age
  resources/{tier}/unions.tsv       age at first marriage, never married, divorced or separated
  resources/{tier}/households.tsv   household size and types
  resources/{tier}/defaults.tsv     median of every column per UN subregion, and the world

A country absent from a table is not an error: the library falls back to the
`defaults.tsv` row of its subregion (and the result says so). Empty cells mean
"no value for this country" and fall back the same way.

Usage:
    python prepare_demography.py              # both tiers
    python prepare_demography.py --tier lite  # one tier
    python prepare_demography.py --report     # also print the sanity report
"""

import argparse
import json
import os
import statistics
import sys
import urllib.request

import openpyxl

_RESTCOUNTRIES_URL = (
    "https://gitlab.com/restcountries/restcountries"
    "/-/raw/master/src/main/resources/countriesV3.1.json"
)
_WPP_RAW = "https://raw.githubusercontent.com/PPgp/wpp2024/main/data-raw"
_WPP_DATA = "https://raw.githubusercontent.com/PPgp/wpp2024/main/data"
_WMD_URL = (
    "https://www.un.org/development/desa/pd/sites/www.un.org.development.desa.pd"
    "/files/undesa_pd_2019_wmd_marital_status.xlsx"
)
_HH_URL = "https://population.un.org/Household/assets/UNDESA_PD_2026_hh-size-composition.xlsx"

_CACHE_DIR = os.path.join(os.path.dirname(__file__), ".cache")
_OUT_DIR = os.path.join(os.path.dirname(__file__), "..", "resources")

_YEAR = "2023"  # latest historical estimate year in the WPP data
_AGE_GROUPS = ["10-14", "15-19", "20-24", "25-29", "30-34", "35-39", "40-44", "45-49"]

_TFR_YEARS = list(range(1950, 2021, 5))  # earlier periods, to size the family of an older person
_FERTILITY_COLUMNS = (
    ["cca2", "tfr", "mac"] + ["pasfr_" + g.replace("-", "_") for g in _AGE_GROUPS]
    + [f"tfr_{y}" for y in _TFR_YEARS]
)
_UNION_COLUMNS = [
    "cca2", "smam_m", "smam_f", "never_m_45_49", "never_f_45_49",
    "divorced_m_45_49", "divorced_f_45_49", "year",
]
_HOUSEHOLD_COLUMNS = [
    "cca2", "size", "one_person", "single_parent", "multi_generation", "year", "source",
]


# ---------------------------------------------------------------------------
# Download helpers
# ---------------------------------------------------------------------------


def _fetch_cached(url: str, cache_key: str) -> str:
    """Download a URL once; return the path of the cached file."""
    path = os.path.join(_CACHE_DIR, cache_key)
    if not os.path.exists(path):
        print(f"    GET {url[:110]}{'...' if len(url) > 110 else ''}")
        req = urllib.request.Request(url, headers={"User-Agent": "relationship-generator/1.0"})
        raw = urllib.request.urlopen(req, timeout=300).read()  # noqa: S310
        os.makedirs(_CACHE_DIR, exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(raw)
    return path


def _fetch_tsv(url: str, cache_key: str) -> list[list[str]]:
    with open(_fetch_cached(url, cache_key), encoding="utf-8") as fh:
        return [line.rstrip("\n").split("\t") for line in fh if line.strip()]


def _number(value) -> float | None:
    """A cell as a float, or None when it is empty or not a number ('..', '-')."""
    if value is None:
        return None
    try:
        return float(str(value).strip())
    except ValueError:
        return None


# ---------------------------------------------------------------------------
# Countries
# ---------------------------------------------------------------------------


def _countries() -> dict[str, dict]:
    """REST Countries keyed by alpha-2, with the M49 number as an int."""
    with open(_fetch_cached(_RESTCOUNTRIES_URL, "restcountries_v3.1.json"), encoding="utf-8") as fh:
        data = json.load(fh)
    result = {}
    for c in data:
        cca2 = c.get("cca2", "")
        if not cca2 or not c.get("ccn3"):
            continue
        result[cca2] = {
            "m49": int(c["ccn3"]),
            "name": c.get("name", {}).get("common", ""),
            "subregion": c.get("subregion", "") or c.get("region", ""),
            "independent": bool(c.get("independent", False)),
        }
    return result


# ---------------------------------------------------------------------------
# Fertility (WPP 2024)
# ---------------------------------------------------------------------------


def _fertility(by_m49: dict[int, str]) -> dict[str, dict[str, float]]:
    tfr_rows = _fetch_tsv(f"{_WPP_RAW}/tfr.txt", "wpp_tfr.txt")
    asfr_rows = _fetch_tsv(f"{_WPP_RAW}/percentASFR5.txt", "wpp_percentASFR5.txt")
    ti = tfr_rows[0].index(_YEAR)
    ai = asfr_rows[0].index(_YEAR)
    hist = {y: tfr_rows[0].index(str(y)) for y in _TFR_YEARS}

    out: dict[str, dict[str, float]] = {}
    for row in tfr_rows[1:]:
        cca2 = by_m49.get(int(row[0]))
        value = _number(row[ti]) if len(row) > ti else None
        if cca2 and value is not None:
            out[cca2] = {"tfr": value}
            for y, i in hist.items():
                h = _number(row[i]) if len(row) > i else None
                if h is not None:
                    out[cca2][f"tfr_{y}"] = h

    shares: dict[str, dict[str, float]] = {}
    for row in asfr_rows[1:]:
        cca2 = by_m49.get(int(row[0]))
        value = _number(row[ai]) if len(row) > ai else None
        if cca2 in out and row[2] in _AGE_GROUPS and value is not None:
            shares.setdefault(cca2, {})[row[2]] = value

    for cca2 in list(out):
        s = shares.get(cca2, {})
        total = sum(s.values())
        if len(s) != len(_AGE_GROUPS) or total <= 0:
            del out[cca2]
            continue
        mac = sum((int(g[:2]) + 2.5) * v for g, v in s.items()) / total
        out[cca2]["mac"] = mac
        for g in _AGE_GROUPS:  # renormalised to exactly 100
            out[cca2]["pasfr_" + g.replace("-", "_")] = 100.0 * s[g] / total
    return out


# ---------------------------------------------------------------------------
# Unions (World Marriage Data 2019)
# ---------------------------------------------------------------------------


def _latest(best: dict, key, year: int, value: float) -> None:
    if key not in best or year > best[key][0]:
        best[key] = (year, value)


def _unions(by_m49: dict[int, str]) -> dict[str, dict[str, float]]:
    wb = openpyxl.load_workbook(_fetch_cached(_WMD_URL, "wmd_2019.xlsx"), read_only=True)
    sex = {"Men": "m", "Women": "f"}

    smam: dict = {}
    for r in wb["SMAM"].iter_rows(min_row=4, values_only=True):
        v = _number(r[5])
        if r[1] in by_m49 and r[4] in sex and v is not None:
            _latest(smam, (by_m49[r[1]], sex[r[4]]), int(r[3]), v)

    ever: dict = {}
    for r in wb["EVER_MARRIED"].iter_rows(min_row=4, values_only=True):
        v = _number(r[8])
        if r[1] in by_m49 and r[4] in sex and r[5] == "[45-49]" and v is not None:
            _latest(ever, (by_m49[r[1]], sex[r[4]]), int(r[3]), v)

    # Divorced and separated are separate categories in most series and joined in
    # some; add the two, or take the joined one.
    parts: dict = {}
    for r in wb["MARITAL_STATUS_BY_AGE"].iter_rows(min_row=4, values_only=True):
        v = _number(r[11])
        if (r[1] in by_m49 and r[4] in sex and r[8] == "[45-49]" and v is not None
                and r[5] in ("Divorced", "Separated", "Divorced or Separated")):
            key = (by_m49[r[1]], sex[r[4]], int(r[3]), r[13])
            parts.setdefault(key, {})[r[5]] = v
    divorced: dict = {}
    for (cca2, s, year, _source), p in parts.items():
        if "Divorced or Separated" in p:
            total = p["Divorced or Separated"]
        elif "Divorced" in p:  # a series with only "Separated" would understate
            total = p["Divorced"] + p.get("Separated", 0.0)
        else:
            continue
        _latest(divorced, (cca2, s), year, total)

    out: dict[str, dict[str, float]] = {}
    for (cca2, s), (year, v) in smam.items():
        out.setdefault(cca2, {})[f"smam_{s}"] = v
        if s == "f":
            out[cca2]["year"] = float(year)
    for (cca2, s), (_year, v) in ever.items():
        out.setdefault(cca2, {})[f"never_{s}_45_49"] = max(0.0, 100.0 - v)
    for (cca2, s), (_year, v) in divorced.items():
        out.setdefault(cca2, {})[f"divorced_{s}_45_49"] = v
    return out


# ---------------------------------------------------------------------------
# Households (UN Household Size and Composition 2026)
# ---------------------------------------------------------------------------


def _households(known: set[str]) -> dict[str, dict]:
    wb = openpyxl.load_workbook(_fetch_cached(_HH_URL, "hh_2026.xlsx"), read_only=True)
    rows = list(wb["HH size and composition 2026"].iter_rows(min_row=6, values_only=True))
    col = {"size": 10, "one": 36, "single": 39, "multi": 46}

    best: dict[str, tuple] = {}
    for r in rows:
        cca2, year = r[3], _number(r[8])
        if cca2 not in known or year is None:
            continue
        values = {k: _number(r[i]) for k, i in col.items()}
        if values["size"] is None:
            continue
        # The latest survey with the most indicators filled in.
        rank = (sum(v is not None for v in values.values()), year)
        if cca2 not in best or rank > best[cca2][0]:
            best[cca2] = (rank, year, values, str(r[6]))

    out = {}
    for cca2, (_rank, year, v, source) in best.items():
        out[cca2] = {
            "size": v["size"], "one_person": v["one"], "single_parent": v["single"],
            "multi_generation": v["multi"], "year": year, "source": source,
        }
    return out


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------


def _fmt(value) -> str:
    if value is None:
        return ""
    if isinstance(value, str):
        return value
    if float(value).is_integer():
        return str(int(value))
    return f"{value:.4f}".rstrip("0").rstrip(".")


def _write(path: str, columns: list[str], rows: list[dict]) -> None:
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\t".join(columns) + "\n")
        for row in rows:
            fh.write("\t".join(_fmt(row.get(c)) for c in columns) + "\n")
    print(f"  Wrote {len(rows)} rows to {os.path.normpath(path)}")


# Subregions where no country has a survey figure, so the median is missing and
# a country would fall back to the world's (a third of households), which is far
# too high for them. Curated (rule 6): the multi-generation share of households
# is a few per cent in Western Europe (Eurostat, national censuses) and in
# Australia and New Zealand (the 2021 censuses).
_CURATED_DEFAULTS = {
    ("Western Europe", "multi_generation"): 5.0,
    ("Australia and New Zealand", "multi_generation"): 6.0,
}


def _defaults(tables: dict[str, dict[str, dict]], countries: dict[str, dict]) -> list[dict]:
    """Median of every numeric column per subregion; `World` is the median of all."""
    columns = [c for c in _FERTILITY_COLUMNS[1:] + _UNION_COLUMNS[1:] + _HOUSEHOLD_COLUMNS[1:]
               if c not in ("year", "source")]
    merged: dict[str, dict] = {}
    for table in tables.values():
        for cca2, row in table.items():
            merged.setdefault(cca2, {}).update(row)

    groups: dict[str, list[str]] = {"World": list(merged)}
    for cca2 in merged:
        groups.setdefault(countries[cca2]["subregion"], []).append(cca2)

    rows = []
    for name in ["World"] + sorted(k for k in groups if k != "World"):
        row = {"subregion": name, "countries": len(groups[name])}
        for c in columns:
            values = [merged[k][c] for k in groups[name] if merged[k].get(c) is not None]
            if values:
                row[c] = statistics.median(values)
            elif (name, c) in _CURATED_DEFAULTS:
                row[c] = _CURATED_DEFAULTS[(name, c)]
        rows.append(row)
    return rows


def _report(tables: dict[str, dict[str, dict]], countries: dict[str, dict]) -> None:
    print("\n=== Sanity report ===")
    for name, table in tables.items():
        print(f"  {name}: {len(table)} countries")
    limits = {"mac": (22, 34), "smam_m": (20, 36), "smam_f": (17, 34), "tfr": (0.7, 8.0),
              "never_f_45_49": (0, 40), "divorced_f_45_49": (0, 25), "size": (1.5, 10)}
    for name, table in tables.items():
        for cca2, row in sorted(table.items()):
            for c, (lo, hi) in limits.items():
                v = row.get(c)
                if v is not None and not lo <= v <= hi:
                    print(f"  OUTLIER {name} {cca2} {countries[cca2]['name']}: {c} = {v:.2f}")
    merged = {k for t in tables.values() for k in t}
    lacking = [c for c, v in countries.items() if v["independent"] and c not in merged]
    print(f"  independent states with no row in any table: {len(lacking)}: {' '.join(sorted(lacking))}")
    for name, table in tables.items():
        lacking = [c for c, v in countries.items() if v["independent"] and c not in table]
        print(f"  {name}: independent states using the subregion default: {len(lacking)}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Prepare demography TSV files for relationship-generator.")
    parser.add_argument("--tier", default="both", choices=["full", "lite", "both"])
    parser.add_argument("--report", action="store_true", help="print the sanity report")
    args = parser.parse_args()

    print("=== 1. Countries (REST Countries) ===")
    countries = _countries()
    by_m49 = {v["m49"]: k for k, v in countries.items()}
    print(f"  {len(countries)} countries")

    print("=== 2. Fertility (WPP 2024) ===")
    fertility = _fertility(by_m49)
    print(f"  {len(fertility)} countries")

    print("=== 3. Unions (World Marriage Data 2019) ===")
    unions = _unions(by_m49)
    print(f"  {len(unions)} countries")

    print("=== 4. Households (UN 2026) ===")
    households = _households(set(countries))
    print(f"  {len(households)} countries")

    tables = {"fertility": fertility, "unions": unions, "households": households}
    if args.report:
        _report(tables, countries)

    tiers = ["lite", "full"] if args.tier == "both" else [args.tier]
    for tier in tiers:
        print(f"=== Writing {tier} ===")
        keep = {k for k, v in countries.items() if tier == "full" or v["independent"]}
        out = os.path.join(_OUT_DIR, tier)
        order = sorted(keep, key=lambda k: countries[k]["name"])
        _write(os.path.join(out, "regions.tsv"), ["cca2", "subregion"],
               [{"cca2": k, "subregion": countries[k]["subregion"]} for k in order])
        for fname, cols, table in (
            ("fertility.tsv", _FERTILITY_COLUMNS, fertility),
            ("unions.tsv", _UNION_COLUMNS, unions),
            ("households.tsv", _HOUSEHOLD_COLUMNS, households),
        ):
            _write(os.path.join(out, fname), cols, [{"cca2": k, **table[k]} for k in order if k in table])
        # The defaults come from every country, so the two tiers fall back alike.
        defaults = _defaults(tables, countries)
        _write(os.path.join(out, "defaults.tsv"), ["subregion", "countries"] + [
            c for c in _FERTILITY_COLUMNS[1:] + _UNION_COLUMNS[1:] + _HOUSEHOLD_COLUMNS[1:]
            if c not in ("year", "source")], defaults)


if __name__ == "__main__":
    sys.exit(main())
