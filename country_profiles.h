#pragma once
// country_profiles.h — home-location hints for GPS warm start
//
// Each entry provides:
//   code     — ISO 3166-1 alpha-2 (or "CUSTOM" for a user-defined location)
//   homeLat  — approximate home latitude  (used for warm-start MGA hint)
//   homeLng  — approximate home longitude
//   homeAlt  — approximate home altitude in metres
//
// Set COUNTRY_CODE in gps_speedometer.ino to one of the codes below,
// or to CUSTOM and fill in the first row with your own coordinates.

#include <string.h>
#include <stdint.h>

struct CountryProfile {
  const char* code;
  float       homeLat;
  float       homeLng;
  float       homeAlt;
};

static const CountryProfile COUNTRY_PROFILES[] = {
	{ "CUSTOM",  51.0369f,  7.5852f, 250.0f }, // add your house or work location
    { "AD", 42.507f,   1.521f, 1023.0f }, // Andorra la Vella, Andorra
    { "AE", 24.467f,  54.367f,    5.0f }, // Abu Dhabi, United Arab Emirates
    { "AF", 34.529f,  69.172f, 1791.0f }, // Kabul, Afghanistan
    { "AG", 17.118f, -61.845f,    0.0f }, // St. John's, Antigua and Barbuda
    { "AL", 41.328f,  19.819f,  104.0f }, // Tirana, Albania
    { "AM", 40.179f,  44.499f,  989.0f }, // Yerevan, Armenia
    { "AO", -8.837f,  13.234f,   74.0f }, // Luanda, Angola
    { "AR", -34.603f, -58.382f,  25.0f }, // Buenos Aires, Argentina
    { "AT", 48.208f,  16.373f,  171.0f }, // Vienna, Austria
    { "AU", -35.281f, 149.130f,  577.0f }, // Canberra, Australia
    { "AZ", 40.409f,  49.867f,  -28.0f }, // Baku, Azerbaijan
    { "BA", 43.856f,  18.413f,  518.0f }, // Sarajevo, Bosnia and Herzegovina
    { "BB", 13.098f, -59.618f,    0.0f }, // Bridgetown, Barbados
    { "BD", 23.810f,  90.413f,    4.0f }, // Dhaka, Bangladesh
    { "BE", 50.850f,   4.352f,   20.0f }, // Brussels, Belgium
    { "BF", 12.371f,  -1.520f,  305.0f }, // Ouagadougou, Burkina Faso
    { "BG", 42.698f,  23.323f,  556.0f }, // Sofia, Bulgaria
    { "BH", 26.229f,  50.583f,    0.0f }, // Manama, Bahrain
    { "BI", -3.383f,  29.367f,  772.0f }, // Gitega, Burundi
    { "BJ",  6.497f,   2.605f,   38.0f }, // Porto-Novo, Benin
    { "BN",  4.890f, 114.942f,    0.0f }, // Bandar Seri Begawan, Brunei Darussalam
    { "BO", -16.500f, -68.150f, 3640.0f }, // La Paz, Bolivia
    { "BR", -15.827f, -47.922f, 1172.0f }, // Brasilia, Brazil
    { "BS", 25.066f, -77.340f,    0.0f }, // Nassau, Bahamas
    { "BT", 27.472f,  89.639f, 2320.0f }, // Thimphu, Bhutan
    { "BW", -24.654f, 25.912f, 1005.0f }, // Gaborone, Botswana
    { "BY", 53.900f,  27.559f,  203.0f }, // Minsk, Belarus
    { "BZ", 17.251f, -88.768f,    5.0f }, // Belmopan, Belize
    { "CA", 45.421f, -75.697f,   70.0f }, // Ottawa, Canada
    { "CD", -4.328f,  15.313f,  240.0f }, // Kinshasa, Democratic Republic of the Congo
    { "CF",  4.394f,  18.558f,  369.0f }, // Bangui, Central African Republic
    { "CG", -4.263f,  15.242f,   10.0f }, // Brazzaville, Republic of the Congo
    { "CH", 46.948f,   7.447f,  542.0f }, // Bern, Switzerland
    { "CI",  6.827f,  -5.280f,  245.0f }, // Yamoussoukro, Côte d'Ivoire
    { "CL", -33.448f, -70.669f,  520.0f }, // Santiago, Chile
    { "CM",  3.848f,  11.502f,  726.0f }, // Yaoundé, Cameroon
    { "CN", 39.904f, 116.407f,   44.0f }, // Beijing, China
    { "CO",  4.711f, -74.072f, 2640.0f }, // Bogotá, Colombia
    { "CR",  9.928f, -84.090f, 1170.0f }, // San José, Costa Rica
    { "CU", 23.113f, -82.366f,   24.0f }, // Havana, Cuba
    { "CV", 14.933f, -23.513f,    0.0f }, // Praia, Cabo Verde
    { "CY", 35.166f,  33.367f,  140.0f }, // Nicosia, Cyprus
    { "CZ", 50.075f,  14.438f,  235.0f }, // Prague, Czechia
    { "DE", 52.520f,  13.405f,   34.0f }, // Berlin, Germany
    { "DJ", 11.589f,  43.145f,    0.0f }, // Djibouti, Djibouti
    { "DK", 55.676f,  12.568f,    5.0f }, // Copenhagen, Denmark
    { "DM", 15.301f, -61.388f,    0.0f }, // Roseau, Dominica
    { "DO", 18.486f, -69.931f,   14.0f }, // Santo Domingo, Dominican Republic
    { "DZ", 36.753f,   3.059f,   24.0f }, // Algiers, Algeria
    { "EC", -0.180f, -78.467f, 2850.0f }, // Quito, Ecuador
    { "EE", 59.437f,  24.753f,   10.0f }, // Tallinn, Estonia
    { "EG", 30.044f,  31.236f,   23.0f }, // Cairo, Egypt
    { "ER", 15.322f,  38.925f, 2325.0f }, // Asmara, Eritrea
    { "ES", 40.417f,  -3.704f,  657.0f }, // Madrid, Spain
    { "ET",  9.030f,  38.740f, 2355.0f }, // Addis Ababa, Ethiopia
    { "FI", 60.170f,  24.938f,   17.0f }, // Helsinki, Finland
    { "FJ", -18.141f, 178.442f,    0.0f }, // Suva, Fiji
    { "FM",  6.917f, 158.155f,    0.0f }, // Palikir, Micronesia
    { "FR", 48.857f,   2.352f,   35.0f }, // Paris, France
    { "GA",  0.417f,   9.453f,    0.0f }, // Libreville, Gabon
    { "GB", 51.507f,  -0.128f,   11.0f }, // London, United Kingdom
    { "GD", 12.052f, -61.749f,    0.0f }, // St. George's, Grenada
    { "GE", 41.693f,  44.801f,  770.0f }, // Tbilisi, Georgia
    { "GH",  5.603f,  -0.187f,   61.0f }, // Accra, Ghana
    { "GM", 13.453f, -16.578f,    0.0f }, // Banjul, Gambia
    { "GN",  9.641f, -13.578f,   23.0f }, // Conakry, Guinea
    { "GQ",  3.750f,   8.783f,    0.0f }, // Malabo, Equatorial Guinea
    { "GR", 37.984f,  23.728f,   95.0f }, // Athens, Greece
    { "GT", 14.634f, -90.507f, 1500.0f }, // Guatemala City, Guatemala
    { "GW", 11.864f, -15.598f,    0.0f }, // Bissau, Guinea-Bissau
    { "GY",  6.805f, -58.162f,    0.0f }, // Georgetown, Guyana
    { "HN", 14.082f, -87.206f,  975.0f }, // Tegucigalpa, Honduras
    { "HR", 45.815f,  15.978f,  130.0f }, // Zagreb, Croatia
    { "HT", 18.539f, -72.336f,   34.0f }, // Port-au-Prince, Haiti
    { "HU", 47.498f,  19.040f,   96.0f }, // Budapest, Hungary
    { "ID", -6.208f, 106.846f,    8.0f }, // Jakarta, Indonesia
    { "IE", 53.350f,  -6.260f,   20.0f }, // Dublin, Ireland
    { "IL", 31.780f,  35.235f,  754.0f }, // Jerusalem, Israel
    { "IN", 28.614f,  77.209f,  216.0f }, // New Delhi, India
    { "IQ", 33.315f,  44.366f,   34.0f }, // Baghdad, Iraq
    { "IR", 35.689f,  51.389f, 1130.0f }, // Tehran, Iran
    { "IS", 64.147f, -21.942f,   25.0f }, // Reykjavik, Iceland
    { "IT", 41.903f,  12.496f,   20.0f }, // Rome, Italy
    { "JM", 18.017f, -76.809f,   65.0f }, // Kingston, Jamaica
    { "JO", 31.950f,  35.933f,  780.0f }, // Amman, Jordan
    { "JP", 35.676f, 139.650f,   40.0f }, // Tokyo, Japan
    { "KE", -1.292f,  36.821f, 1795.0f }, // Nairobi, Kenya
    { "KG", 42.874f,  74.612f,  760.0f }, // Bishkek, Kyrgyzstan
    { "KH", 11.550f, 104.917f,   15.0f }, // Phnom Penh, Cambodia
    { "KI",  1.329f, 172.979f,    0.0f }, // South Tarawa, Kiribati
    { "KM", -11.704f,  43.240f,   0.0f }, // Moroni, Comoros
    { "KN", 17.333f, -62.733f,    0.0f }, // Basseterre, Saint Kitts and Nevis
    { "KP", 39.039f, 125.763f,   12.0f }, // Pyongyang, North Korea
    { "KR", 37.566f, 126.978f,   38.0f }, // Seoul, South Korea
    { "KW", 29.375f,  47.977f,    0.0f }, // Kuwait City, Kuwait
    { "KZ", 51.169f,  71.449f,  347.0f }, // Astana, Kazakhstan
    { "LA", 17.975f, 102.633f,  170.0f }, // Vientiane, Laos
    { "LB", 33.893f,  35.503f,   10.0f }, // Beirut, Lebanon
    { "LC", 14.010f, -60.988f,    0.0f }, // Castries, Saint Lucia
    { "LI", 47.141f,   9.521f,  460.0f }, // Vaduz, Liechtenstein
    { "LK",  6.927f,  79.861f,    5.0f }, // Sri Jayawardenepura Kotte, Sri Lanka
    { "LR",  6.315f, -10.807f,   32.0f }, // Monrovia, Liberia
    { "LS", -29.317f, 27.483f, 1555.0f }, // Maseru, Lesotho
    { "LT", 54.687f,  25.280f,  112.0f }, // Vilnius, Lithuania
    { "LU", 49.611f,   6.132f,  280.0f }, // Luxembourg, Luxembourg
    { "LV", 56.949f,  24.106f,   13.0f }, // Riga, Latvia
    { "LY", 32.887f,  13.191f,   81.0f }, // Tripoli, Libya
    { "MA", 33.971f,  -6.849f,   75.0f }, // Rabat, Morocco
    { "MC", 43.733f,   7.417f,   19.0f }, // Monaco, Monaco
    { "MD", 47.026f,  28.835f,   55.0f }, // Chișinău, Moldova
    { "ME", 42.441f,  19.263f,   40.0f }, // Podgorica, Montenegro
    { "MG", -18.879f, 47.507f, 1430.0f }, // Antananarivo, Madagascar
    { "MH",  7.089f, 171.380f,    0.0f }, // Majuro, Marshall Islands
    { "MK", 41.997f,  21.428f,  240.0f }, // Skopje, North Macedonia
    { "ML", 12.639f,  -8.002f,  380.0f }, // Bamako, Mali
    { "MM", 16.871f,  96.199f,   15.0f }, // Naypyidaw, Myanmar
    { "MN", 47.921f, 106.918f, 1350.0f }, // Ulaanbaatar, Mongolia
    { "MR", 18.074f, -15.958f,    0.0f }, // Nouakchott, Mauritania
    { "MT", 35.899f,  14.514f,   48.0f }, // Valletta, Malta
    { "MU", -20.166f, 57.503f,    0.0f }, // Port Louis, Mauritius
    { "MV",  4.175f,  73.509f,    0.0f }, // Malé, Maldives
    { "MW", -13.963f, 33.774f, 1050.0f }, // Lilongwe, Malawi
    { "MX", 19.433f, -99.133f, 2250.0f }, // Mexico City, Mexico
    { "MY",  3.139f, 101.687f,   21.0f }, // Kuala Lumpur, Malaysia
    { "MZ", -25.966f, 32.583f,   10.0f }, // Maputo, Mozambique
    { "NA", -22.560f, 17.084f, 1660.0f }, // Windhoek, Namibia
    { "NE", 13.512f,   2.112f,  183.0f }, // Niamey, Niger
    { "NG",  9.057f,   7.489f,  360.0f }, // Abuja, Nigeria
    { "NI", 12.136f, -86.251f,   59.0f }, // Managua, Nicaragua
    { "NL", 52.370f,   4.895f,    2.0f }, // Amsterdam, Netherlands
    { "NO", 59.914f,  10.752f,   23.0f }, // Oslo, Norway
    { "NP", 27.717f,  85.324f, 1400.0f }, // Kathmandu, Nepal
    { "NR", -0.533f, 166.917f,    0.0f }, // Yaren, Nauru
    { "NZ", -41.286f, 174.776f,  10.0f }, // Wellington, New Zealand
    { "OM", 23.588f,  58.382f,    0.0f }, // Muscat, Oman
    { "PA",  8.983f, -79.520f,   10.0f }, // Panama City, Panama
    { "PE", -12.046f, -77.043f,  154.0f }, // Lima, Peru
    { "PG", -9.479f, 147.149f,   14.0f }, // Port Moresby, Papua New Guinea
    { "PH", 14.599f, 120.984f,    5.0f }, // Manila, Philippines
    { "PK", 33.684f,  73.048f,  560.0f }, // Islamabad, Pakistan
    { "PL", 52.230f,  21.012f,  113.0f }, // Warsaw, Poland
    { "PT", 38.722f,  -9.139f,   25.0f }, // Lisbon, Portugal
    { "PW",  7.500f, 134.625f,    0.0f }, // Ngerulmud, Palau
    { "PY", -25.264f, -57.636f,  55.0f }, // Asunción, Paraguay
    { "QA", 25.286f,  51.533f,    0.0f }, // Doha, Qatar
    { "RO", 44.427f,  26.102f,   80.0f }, // Bucharest, Romania
    { "RS", 44.817f,  20.457f,  117.0f }, // Belgrade, Serbia
    { "RU", 55.755f,  37.617f,  156.0f }, // Moscow, Russia
    { "RW", -1.944f,  30.059f, 1567.0f }, // Kigali, Rwanda
    { "SA", 24.713f,  46.675f,  612.0f }, // Riyadh, Saudi Arabia
    { "SB", -9.433f, 159.950f,    0.0f }, // Honiara, Solomon Islands
    { "SC", -4.620f,  55.455f,    0.0f }, // Victoria, Seychelles
    { "SD", 15.551f,  32.532f,  381.0f }, // Khartoum, Sudan
    { "SE", 59.329f,  18.069f,   15.0f }, // Stockholm, Sweden
    { "SG",  1.352f, 103.820f,    0.0f }, // Singapore, Singapore
    { "SI", 46.056f,  14.508f,  295.0f }, // Ljubljana, Slovenia
    { "SK", 48.148f,  17.107f,  140.0f }, // Bratislava, Slovakia
    { "SL",  8.465f, -13.231f,   26.0f }, // Freetown, Sierra Leone
    { "SM", 43.936f,  12.447f,  300.0f }, // San Marino, San Marino
    { "SN", 14.693f, -17.444f,    0.0f }, // Dakar, Senegal
    { "SO",  2.047f,  45.326f,    9.0f }, // Mogadishu, Somalia
    { "SR",  5.852f, -55.204f,    0.0f }, // Paramaribo, Suriname
    { "SS",  4.859f,  31.571f,  466.0f }, // Juba, South Sudan
    { "ST",  0.337f,   6.731f,    0.0f }, // São Tomé, São Tomé and Príncipe
    { "SV", 13.694f, -89.192f,  682.0f }, // San Salvador, El Salvador
    { "SY", 33.513f,  36.292f,  680.0f }, // Damascus, Syria
    { "SZ", -26.305f, 31.136f,  670.0f }, // Mbabane, Eswatini
    { "TD", 12.105f,  15.044f,  298.0f }, // N'Djamena, Chad
    { "TG",  6.172f,   1.232f,   63.0f }, // Lomé, Togo
    { "TH", 13.756f, 100.502f,    2.0f }, // Bangkok, Thailand
    { "TJ", 38.560f,  68.774f,  800.0f }, // Dushanbe, Tajikistan
    { "TL", -8.559f, 125.579f,    0.0f }, // Dili, Timor-Leste
    { "TM", 37.960f,  58.326f,  219.0f }, // Ashgabat, Turkmenistan
    { "TN", 36.807f,  10.165f,   10.0f }, // Tunis, Tunisia
    { "TO", -21.139f,-175.198f,   0.0f }, // Nukuʻalofa, Tonga
    { "TR", 39.933f,  32.859f,  938.0f }, // Ankara, Türkiye
    { "TT", 10.654f, -61.519f,    0.0f }, // Port of Spain, Trinidad and Tobago
    { "TV", -8.517f, 179.198f,    0.0f }, // Funafuti, Tuvalu
    { "TZ", -6.163f,  35.751f, 1120.0f }, // Dodoma, Tanzania
    { "UA", 50.450f,  30.523f,  179.0f }, // Kyiv, Ukraine
    { "UG",  0.347f,  32.583f, 1190.0f }, // Kampala, Uganda
    { "US", 38.895f, -77.036f,   10.0f }, // Washington, D.C., United States of America
    { "UY", -34.901f, -56.164f,  43.0f }, // Montevideo, Uruguay
    { "UZ", 41.299f,  69.240f,  440.0f }, // Tashkent, Uzbekistan
    { "VA", 41.902f,  12.453f,   48.0f }, // Vatican City, Holy See
    { "VC", 13.160f, -61.235f,    0.0f }, // Kingstown, Saint Vincent and the Grenadines
    { "VE", 10.491f, -66.902f,  920.0f }, // Caracas, Venezuela
    { "VN", 21.028f, 105.834f,   16.0f }, // Hanoi, Vietnam
    { "VU", -17.733f, 168.322f,   0.0f }, // Port Vila, Vanuatu
    { "WS", -13.833f,-171.767f,   0.0f }, // Apia, Samoa
    { "YE", 15.352f,  44.208f, 2250.0f }, // Sana'a, Yemen
    { "ZA", -25.747f, 28.188f, 1310.0f }, // Pretoria, South Africa
    { "ZM", -15.387f, 28.322f, 1170.0f }, // Lusaka, Zambia
    { "ZW", -17.829f, 31.053f, 1480.0f }  // Harare, Zimbabwe
};
static const int COUNTRY_PROFILES_N =
    (int)(sizeof(COUNTRY_PROFILES) / sizeof(COUNTRY_PROFILES[0]));

// Returns a pointer to the profile matching `code`, or COUNTRY_PROFILES[1] ("DE")
// as a sensible European fallback when the code is not found.
static inline const CountryProfile* findCountryProfile(const char* code) {
  for (int i = 0; i < COUNTRY_PROFILES_N; i++)
    if (strcmp(COUNTRY_PROFILES[i].code, code) == 0)
      return &COUNTRY_PROFILES[i];
  return &COUNTRY_PROFILES[1];  // fallback: DE
}