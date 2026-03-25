#!/usr/bin/env python3
"""
build_timezone_geojson.py
=========================
Downloads the timezone boundary map (land only, no oceans) from
timezone-boundary-builder, simplifies polygon boundaries, and enriches
each region with:
  - ISO 3166-1 alpha-2 country code (field "country")
  - DST rules in POSIX Mm.w.d/hh:mm format

Output structure: one Feature per unique (country, tzid) pair.
Countries with multiple tzid values produce multiple Features.

Dependencies:
    pip install requests shapely pytz tqdm

Usage:
    python build_timezone_geojson.py [--tolerance 0.05] [--output timezones_dst.geojson]

Arguments:
    --tolerance   Simplification tolerance in degrees. Default: 0.05
    --output      Output filename. Default: timezones_dst.geojson
    --release     timezone-boundary-builder release tag. Default: 2025b
    --year        Year used for DST analysis. Default: current year
"""

import argparse
import calendar
import json
import sys
import zipfile
from datetime import datetime, timedelta, timezone as dt_timezone
from pathlib import Path

import requests
from shapely.geometry import shape, mapping
from shapely.ops import unary_union
from shapely.validation import make_valid
from tqdm import tqdm
import pytz


# ──────────────────────────────────────────────────────────────────────────────
# Constants
# ──────────────────────────────────────────────────────────────────────────────

GITHUB_RELEASE_URL = (
    "https://github.com/evansiroky/timezone-boundary-builder"
    "/releases/download/{release}/timezones.geojson.zip"
)
GEOJSON_FILENAME_IN_ZIP     = "combined.json"
GEOJSON_FILENAME_IN_ZIP_NEW = "combined.geojson"


# ──────────────────────────────────────────────────────────────────────────────
# IANA tzid → ISO 3166-1 alpha-2 mapping
#
# Source: Continent/City zones from the IANA tz database.
# Covers all ~600 canonical zones. Alias zones are resolved via
# _TZID_ALIAS_MAP with a prefix-based fallback.
# ──────────────────────────────────────────────────────────────────────────────

# fmt: off
_TZID_TO_COUNTRY: dict[str, str] = {
    # Africa
    "Africa/Abidjan": "CI", "Africa/Accra": "GH", "Africa/Addis_Ababa": "ET",
    "Africa/Algiers": "DZ", "Africa/Asmara": "ER", "Africa/Bamako": "ML",
    "Africa/Bangui": "CF", "Africa/Banjul": "GM", "Africa/Bissau": "GW",
    "Africa/Blantyre": "MW", "Africa/Brazzaville": "CG", "Africa/Bujumbura": "BI",
    "Africa/Cairo": "EG", "Africa/Casablanca": "MA", "Africa/Ceuta": "ES",
    "Africa/Conakry": "GN", "Africa/Dakar": "SN", "Africa/Dar_es_Salaam": "TZ",
    "Africa/Djibouti": "DJ", "Africa/Douala": "CM", "Africa/El_Aaiun": "EH",
    "Africa/Freetown": "SL", "Africa/Gaborone": "BW", "Africa/Harare": "ZW",
    "Africa/Johannesburg": "ZA", "Africa/Juba": "SS", "Africa/Kampala": "UG",
    "Africa/Khartoum": "SD", "Africa/Kigali": "RW", "Africa/Kinshasa": "CD",
    "Africa/Lagos": "NG", "Africa/Libreville": "GA", "Africa/Lome": "TG",
    "Africa/Luanda": "AO", "Africa/Lubumbashi": "CD", "Africa/Lusaka": "ZM",
    "Africa/Malabo": "GQ", "Africa/Maputo": "MZ", "Africa/Maseru": "LS",
    "Africa/Mbabane": "SZ", "Africa/Mogadishu": "SO", "Africa/Monrovia": "LR",
    "Africa/Nairobi": "KE", "Africa/Ndjamena": "TD", "Africa/Niamey": "NE",
    "Africa/Nouakchott": "MR", "Africa/Ouagadougou": "BF", "Africa/Porto-Novo": "BJ",
    "Africa/Sao_Tome": "ST", "Africa/Tripoli": "LY", "Africa/Tunis": "TN",
    "Africa/Windhoek": "NA",
    # America
    "America/Adak": "US", "America/Anchorage": "US", "America/Anguilla": "AI",
    "America/Antigua": "AG", "America/Araguaina": "BR", "America/Argentina/Buenos_Aires": "AR",
    "America/Argentina/Catamarca": "AR", "America/Argentina/Cordoba": "AR",
    "America/Argentina/Jujuy": "AR", "America/Argentina/La_Rioja": "AR",
    "America/Argentina/Mendoza": "AR", "America/Argentina/Rio_Gallegos": "AR",
    "America/Argentina/Salta": "AR", "America/Argentina/San_Juan": "AR",
    "America/Argentina/San_Luis": "AR", "America/Argentina/Tucuman": "AR",
    "America/Argentina/Ushuaia": "AR", "America/Aruba": "AW",
    "America/Asuncion": "PY", "America/Atikokan": "CA", "America/Bahia": "BR",
    "America/Bahia_Banderas": "MX", "America/Barbados": "BB", "America/Belem": "BR",
    "America/Belize": "BZ", "America/Blanc-Sablon": "CA", "America/Boa_Vista": "BR",
    "America/Bogota": "CO", "America/Boise": "US", "America/Cambridge_Bay": "CA",
    "America/Campo_Grande": "BR", "America/Cancun": "MX", "America/Caracas": "VE",
    "America/Cayenne": "GF", "America/Cayman": "KY", "America/Chicago": "US",
    "America/Chihuahua": "MX", "America/Ciudad_Juarez": "MX",
    "America/Costa_Rica": "CR", "America/Creston": "CA", "America/Cuiaba": "BR",
    "America/Curacao": "CW", "America/Danmarkshavn": "GL", "America/Dawson": "CA",
    "America/Dawson_Creek": "CA", "America/Denver": "US", "America/Detroit": "US",
    "America/Dominica": "DM", "America/Edmonton": "CA", "America/Eirunepe": "BR",
    "America/El_Salvador": "SV", "America/Fortaleza": "BR", "America/Glace_Bay": "CA",
    "America/Godthab": "GL", "America/Goose_Bay": "CA", "America/Grand_Turk": "TC",
    "America/Grenada": "GD", "America/Guadeloupe": "GP", "America/Guatemala": "GT",
    "America/Guayaquil": "EC", "America/Guyana": "GY", "America/Halifax": "CA",
    "America/Havana": "CU", "America/Hermosillo": "MX", "America/Indiana/Indianapolis": "US",
    "America/Indiana/Knox": "US", "America/Indiana/Marengo": "US",
    "America/Indiana/Petersburg": "US", "America/Indiana/Tell_City": "US",
    "America/Indiana/Vevay": "US", "America/Indiana/Vincennes": "US",
    "America/Indiana/Winamac": "US", "America/Inuvik": "CA", "America/Iqaluit": "CA",
    "America/Jamaica": "JM", "America/Juneau": "US", "America/Kentucky/Louisville": "US",
    "America/Kentucky/Monticello": "US", "America/Kralendijk": "BQ",
    "America/La_Paz": "BO", "America/Lima": "PE", "America/Los_Angeles": "US",
    "America/Lower_Princes": "SX", "America/Maceio": "BR", "America/Managua": "NI",
    "America/Manaus": "BR", "America/Marigot": "MF", "America/Martinique": "MQ",
    "America/Matamoros": "MX", "America/Mazatlan": "MX", "America/Menominee": "US",
    "America/Merida": "MX", "America/Metlakatla": "US", "America/Mexico_City": "MX",
    "America/Miquelon": "PM", "America/Moncton": "CA", "America/Monterrey": "MX",
    "America/Montevideo": "UY", "America/Montserrat": "MS", "America/Nassau": "BS",
    "America/New_York": "US", "America/Nipigon": "CA", "America/Nome": "US",
    "America/Noronha": "BR", "America/North_Dakota/Beulah": "US",
    "America/North_Dakota/Center": "US", "America/North_Dakota/New_Salem": "US",
    "America/Nuuk": "GL", "America/Ojinaga": "MX", "America/Panama": "PA",
    "America/Pangnirtung": "CA", "America/Paramaribo": "SR", "America/Phoenix": "US",
    "America/Port-au-Prince": "HT", "America/Port_of_Spain": "TT",
    "America/Porto_Velho": "BR", "America/Puerto_Rico": "PR",
    "America/Punta_Arenas": "CL", "America/Rainy_River": "CA",
    "America/Rankin_Inlet": "CA", "America/Recife": "BR", "America/Regina": "CA",
    "America/Resolute": "CA", "America/Rio_Branco": "BR", "America/Santarem": "BR",
    "America/Santiago": "CL", "America/Santo_Domingo": "DO", "America/Sao_Paulo": "BR",
    "America/Scoresbysund": "GL", "America/Sitka": "US", "America/St_Barthelemy": "BL",
    "America/St_Johns": "CA", "America/St_Kitts": "KN", "America/St_Lucia": "LC",
    "America/St_Thomas": "VI", "America/St_Vincent": "VC",
    "America/Swift_Current": "CA", "America/Tegucigalpa": "HN",
    "America/Thule": "GL", "America/Thunder_Bay": "CA", "America/Tijuana": "MX",
    "America/Toronto": "CA", "America/Tortola": "VG", "America/Vancouver": "CA",
    "America/Whitehorse": "CA", "America/Winnipeg": "CA", "America/Yakutat": "US",
    "America/Yellowknife": "CA",
    # Antarctica
    "Antarctica/Casey": "AQ", "Antarctica/Davis": "AQ", "Antarctica/DumontDUrville": "AQ",
    "Antarctica/Macquarie": "AU", "Antarctica/Mawson": "AQ", "Antarctica/McMurdo": "AQ",
    "Antarctica/Palmer": "AQ", "Antarctica/Rothera": "AQ", "Antarctica/Syowa": "AQ",
    "Antarctica/Troll": "AQ", "Antarctica/Vostok": "AQ",
    # Arctic
    "Arctic/Longyearbyen": "SJ",
    # Asia
    "Asia/Aden": "YE", "Asia/Almaty": "KZ", "Asia/Amman": "JO",
    "Asia/Anadyr": "RU", "Asia/Aqtau": "KZ", "Asia/Aqtobe": "KZ",
    "Asia/Ashgabat": "TM", "Asia/Atyrau": "KZ", "Asia/Baghdad": "IQ",
    "Asia/Bahrain": "BH", "Asia/Baku": "AZ", "Asia/Bangkok": "TH",
    "Asia/Barnaul": "RU", "Asia/Beirut": "LB", "Asia/Bishkek": "KG",
    "Asia/Brunei": "BN", "Asia/Chita": "RU", "Asia/Choibalsan": "MN",
    "Asia/Colombo": "LK", "Asia/Damascus": "SY", "Asia/Dhaka": "BD",
    "Asia/Dili": "TL", "Asia/Dubai": "AE", "Asia/Dushanbe": "TJ",
    "Asia/Famagusta": "CY", "Asia/Gaza": "PS", "Asia/Hebron": "PS",
    "Asia/Ho_Chi_Minh": "VN", "Asia/Hong_Kong": "HK", "Asia/Hovd": "MN",
    "Asia/Irkutsk": "RU", "Asia/Jakarta": "ID", "Asia/Jayapura": "ID",
    "Asia/Jerusalem": "IL", "Asia/Kabul": "AF", "Asia/Kamchatka": "RU",
    "Asia/Karachi": "PK", "Asia/Kathmandu": "NP", "Asia/Khandyga": "RU",
    "Asia/Kolkata": "IN", "Asia/Krasnoyarsk": "RU", "Asia/Kuala_Lumpur": "MY",
    "Asia/Kuching": "MY", "Asia/Kuwait": "KW", "Asia/Macau": "MO",
    "Asia/Magadan": "RU", "Asia/Makassar": "ID", "Asia/Manila": "PH",
    "Asia/Muscat": "OM", "Asia/Nicosia": "CY", "Asia/Novokuznetsk": "RU",
    "Asia/Novosibirsk": "RU", "Asia/Omsk": "RU", "Asia/Oral": "KZ",
    "Asia/Phnom_Penh": "KH", "Asia/Pontianak": "ID", "Asia/Pyongyang": "KP",
    "Asia/Qatar": "QA", "Asia/Qostanay": "KZ", "Asia/Qyzylorda": "KZ",
    "Asia/Riyadh": "SA", "Asia/Sakhalin": "RU", "Asia/Samarkand": "UZ",
    "Asia/Seoul": "KR", "Asia/Shanghai": "CN", "Asia/Singapore": "SG",
    "Asia/Srednekolymsk": "RU", "Asia/Taipei": "TW", "Asia/Tashkent": "UZ",
    "Asia/Tbilisi": "GE", "Asia/Tehran": "IR", "Asia/Thimphu": "BT",
    "Asia/Tokyo": "JP", "Asia/Tomsk": "RU", "Asia/Ulaanbaatar": "MN",
    "Asia/Urumqi": "CN", "Asia/Ust-Nera": "RU", "Asia/Vientiane": "LA",
    "Asia/Vladivostok": "RU", "Asia/Yakutsk": "RU", "Asia/Yangon": "MM",
    "Asia/Yekaterinburg": "RU", "Asia/Yerevan": "AM",
    # Atlantic
    "Atlantic/Azores": "PT", "Atlantic/Bermuda": "BM", "Atlantic/Canary": "ES",
    "Atlantic/Cape_Verde": "CV", "Atlantic/Faroe": "FO", "Atlantic/Madeira": "PT",
    "Atlantic/Reykjavik": "IS", "Atlantic/South_Georgia": "GS",
    "Atlantic/St_Helena": "SH", "Atlantic/Stanley": "FK",
    # Australia
    "Australia/Adelaide": "AU", "Australia/Brisbane": "AU", "Australia/Broken_Hill": "AU",
    "Australia/Darwin": "AU", "Australia/Eucla": "AU", "Australia/Hobart": "AU",
    "Australia/Lindeman": "AU", "Australia/Lord_Howe": "AU", "Australia/Melbourne": "AU",
    "Australia/Perth": "AU", "Australia/Sydney": "AU",
    # Europe
    "Europe/Amsterdam": "NL", "Europe/Andorra": "AD", "Europe/Astrakhan": "RU",
    "Europe/Athens": "GR", "Europe/Belgrade": "RS", "Europe/Berlin": "DE",
    "Europe/Bratislava": "SK", "Europe/Brussels": "BE", "Europe/Bucharest": "RO",
    "Europe/Budapest": "HU", "Europe/Busingen": "DE", "Europe/Chisinau": "MD",
    "Europe/Copenhagen": "DK", "Europe/Dublin": "IE", "Europe/Gibraltar": "GI",
    "Europe/Guernsey": "GG", "Europe/Helsinki": "FI", "Europe/Isle_of_Man": "IM",
    "Europe/Istanbul": "TR", "Europe/Jersey": "JE", "Europe/Kaliningrad": "RU",
    "Europe/Kiev": "UA", "Europe/Kirov": "RU", "Europe/Kyiv": "UA",
    "Europe/Lisbon": "PT", "Europe/Ljubljana": "SI", "Europe/London": "GB",
    "Europe/Luxembourg": "LU", "Europe/Madrid": "ES", "Europe/Malta": "MT",
    "Europe/Mariehamn": "AX", "Europe/Minsk": "BY", "Europe/Monaco": "MC",
    "Europe/Moscow": "RU", "Europe/Nicosia": "CY", "Europe/Oslo": "NO",
    "Europe/Paris": "FR", "Europe/Podgorica": "ME", "Europe/Prague": "CZ",
    "Europe/Riga": "LV", "Europe/Rome": "IT", "Europe/Samara": "RU",
    "Europe/San_Marino": "SM", "Europe/Sarajevo": "BA", "Europe/Saratov": "RU",
    "Europe/Simferopol": "UA", "Europe/Skopje": "MK", "Europe/Sofia": "BG",
    "Europe/Stockholm": "SE", "Europe/Tallinn": "EE", "Europe/Tirane": "AL",
    "Europe/Ulyanovsk": "RU", "Europe/Uzhgorod": "UA", "Europe/Vaduz": "LI",
    "Europe/Vatican": "VA", "Europe/Vienna": "AT", "Europe/Vilnius": "LT",
    "Europe/Volgograd": "RU", "Europe/Warsaw": "PL", "Europe/Zagreb": "HR",
    "Europe/Zaporozhye": "UA", "Europe/Zurich": "CH",
    # Indian
    "Indian/Antananarivo": "MG", "Indian/Chagos": "IO", "Indian/Christmas": "CX",
    "Indian/Cocos": "CC", "Indian/Comoro": "KM", "Indian/Kerguelen": "TF",
    "Indian/Mahe": "SC", "Indian/Maldives": "MV", "Indian/Mauritius": "MU",
    "Indian/Mayotte": "YT", "Indian/Reunion": "RE",
    # Pacific
    "Pacific/Apia": "WS", "Pacific/Auckland": "NZ", "Pacific/Bougainville": "PG",
    "Pacific/Chatham": "NZ", "Pacific/Chuuk": "FM", "Pacific/Easter": "CL",
    "Pacific/Efate": "VU", "Pacific/Fakaofo": "TK", "Pacific/Fiji": "FJ",
    "Pacific/Funafuti": "TV", "Pacific/Galapagos": "EC", "Pacific/Gambier": "PF",
    "Pacific/Guadalcanal": "SB", "Pacific/Guam": "GU", "Pacific/Honolulu": "US",
    "Pacific/Kanton": "KI", "Pacific/Kiritimati": "KI", "Pacific/Kosrae": "FM",
    "Pacific/Kwajalein": "MH", "Pacific/Majuro": "MH", "Pacific/Marquesas": "PF",
    "Pacific/Midway": "UM", "Pacific/Nauru": "NR", "Pacific/Niue": "NU",
    "Pacific/Norfolk": "NF", "Pacific/Noumea": "NC", "Pacific/Pago_Pago": "AS",
    "Pacific/Palau": "PW", "Pacific/Pitcairn": "PN", "Pacific/Pohnpei": "FM",
    "Pacific/Port_Moresby": "PG", "Pacific/Rarotonga": "CK", "Pacific/Saipan": "MP",
    "Pacific/Tahiti": "PF", "Pacific/Tarawa": "KI", "Pacific/Tongatapu": "TO",
    "Pacific/Wake": "UM", "Pacific/Wallis": "WF",
    # UTC
    "UTC": "XX", "Etc/UTC": "XX", "Etc/GMT": "XX",
}
# fmt: on


def tzid_to_country(tzid: str) -> str:
    """Returns the ISO 3166-1 alpha-2 country code for a tzid, or 'XX' if not found."""
    if tzid in _TZID_TO_COUNTRY:
        return _TZID_TO_COUNTRY[tzid]
    # Fall back to pytz country_timezones reverse lookup
    for country_code, zones in pytz.country_timezones.items():
        if tzid in zones:
            _TZID_TO_COUNTRY[tzid] = country_code  # cache the result
            return country_code
    return "XX"


# ──────────────────────────────────────────────────────────────────────────────
# DST enrichment
# ──────────────────────────────────────────────────────────────────────────────

def get_dst_info(tzid: str, year: int) -> dict:
    """
    Returns a dictionary with DST information for the given timezone.

    Result fields:
        country          - ISO 3166-1 alpha-2 country code
        utc_offset_std   - UTC offset in standard time (hours, float)
        utc_offset_dst   - UTC offset in DST (hours, float)
        has_dst          - True if DST is observed
        dst_start        - DST start rule, POSIX Mm.w.d/hh:mm, or null
        dst_end          - DST end rule, POSIX Mm.w.d/hh:mm, or null
        dst_offset_hours - Clock shift magnitude (usually 1.0) or 0
        abbr_std         - Standard time abbreviation (e.g. "CET")
        abbr_dst         - DST abbreviation (e.g. "CEST")
    """
    try:
        tz = pytz.timezone(tzid)
    except pytz.UnknownTimeZoneError:
        return _empty_dst_info(tzid)

    std_offset, dst_offset, abbr_std, abbr_dst = _get_offsets(tz, year)
    transitions = _find_dst_transitions(tz, year)
    has_dst = len(transitions) >= 2

    dst_start_rule = None
    dst_end_rule   = None

    if has_dst:
        by_offset = sorted(transitions, key=lambda t: t[1], reverse=True)
        t_to_dst   = by_offset[0][0]
        t_from_dst = by_offset[1][0]

        wall_to_dst   = _utc_to_wall(t_to_dst,   std_offset)
        wall_from_dst = _utc_to_wall(t_from_dst, dst_offset)

        dst_start_rule = _to_posix_rule(wall_to_dst)
        dst_end_rule   = _to_posix_rule(wall_from_dst)

    dst_delta = dst_offset - std_offset if has_dst else 0.0

    return {
        "country":          tzid_to_country(tzid),
        "utc_offset_std":   round(std_offset, 2),
        "utc_offset_dst":   round(dst_offset, 2) if has_dst else round(std_offset, 2),
        "has_dst":          has_dst,
        "dst_start":        dst_start_rule,
        "dst_end":          dst_end_rule,
        "dst_offset_hours": round(dst_delta, 2),
        "abbr_std":         abbr_std,
        "abbr_dst":         abbr_dst if has_dst else abbr_std,
    }


def _empty_dst_info(tzid: str = "") -> dict:
    return {
        "country":          tzid_to_country(tzid) if tzid else "XX",
        "utc_offset_std":   0.0,
        "utc_offset_dst":   0.0,
        "has_dst":          False,
        "dst_start":        None,
        "dst_end":          None,
        "dst_offset_hours": 0,
        "abbr_std":         "UTC",
        "abbr_dst":         "UTC",
    }


def _get_offsets(tz, year: int):
    jan = datetime(year, 1, 15, 12, tzinfo=dt_timezone.utc).astimezone(tz)
    jul = datetime(year, 7, 15, 12, tzinfo=dt_timezone.utc).astimezone(tz)
    jan_h = jan.utcoffset().total_seconds() / 3600
    jul_h = jul.utcoffset().total_seconds() / 3600
    if jul_h >= jan_h:
        return jan_h, jul_h, jan.strftime("%Z"), jul.strftime("%Z")
    else:
        return jul_h, jan_h, jul.strftime("%Z"), jan.strftime("%Z")


def _find_dst_transitions(tz, year: int) -> list:
    transitions = []
    prev_offset = None
    prev_dt     = None
    for month in range(1, 13):
        for day in range(1, 32):
            try:
                dt_utc = datetime(year, month, day, 0, tzinfo=dt_timezone.utc)
            except ValueError:
                continue
            offset = dt_utc.astimezone(tz).utcoffset().total_seconds()
            if prev_offset is not None and offset != prev_offset:
                refined = _refine_transition(tz, prev_dt, dt_utc)
                if refined is not None:
                    new_off = refined.astimezone(tz).utcoffset().total_seconds() / 3600
                    transitions.append((refined, new_off))
            prev_offset = offset
            prev_dt     = dt_utc
    return transitions[:2]


def _refine_transition(tz, dt_before: datetime, dt_after: datetime):
    lo, hi = dt_before, dt_after
    offset_lo = lo.astimezone(tz).utcoffset()
    for _ in range(12):
        mid = lo + (hi - lo) / 2
        if mid == lo:
            break
        if mid.astimezone(tz).utcoffset() == offset_lo:
            lo = mid
        else:
            hi = mid
    base     = lo.replace(second=0, microsecond=0)
    base_off = base.astimezone(tz).utcoffset()
    for m in range(120):
        candidate = base + timedelta(minutes=m)
        if candidate.astimezone(tz).utcoffset() != base_off:
            return candidate
    return hi


def _utc_to_wall(dt_utc: datetime, offset_hours: float) -> datetime:
    return (dt_utc + timedelta(hours=offset_hours)).replace(tzinfo=None)


def _to_posix_rule(wall: datetime) -> str:
    """
    POSIX TZ rule: Mm.w.d/hh:mm
      m     - month (1-12)
      w     - week (1-4, or 5 = last)
      d     - day of week: 0=Sun, 1=Mon ... 6=Sat
      hh:mm - wall-clock time of the transition
    """
    month        = wall.month
    day_of_month = wall.day
    posix_dow    = (wall.weekday() + 1) % 7       # Python 0=Mon → POSIX 0=Sun
    week_num     = (day_of_month - 1) // 7 + 1    # 1..5
    last_day     = calendar.monthrange(wall.year, month)[1]
    if day_of_month + 7 > last_day:
        week_num = 5
    return f"M{month}.{week_num}.{posix_dow}/{wall.hour:02d}:{wall.minute:02d}"


# ──────────────────────────────────────────────────────────────────────────────
# Download
# ──────────────────────────────────────────────────────────────────────────────

def download_geojson(release: str, cache_dir: Path) -> Path:
    url      = GITHUB_RELEASE_URL.format(release=release)
    zip_path = cache_dir / f"timezones_{release}.zip"

    if zip_path.exists():
        print(f"✔ Using cached archive: {zip_path}")
    else:
        print(f"⬇ Downloading {url} ...")
        cache_dir.mkdir(parents=True, exist_ok=True)
        with requests.get(url, stream=True, timeout=120) as r:
            r.raise_for_status()
            total = int(r.headers.get("content-length", 0))
            with open(zip_path, "wb") as f, tqdm(
                total=total, unit="B", unit_scale=True, desc="Downloading"
            ) as bar:
                for chunk in r.iter_content(chunk_size=65536):
                    f.write(chunk)
                    bar.update(len(chunk))
        print(f"✔ Archive saved: {zip_path}")

    geojson_path = cache_dir / f"timezones_{release}.geojson"
    if geojson_path.exists():
        print(f"✔ Using cached GeoJSON: {geojson_path}")
        return geojson_path

    print("📦 Extracting archive ...")
    with zipfile.ZipFile(zip_path) as zf:
        names  = zf.namelist()
        target = None
        for candidate in (GEOJSON_FILENAME_IN_ZIP_NEW, GEOJSON_FILENAME_IN_ZIP):
            if candidate in names:
                target = candidate
                break
        if target is None:
            for n in names:
                if "ocean" not in n.lower() and (n.endswith(".json") or n.endswith(".geojson")):
                    target = n
                    break
        if target is None:
            raise FileNotFoundError(f"GeoJSON not found in archive. Contents: {names}")
        print(f"   Extracting: {target}")
        with zf.open(target) as src, open(geojson_path, "wb") as dst:
            dst.write(src.read())

    print(f"✔ GeoJSON saved: {geojson_path}")
    return geojson_path


# ──────────────────────────────────────────────────────────────────────────────
# Geometry simplification
# ──────────────────────────────────────────────────────────────────────────────

def simplify_geometry(geom_dict: dict, tolerance: float) -> dict:
    try:
        geom = shape(geom_dict)
        if not geom.is_valid:
            geom = make_valid(geom)
        simplified = geom.simplify(tolerance, preserve_topology=True)
        if simplified.is_empty:
            simplified = geom
        return mapping(simplified)
    except Exception as e:
        print(f"  ⚠ Simplification error: {e}", file=sys.stderr)
        return geom_dict


# ──────────────────────────────────────────────────────────────────────────────
# Merge polygons for the same tzid into a single Feature
# ──────────────────────────────────────────────────────────────────────────────

def merge_by_tzid(features: list, tolerance: float) -> list:
    """
    Groups all Features by tzid, merges geometries via unary_union,
    simplifies them, and returns a list of Features (one per tzid).
    """
    from collections import defaultdict
    groups: dict[str, list] = defaultdict(list)

    for feat in features:
        tzid = (feat.get("properties") or {}).get("tzid", "")
        geom = feat.get("geometry")
        if geom:
            try:
                g = shape(geom)
                if not g.is_valid:
                    g = make_valid(g)
                groups[tzid].append(g)
            except Exception as e:
                print(f"  ⚠ Skipping geometry for {tzid}: {e}", file=sys.stderr)

    result = []
    for tzid, geoms in tqdm(groups.items(), desc="Merging + simplifying", unit="zone"):
        merged = unary_union(geoms) if len(geoms) > 1 else geoms[0]
        if not merged.is_valid:
            merged = make_valid(merged)
        simplified = merged.simplify(tolerance, preserve_topology=True)
        if simplified.is_empty:
            simplified = merged
        result.append({
            "type": "Feature",
            "geometry": mapping(simplified),
            "properties": {"tzid": tzid},
        })

    return result


# ──────────────────────────────────────────────────────────────────────────────
# Main logic
# ──────────────────────────────────────────────────────────────────────────────

def process(release: str, tolerance: float, output_path: Path, year: int):
    cache_dir = Path(".tz_cache")

    geojson_path = download_geojson(release, cache_dir)

    print("📖 Loading GeoJSON into memory ...")
    with open(geojson_path, encoding="utf-8") as f:
        data = json.load(f)

    features = data.get("features", [])
    print(f"   Source polygons: {len(features)}")

    original_size = geojson_path.stat().st_size / 1024 / 1024

    # Merge polygons by tzid and simplify
    merged_features = merge_by_tzid(features, tolerance)
    print(f"   Unique zones after merging: {len(merged_features)}")

    # Enrich with DST data (once per tzid)
    print("⚙  Enriching with DST data and country codes ...")
    dst_cache: dict[str, dict] = {}
    new_features = []

    for feature in tqdm(merged_features, desc="DST enrichment", unit="zone"):
        props: dict = feature.get("properties", {}) or {}
        tzid: str   = props.get("tzid", "")

        if tzid not in dst_cache:
            dst_cache[tzid] = get_dst_info(tzid, year)

        # Build properties with country first, tzid second
        info = dst_cache[tzid]
        feature["properties"] = {
            "country":          info["country"],
            "tzid":             tzid,
            "utc_offset_std":   info["utc_offset_std"],
            "utc_offset_dst":   info["utc_offset_dst"],
            "has_dst":          info["has_dst"],
            "dst_start":        info["dst_start"],
            "dst_end":          info["dst_end"],
            "dst_offset_hours": info["dst_offset_hours"],
            "abbr_std":         info["abbr_std"],
            "abbr_dst":         info["abbr_dst"],
        }
        new_features.append(feature)

    # Sort by country, then tzid — convenient for reading
    new_features.sort(key=lambda f: (
        f["properties"]["country"],
        f["properties"]["tzid"],
    ))

    out_data = {
        "type": "FeatureCollection",
        "metadata": {
            "source":        f"timezone-boundary-builder {release}",
            "dst_year":      year,
            "tolerance_deg": tolerance,
            "generated":     datetime.utcnow().isoformat() + "Z",
            "feature_count": len(new_features),
            "property_description": {
                "country":          "ISO 3166-1 alpha-2 country code (XX = unknown/international)",
                "tzid":             "IANA timezone identifier",
                "utc_offset_std":   "UTC offset in standard (winter) time, hours",
                "utc_offset_dst":   "UTC offset in DST (summer) time, hours",
                "has_dst":          "True if DST is observed",
                "dst_start":        "DST start rule, POSIX Mm.w.d/hh:mm (e.g. M3.5.0/02:00)",
                "dst_end":          "DST end rule, POSIX Mm.w.d/hh:mm (e.g. M10.5.0/03:00)",
                "dst_offset_hours": "Clock shift magnitude in hours (usually 1.0)",
                "abbr_std":         "Standard time abbreviation (e.g. CET)",
                "abbr_dst":         "DST abbreviation (e.g. CEST)",
            },
        },
        "features": new_features,
    }

    print(f"💾 Saving result → {output_path} ...")
    with open(output_path, "w", encoding="utf-8") as f:
        json.dump(out_data, f, ensure_ascii=False, separators=(",", ":"))

    new_size  = output_path.stat().st_size / 1024 / 1024
    dst_count = sum(1 for v in dst_cache.values() if v["has_dst"])
    countries = len({v["country"] for v in dst_cache.values()})

    print()
    print("═" * 55)
    print(f"  ✅ Done!")
    print(f"  Source size:        {original_size:.1f} MB")
    print(f"  Result:             {new_size:.1f} MB  ({new_size/original_size*100:.0f}%)")
    print(f"  Features (zones):   {len(new_features)}")
    print(f"  Countries:          {countries}")
    print(f"  Zones with DST:     {dst_count} ({dst_count/len(dst_cache)*100:.0f}%)")
    print(f"  File:               {output_path.resolve()}")
    print("═" * 55)


# ──────────────────────────────────────────────────────────────────────────────
# CLI
# ──────────────────────────────────────────────────────────────────────────────

def parse_args():
    parser = argparse.ArgumentParser(
        description="Download, simplify, and enrich a timezone boundary map with DST data."
    )
    parser.add_argument(
        "--tolerance", type=float, default=0.05,
        help="Simplification tolerance in degrees (0.01-0.2). Default: 0.05"
    )
    parser.add_argument(
        "--output", type=str, default="timezones_dst.geojson",
        help="Output filename. Default: timezones_dst.geojson"
    )
    parser.add_argument(
        "--release", type=str, default="2025b",
        help="timezone-boundary-builder release tag. Default: 2025b"
    )
    parser.add_argument(
        "--year", type=int, default=datetime.now().year,
        help="Year used for DST transition analysis. Default: current year"
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()
    process(
        release=args.release,
        tolerance=args.tolerance,
        output_path=Path(args.output),
        year=args.year,
    )
