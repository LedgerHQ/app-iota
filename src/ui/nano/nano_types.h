#ifndef NANO_TYPES_H
#define NANO_TYPES_H

#include <stdint.h>
#include "os_io_seproxyhal.h"
#include "iota/bundle.h"

/// length of one text line
#define TEXT_LEN 21
#define TOTAL_GLYPHS GLYPH_NONE

#define BUTTON_L 0
#define BUTTON_R 1
#define BUTTON_B 2

#define BUTTON_BAD 255

// TODO rename something better?
// Different positions a text can have
typedef enum { TOP_H, TOP, MID, BOT, BOT_H, POS_X } UI_TEXT_POS;

#ifdef TARGET_NANOS

#define UI_SCREENS_NANO UI_SCREENS_NANOS

// UI SCREENS
typedef enum {
    SCREEN_TITLE,
    SCREEN_TITLE_BOLD,
    SCREEN_MENU,
    SCREEN_IOTA,
    SCREEN_BACK
} UI_SCREENS_NANOS;

// GLYPH TYPES
typedef enum {
    GLYPH_CONFIRM,
    GLYPH_UP,
    GLYPH_DOWN,
    GLYPH_DASH,
    GLYPH_LOAD,
    GLYPH_NONE, // glyphs after none require special screens
    GLYPH_IOTA,
    GLYPH_BACK
} UI_GLYPH_TYPES_NANOS;

#else // TARGET_NANOS/X
// TODO rename NANOS to NANO (separated into #defines)
#define UI_SCREENS_NANO UI_SCREENS_NANOX

// TODO Sub this in
#define bagl_icon_sz sizeof(bagl_icon_details_s)

// UI SCREENS
typedef enum {
    SCREEN_TITLE,
    SCREEN_ICON,
    SCREEN_ICON_MULTI,
    SCREEN_BIP,
    SCREEN_ADDR
} UI_SCREENS_NANOX;

// cover warnings from other files that don't pertain to NANOX
// TODO leave/fix?
#define UI_GLYPH_TYPES_NANOS UI_GLYPH_TYPES_NANOX

#define GLYPH_IOTA_FLAG 1 << GLYPH_IOTA
#define GLYPH_BACK_FLAG 1 << GLYPH_BACK
#define GLYPH_DASH_FLAG 1 << GLYPH_DASH
#define GLYPH_INFO_FLAG 1 << GLYPH_INFO
#define GLYPH_LOAD_FLAG 1 << GLYPH_LOAD
#define GLYPH_APPROVE_FLAG 1 << GLYPH_APPROVE
#define GLYPH_DENY_FLAG 1 << GLYPH_DENY
#define GLYPH_UP_FLAG 1 << GLYPH_UP
#define GLYPH_DOWN_FLAG 1 << GLYPH_DOWN
#define GLYPH_CONFIRM_FLAG 1 << GLYPH_CONFIRM

// GLYPH TYPES
typedef enum {
    GLYPH_IOTA,
    GLYPH_BACK,
    GLYPH_DASH,
    GLYPH_INFO,
    GLYPH_LOAD,
    GLYPH_CHECK,
    GLYPH_CROSS,
    GLYPH_UP,   // maps to left
    GLYPH_DOWN, // maps to right
    GLYPH_CONFIRM,
    GLYPH_NONE
} UI_GLYPH_TYPES_NANOX;

// TODO group better?
typedef enum {
    EL_IOTA,
    EL_BACK,
    EL_DASH,
    EL_INFO,
    EL_LOAD,
    EL_CHECK,
    EL_CROSS,
    EL_UP,   // maps to left
    EL_DOWN, // maps to right
    EL_CONFIRM,
    EL_NONE,
    EL_TITLE,
    EL_BIP,
    EL_ADDR,
    EL_ICON,
    EL_ICON_MULTI,
    EL_CLEAR
} UI_EL_USERIDS_X;

// TODO use GLYPH_NUM instead of GLYPH_NONE for sz
#define GLYPH_NUM GLYPH_NONE

#endif // TARGET_NANOS/X

// Size of Menu
#define MENU_ADDR_LEN 8
#define MENU_MORE_INFO_LEN 3

#ifdef TARGET_NANOS
#define MENU_ADDR_LAST MENU_ADDR_LEN - 1
#define MENU_BIP_LAST 1
#else
#define MENU_ADDR_LAST 1 // X screen addr broken up into 2
#define MENU_BIP_LAST 0
#endif

#define MENU_TX_APPROVE tx_array_sz - 2
#define MENU_TX_DENY tx_array_sz - 1

// UI STATES
typedef enum {
    STATE_MAIN_MENU,
    STATE_IGNORE,
    STATE_ABOUT,
    STATE_VERSION,
    STATE_MORE_INFO,
    STATE_DISP_ADDR_CHK, // Abbreviated address with Checksum
    STATE_DISP_ADDR,     // Host displays pubkey on ledger
    STATE_TX_ADDR,       // Display full address in TX
    STATE_PROMPT_TX,
    STATE_BIP_PATH,
    STATE_EXIT = 255
} UI_STATES_NANO;

// Main menu entries
typedef enum {
    MENU_MAIN_IOTA,
    MENU_MAIN_ABOUT,
    MENU_MAIN_EXIT,
    MENU_MAIN_LEN
} MENU_MAIN_ENTRIES;

// About menu entries
typedef enum {
    MENU_ABOUT_VERSION,
    MENU_ABOUT_MORE_INFO,
    MENU_ABOUT_BACK,
    MENU_ABOUT_LEN
} MENU_ABOUT_ENTRIES;

typedef struct UI_TEXT_CTX_NANO {

    char top_str[TEXT_LEN];
    char mid_str[TEXT_LEN];
    char bot_str[TEXT_LEN];
#ifdef TARGET_NANOX
    char x_str[TEXT_LEN];
#endif

} UI_TEXT_CTX_NANO;

typedef struct UI_GLYPH_CTX_NANO {

    // flags for turning on/off certain glyphs
    char glyph[TOTAL_GLYPHS + 1];

} UI_GLYPH_CTX_NANO;

typedef struct UI_STATE_CTX_NANO {

    // tx information
    int64_t val;
    bool display_full_value;

    char addr[90];

    uint8_t state;
    uint8_t menu_idx;

    uint8_t backup_state;
    uint8_t backup_menu_idx;

    // TODO figure out why I can't remove glyphs, or move glyphX into glyph
#ifdef TARGET_NANOX
    // flag for which glyphs are shown
    unsigned int glyphs;

    char glyphX[GLYPH_NONE][20];
#endif

} UI_STATE_CTX_NANO;

extern UI_TEXT_CTX_NANO ui_text;
extern UI_GLYPH_CTX_NANO ui_glyphs;
extern UI_STATE_CTX_NANO ui_state;

#endif // NANO_TYPES_H
