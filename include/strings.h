/**
 * strings.h — Multi-language LCD text for the Environmental Game Controller
 *
 * Supported languages: English (0), Spanish (1), Catalan (2)
 * Selected via the AP mode web interface; persisted in Preferences ("lang" key).
 *
 * HD44780 custom character slots:
 *   Slot 0  — skull glyph (defined inside displayDefeatScreen; NOT for text)
 *   Slots 1–5 — language accent characters (defined by setLanguage() at boot)
 *   Slots 6–7 — ü and ö (fixed; used by sanitizeForLCD for API city names)
 *
 * Accent character codes embedded in string literals:
 *   Spanish  \x01=ñ  \x02=á  \x03=é  \x04=ó  \x05=í
 *   Catalan  \x01=é  \x02=ó  \x03=ò  \x04=í  \x05=ç
 *   English  (no custom chars — standard ASCII only)
 *
 * IMPORTANT: Catalan strings are currently ASCII-only placeholders.
 * A native Catalan speaker should review the text and insert \x01–\x05 codes
 * where accents are needed (é, ó, ò, í, ç).
 *
 * NOTE: All bitmaps should be verified on physical hardware. The 5×8
 * pixel shapes are common representations but may need tuning per LCD module.
 */

#ifndef STRINGS_H
#define STRINGS_H

#include <stdint.h>

// ─── Language identifiers ────────────────────────────────────────────────────
#define LANG_EN 0
#define LANG_ES 1
#define LANG_CA 2

// ─── GameStrings struct ──────────────────────────────────────────────────────
// All LCD text for one language. Strings must be ≤ 20 chars (LCD column width).
// defeat_title must be exactly 18 chars (fills Row 0 between two skull chars).
// score_o3_label must be exactly 6 chars (scoreboard column alignment).

struct GameStrings {
    // Custom character bitmaps for CGRAM slots 1–5 (8 bytes each).
    // nullptr = slot unused (English needs none).
    const uint8_t* cc1;
    const uint8_t* cc2;
    const uint8_t* cc3;
    const uint8_t* cc4;
    const uint8_t* cc5;

    // Startup screen (Row 1 "v1.0" is always the same)
    const char* startup_title;    // Row 0
    const char* startup_ready;    // Row 2
    const char* startup_loading;  // Row 3

    // No Data screen
    const char* nodata_r0;
    const char* nodata_r1;
    const char* nodata_r2;
    const char* nodata_r3;

    // PM2.5 educational slide (slide 0) — all 4 rows
    const char* pm25_s0_r0;
    const char* pm25_s0_r1;
    const char* pm25_s0_r2;
    const char* pm25_s0_r3;

    // NO2 educational slide (slide 0)
    const char* no2_s0_r0;
    const char* no2_s0_r1;
    const char* no2_s0_r2;
    const char* no2_s0_r3;

    // O3 educational slide (slide 0)
    const char* o3_s0_r0;
    const char* o3_s0_r1;
    const char* o3_s0_r2;
    const char* o3_s0_r3;

    // WHO slide (slide 1) — rows 0–1 shared, row 2 per pollutant.
    // Row 3 is always the WHO value ("5 ug/m3", "5 ppb", "30 ppb") — not translated.
    const char* who_r0;
    const char* who_r1;
    const char* who_pm25_r2;
    const char* who_no2_r2;
    const char* who_o3_r2;

    // Gate slide (slide 2) — O3 pollutant name + two-row button prompts.
    // PM2.5 and NO2 names are universal (scientific notation).
    const char* gate_o3_name;   // "Ozone" / "Ozono" / "Ozo"
    const char* gate_next_r2;   // PM2.5 + NO2 gate row 2
    const char* gate_next_r3;   // PM2.5 + NO2 gate row 3
    const char* gate_last_r2;   // O3 gate row 2
    const char* gate_last_r3;   // O3 gate row 3

    // Waiting screen
    const char* waiting_r1;
    const char* waiting_r2;

    // Countdown
    const char* countdown_label;

    // Defeat screen — Row 0 middle text (exactly 18 chars, flanked by skull chars)
    const char* defeat_title;

    // Scoreboard O3 label (exactly 6 chars; PM2.5 and NO2 labels are universal)
    const char* score_o3_label;

    // Gate slide Row 1 prefix before "<value> <unit>" (include trailing space)
    const char* gate_today_prefix;  // "Today: " / "Hoy: " / "Avui: "

    // Scoreboard header row (exactly 20 chars, includes "|" divider and padding)
    const char* score_header;  // "   Now    |  Goal   "

    // Victory screen
    const char* victory_r0;
    const char* victory_r1;
    const char* victory_city_sfx;  // appended to city name on Row 2
    const char* victory_r3;

    // Defeat screen rows 1–3
    const char* defeat_city_sfx;  // appended to city name on Row 1
    const char* defeat_r2;
    const char* defeat_r3;

    // Attract / start screen (shown before city-select and after inactivity)
    const char* attract_title;    // Row 0: game title (≤ 20 chars)
    const char* attract_tagline;  // Row 1: short tagline (≤ 20 chars)
    const char* attract_prompt;   // Row 3: call-to-action (≤ 20 chars)
    // Row 2 is always the decorative separator "- - - - - - - - -" (not translated)
};

// ─── Custom character bitmaps ─────────────────────────────────────────────────
// Each bitmap is 8 bytes, one per LCD pixel row (5 active bits, MSB-padded).
// Verify appearance on physical hardware; adjust rows if needed.

// Spanish: ñ á é ó í  (slots 1–5)
static const uint8_t CC_ES_1_N_TILDE[8] = {0x0A,0x00,0x1E,0x11,0x11,0x11,0x11,0x00}; // ñ
static const uint8_t CC_ES_2_A_ACUTE[8] = {0x02,0x04,0x0E,0x01,0x0F,0x11,0x0F,0x00}; // á
static const uint8_t CC_ES_3_E_ACUTE[8] = {0x02,0x04,0x0E,0x11,0x1F,0x10,0x0E,0x00}; // é
static const uint8_t CC_ES_4_O_ACUTE[8] = {0x02,0x04,0x0E,0x11,0x11,0x11,0x0E,0x00}; // ó
static const uint8_t CC_ES_5_I_ACUTE[8] = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00}; // í

// Catalan: é ó ò í ç  (slots 1–5)
// Update strings once native speaker confirms text; add \x01–\x05 where needed.
static const uint8_t CC_CA_1_E_ACUTE[8] = {0x02,0x04,0x0E,0x11,0x1F,0x10,0x0E,0x00}; // é
static const uint8_t CC_CA_2_O_ACUTE[8] = {0x02,0x04,0x0E,0x11,0x11,0x11,0x0E,0x00}; // ó
static const uint8_t CC_CA_3_O_GRAVE[8] = {0x04,0x02,0x0E,0x11,0x11,0x11,0x0E,0x00}; // ò
static const uint8_t CC_CA_4_I_ACUTE[8] = {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E,0x00}; // í
static const uint8_t CC_CA_5_C_CEDIL[8] = {0x00,0x0E,0x10,0x10,0x10,0x0E,0x04,0x06}; // ç

// Fixed CGRAM slots 6–7: umlaut characters for API city names (language-independent).
// Loaded by initCustomChars() on every language change and after every lcd.clear().
// sanitizeForLCD() maps ü→\x06, Ü→\x06, ö→\x07, Ö→\x07.
static const uint8_t CC_U_UMLAUT[8] = {0x0A,0x00,0x11,0x11,0x11,0x0E,0x00,0x00}; // ü (slot 6)
static const uint8_t CC_O_UMLAUT[8] = {0x0A,0x00,0x0E,0x11,0x11,0x0E,0x00,0x00}; // ö (slot 7)

// ─── English ─────────────────────────────────────────────────────────────────
static const GameStrings STRINGS_EN = {
    /* cc1 */ nullptr,
    /* cc2 */ nullptr,
    /* cc3 */ nullptr,
    /* cc4 */ nullptr,
    /* cc5 */ nullptr,

    /* startup_title   */ "Air Defender",
    /* startup_ready   */ "LCD Display Ready",
    /* startup_loading */ "Loading AQI data...",

    /* nodata_r0 */ "Environmental AQI",
    /* nodata_r1 */ "No data available",
    /* nodata_r2 */ "Hold blue on boot",
    /* nodata_r3 */ "to start AP mode",

    /* pm25_s0_r0 */ "PM2.5: Fine Dust",
    /* pm25_s0_r1 */ "Particles <2.5um",
    /* pm25_s0_r2 */ "Bad for lungs",
    /* pm25_s0_r3 */ "Enters bloodstream",

    /* no2_s0_r0 */ "Nitrogen Dioxide",
    /* no2_s0_r1 */ "From burning fuels",
    /* no2_s0_r2 */ "Worsens asthma",
    /* no2_s0_r3 */ "Irritates airways",

    /* o3_s0_r0 */ "Ozone: Ground Level",
    /* o3_s0_r1 */ "Toxic smog made",
    /* o3_s0_r2 */ "when sunlight",
    /* o3_s0_r3 */ "heats exhaust",

    /* who_r0       */ "World Health Org.",
    /* who_r1       */ "recommends safe",
    /* who_pm25_r2  */ "PM2.5 level:",
    /* who_no2_r2   */ "NO2 level:",
    /* who_o3_r2    */ "Ozone level:",

    /* gate_o3_name */ "Ozone",
    /* gate_next_r2 */ "Press button for",
    /* gate_next_r3 */ "next pollutant",
    /* gate_last_r2 */ "Press button",
    /* gate_last_r3 */ "to start battle!",

    /* waiting_r1 */ "Loading next",
    /* waiting_r2 */ "pollutant data...",

    /* countdown_label */ "BATTLE IN",

    /* defeat_title */ "    SMOG WINS!    ",

    /* score_o3_label    */ "OZONE:",
    /* gate_today_prefix */ "Today: ",
    /* score_header      */ "   Now    |  Goal   ",

    /* victory_r0        */ "Congratulations!",
    /* victory_r1        */ "Air Defender!",
    /* victory_city_sfx  */ " is clean!",
    /* victory_r3        */ "Choose next city >",

    /* defeat_city_sfx */ " fell!",
    /* defeat_r2       */ "Choose another city",
    /* defeat_r3       */ "or try again.",

    /* attract_title   */ "* AIR DEFENDER *",
    /* attract_tagline */ "Clean the Air!",
    /* attract_prompt  */ "PRESS ANY BUTTON",
};

// ─── Spanish ─────────────────────────────────────────────────────────────────
// Char codes: \x01=ñ  \x02=á  \x03=é  \x04=ó  \x05=í
// For review: native Spanish speaker should verify phrasing.
// Accents included where omission changes meaning (ñ, á in "está").
// Optional additional accents can be added using the codes above.
static const GameStrings STRINGS_ES = {
    /* cc1 */ CC_ES_1_N_TILDE,
    /* cc2 */ CC_ES_2_A_ACUTE,
    /* cc3 */ CC_ES_3_E_ACUTE,
    /* cc4 */ CC_ES_4_O_ACUTE,
    /* cc5 */ CC_ES_5_I_ACUTE,

    /* startup_title   */ "Defensor del Aire",
    /* startup_ready   */ "Pantalla lista",
    /* startup_loading */ "Cargando datos...",

    /* nodata_r0 */ "Estado del Aire",
    /* nodata_r1 */ "Sin datos cargados",
    /* nodata_r2 */ "Mant\x03n azul al boot",   // Mantén
    /* nodata_r3 */ "para modo AP",

    /* pm25_s0_r0 */ "PM2.5: Polvo Fino",
    /* pm25_s0_r1 */ "Part\x05" "culas <2.5um",  // Partículas
    /* pm25_s0_r2 */ "Da\x01ino a pulmones",     // Dañino
    /* pm25_s0_r3 */ "Entra al torrente",

    /* no2_s0_r0 */ "Di\x04xido Nitr\x04geno",  // Dióxido Nitrógeno
    /* no2_s0_r1 */ "De combustibles",
    /* no2_s0_r2 */ "Empeora el asma",
    /* no2_s0_r3 */ "Irrita las v\x05" "as",     // vías

    /* o3_s0_r0 */ "Ozono: Nivel Suelo",
    /* o3_s0_r1 */ "Smog t\x04xico formado",     // tóxico
    /* o3_s0_r2 */ "cuando el sol",
    /* o3_s0_r3 */ "calienta el escape",

    /* who_r0       */ "Org. Mundial Salud",
    /* who_r1       */ "recomienda nivel",
    /* who_pm25_r2  */ "PM2.5 seguro:",
    /* who_no2_r2   */ "NO2 seguro:",
    /* who_o3_r2    */ "Ozono seguro:",

    /* gate_o3_name */ "Ozono",
    /* gate_next_r2 */ "Presiona bot\x04n",       // botón
    /* gate_next_r3 */ "para siguiente",
    /* gate_last_r2 */ "Presiona bot\x04n",
    /* gate_last_r3 */ "para batallar!",

    /* waiting_r1 */ "Cargando siguiente",
    /* waiting_r2 */ "contaminante...",

    /* countdown_label */ "BATALLA EN",

    /* defeat_title */ "  POLUCI\x04N GANA!  ",

    /* score_o3_label    */ "OZONO:",
    /* gate_today_prefix */ "Hoy: ",
    /* score_header      */ "  Ahora   |  Meta   ",

    /* victory_r0        */ "Felicitaciones!",
    /* victory_r1        */ "Defensor del Aire!",
    /* victory_city_sfx  */ " est\x02 limpia!",   // está
    /* victory_r3        */ "Elige prox. ciudad>",

    /* defeat_city_sfx */ " ha caido!",
    /* defeat_r2       */ "Elige otra ciudad",
    /* defeat_r3       */ "o intentalo.",

    /* attract_title   */ "DEFENSOR DEL AIRE",
    /* attract_tagline */ "Limpia el Aire!",
    /* attract_prompt  */ "PRESIONA UN BOT\x04N",
};

// ─── Catalan ──────────────────────────────────────────────────────────────────
// Char codes: \x01=é  \x02=ó  \x03=ò  \x04=í  \x05=ç
// Phrasing should be reviewed by a native Catalan speaker.
// Additional accents can be added using the codes above.
static const GameStrings STRINGS_CA = {
    /* cc1 */ CC_CA_1_E_ACUTE,
    /* cc2 */ CC_CA_2_O_ACUTE,
    /* cc3 */ CC_CA_3_O_GRAVE,
    /* cc4 */ CC_CA_4_I_ACUTE,
    /* cc5 */ CC_U_UMLAUT,           // ü (ç unused; ü needed for "següent")

    /* startup_title   */ "Defensor de l'Aire",
    /* startup_ready   */ "Pantalla llesta",
    /* startup_loading */ "Carregant dades...",

    /* nodata_r0 */ "Estat de l'Aire",
    /* nodata_r1 */ "Sense dades",
    /* nodata_r2 */ "Prem blau a l'inici",
    /* nodata_r3 */ "per al mode AP",

    /* pm25_s0_r0 */ "PM2.5: Pols Fi",
    /* pm25_s0_r1 */ "Part\x04" "cules <2.5um",  // Partícules
    /* pm25_s0_r2 */ "Dolent pels pulmons",
    /* pm25_s0_r3 */ "Passa a la sang",

    /* no2_s0_r0 */ "Di\x03xid de Nitrogen",    // Diòxid
    /* no2_s0_r1 */ "De combustibles",
    /* no2_s0_r2 */ "Empitjora l'asma",
    /* no2_s0_r3 */ "Irrita les vies",

    /* o3_s0_r0 */ "Oz\x02: Nivell Terra",       // Ozó
    /* o3_s0_r1 */ "Smog t\x03xic creat",        // tòxic
    /* o3_s0_r2 */ "quan el sol",
    /* o3_s0_r3 */ "escalfa l'escapament",

    /* who_r0       */ "Org. Mundial Salut",
    /* who_r1       */ "recomana nivell",
    /* who_pm25_r2  */ "PM2.5 segur:",
    /* who_no2_r2   */ "NO2 segur:",
    /* who_o3_r2    */ "Oz\x02 segur:",           // Ozó

    /* gate_o3_name */ "Oz\x02",                  // Ozó
    /* gate_next_r2 */ "Prem el bot\x02",         // botó
    /* gate_next_r3 */ "per al seg\x05" "ent",    // següent  (\x05 = ü, CGRAM slot 5)
    /* gate_last_r2 */ "Prem el bot\x02",
    /* gate_last_r3 */ "per batallar!",

    /* waiting_r1 */ "Carregant seg\x05" "ent",   // següent  (\x05 = ü, CGRAM slot 5)
    /* waiting_r2 */ "contaminant...",

    /* countdown_label */ "BATALLA EN",

    /* defeat_title */ " POL\x03LUCI\x02 GUANYA!",

    /* score_o3_label    */ "  OZ\x02:",           // \u201c  OZÓ:” — 6 display chars
    /* gate_today_prefix */ "Avui: ",
    /* score_header      */ "    Ara   |  Meta   ",

    /* victory_r0        */ "Felicitacions!",
    /* victory_r1        */ "Defensor de l'Aire!",
    /* victory_city_sfx  */ " \x01s neta!",        // és neta!
    /* victory_r3        */ "Tria seg. ciutat >",

    /* defeat_city_sfx */ " ha caigut!",
    /* defeat_r2       */ "Tria altra ciutat",
    /* defeat_r3       */ "o intenta-ho.",

    /* attract_title   */ "ESCULL CIUTAT I",
    /* attract_tagline */ "SALVA-LA",
    /* attract_prompt  */ "PREM QUALSEVOL BOT\x02",
};

// Lookup table — index with LANG_EN / LANG_ES / LANG_CA
static const GameStrings* const LANGUAGE_TABLE[3] = {
    &STRINGS_EN,
    &STRINGS_ES,
    &STRINGS_CA,
};

#endif // STRINGS_H
