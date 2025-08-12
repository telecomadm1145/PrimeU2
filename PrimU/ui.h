#pragma once
#include "common.h"

/**
 * @brief List of available keycodes.
 * @details Keycodes starting with `KEY_PRIME_` are extended keycodes exclusive to HP Prime G1 (EA656).
 */
enum keycode_e {
	/* 0x00 */
	KEY_ESC = 0x01,
	KEY_LEFT,
	KEY_UP,
	KEY_RIGHT,
	KEY_DOWN,
	KEY_PGUP,
	KEY_PGDN,
	KEY_CAPS = 0x0a,
	KEY_DEL = 0xc,
	KEY_ENTER = 0x0d,
	/* 0x10 */
	/**
	 * @brief Bring up the function menu of the current scene.
	 * @details Not to be confused with the function menu.
	 */
	KEY_MENU = 0x11,
	/**
	 * @brief Bring up the function menu of the current scene.
	 * @details Alias of #KEY_MENU.
	 */
	KEY_FUNC_MENU = 0x11,
	/**
	 * @brief Change font size.
	 */
	KEY_FONT = 0x12,
	/* 0x20 */
	KEY_SPACE = ' ',
	KEY_EXCL = '!',
	KEY_TAB = 0x22,
	KEY_HASH = '#',
	KEY_DOLLAR = '$',
	KEY_PERCENT = '%',
	KEY_LPAREN = '(',
	KEY_RPAREN = ')',
	KEY_STAR = '*',
	KEY_COMMA = ',',
	KEY_DASH = '-',
	KEY_DOT = '.',
	/* 0x30 */
	KEY_0 = '0',
	KEY_1 = '1',
	KEY_2 = '2',
	KEY_3 = '3',
	KEY_4 = '4',
	KEY_5 = '5',
	KEY_6 = '6',
	KEY_7 = '7',
	KEY_8 = '8',
	KEY_9 = '9',
	KEY_QUESTION = '?',
	/* 0x40 */
	KEY_AT = '@',
	KEY_A = 'A',
	KEY_B = 'B',
	KEY_C = 'C',
	KEY_D = 'D',
	KEY_E = 'E',
	KEY_F = 'F',
	KEY_G = 'G',
	KEY_H = 'H',
	KEY_I = 'I',
	KEY_J = 'J',
	KEY_K = 'K',
	KEY_L = 'L',
	KEY_M = 'M',
	KEY_N = 'N',
	KEY_O = 'O',
	/* 0x50 */
	KEY_P = 'P',
	KEY_Q = 'Q',
	KEY_R = 'R',
	KEY_S = 'S',
	KEY_T = 'T',
	KEY_U = 'U',
	KEY_V = 'V',
	KEY_W = 'W',
	KEY_X = 'X',
	KEY_Y = 'Y',
	KEY_Z = 'Z',
	/* 0x80 */
	/**
	 * @brief Trigger TTS in Mandarain Chinese.
	 */
	KEY_LANG_CHN = 0x80,
	/**
	 * @brief Trigger TTS in Cantonese.
	 */
	KEY_LANG_YUE = 0x81,
	/**
	 * @brief Trigger TTS in English.
	 */
	KEY_LANG_ENG = 0x82,
	/**
	 * @brief Power button event.
	 */
	KEY_POWER = 0x83,
	KEY_F1 = 0x84,
	KEY_F2,
	KEY_F3,
	KEY_F4,
	KEY_F5,
	/**
	 * @brief Launch voice recorder.
	 */
	KEY_APP_REC = 0x89,
	KEY_SHIFT = 0x8b,
	/**
	 * @brief Switch input method.
	 */
	KEY_IME = 0x8e,
	/**
	 * @brief Trigger TTS in Japanese.
	 */
	KEY_LANG_JPN = 0x8f,
	/* 0x90 */
	/**
	 * @brief Activate the symbol selection input method.
	 */
	KEY_SYMBOL = 0x91,
	/**
	 * @brief Return to home menu.
	 */
	KEY_HOME = 0x93,
	/**
	 * @brief Return to home menu.
	 * @details Alias of #KEY_HOME
	 */
	KEY_HOME_MENU = 0x93,
	/**
	 * @brief The MENU key on HP Prime.
	 * @details Alias of #KEY_HOME
	 */
	KEY_PRIME_MENU = 0x93,
	/**
	 * @brief Toggle simplified/traditional Chinese.
	 */
	KEY_TOGGLE_SC_TC = 0x94,
	/**
	 * @brief Show in-app help.
	 */
	KEY_HELP = 0x95,
	/**
	 * @brief Save current change and exit.
	 */
	KEY_SAVE = 0x96,
	/**
	 * @brief Launch MP3 player.
	 */
	KEY_APP_MP3_PLAYER = 0x98,
	/**
	 * @brief Open the Volume + Backlight adjustment menu.
	 */
	KEY_VOL_BACKLIGHT = 0x9c,
	/**
	 * @brief Dictionary: Pronounce the current word syllable-by-syllable.
	 */
	KEY_SYLLABLE = 0x9e,
	/**
	 * @brief Dictionary and search: Go back to the first screen the user initiated a search (with input memorized).
	 */
	KEY_ORIGINAL_INPUT = 0x9f,
	/* 0xa0 */
	/**
	 * @brief Board-specific app launching shortcut.
	 */
	KEY_APP_MISC_3 = 0xa3,
	/**
	 * @brief Launch the flashcard app.
	 * @details Available on CA743.
	 */
	KEY_APP_FLASHCARD = 0xa3,
	/**
	 * @brief Board-specific app launching shortcut.
	 */
	KEY_APP_MISC_5 = 0xa5,
	/**
	 * @brief Launch Daijirin Japanese dictionary.
	 * @details Available on JA738.
	 */
	KEY_APP_DICT_JA_DAJIRIN = 0xa5,
	/**
	 * @brief Launch the Games app.
	 * @details Available on CA743.
	 */
	KEY_APP_GAMES = 0xa5,
	/**
	 * @brief Board-specific app launching shortcut.
	 */
	KEY_APP_MISC_6 = 0xa6,
	/**
	 * @brief Launch Shin-Meikai Japanese dictionary.
	 * @details Available on JA738.
	 */
	KEY_APP_DICT_JA_SHINMEIKAI = 0xa6,
	/**
	 * @brief Launch the album app.
	 * @details Available on CA743.
	 */
	KEY_APP_ALBUM = 0xa6,
	/**
	 * @brief Board-specific app launching shortcut.
	 */
	KEY_APP_MISC_7 = 0xa7,
	/**
	 * @brief Launch Longman dictionary.
	 * @details Available on CA743.
	 */
	KEY_APP_DICT_EN_LONGMAN = 0xa7,
	/**
	 * @brief Launch Genius Japanese-English dictionary.
	 * @details Available on JA738.
	 */
	KEY_APP_DICT_JA_EN_GENIUS = 0xa7,
	/**
	 * @brief Board-specific app launching shortcut.
	 */
	KEY_APP_MISC_8 = 0xa8,
	/**
	 * @brief Launch Genius English-Japanese dictionary.
	 * @details Available on JA738.
	 */
	KEY_APP_DICT_EN_JA_GENIUS = 0xa8,
	/* 0xb0 */
	KEY_PRIME_APPS = 0xb1,
	KEY_PRIME_PLOT,
	KEY_PRIME_NUM,
	KEY_PRIME_VIEW,
	KEY_PRIME_CAS,
	KEY_PRIME_ALPHA,
	KEY_PRIME_MINUS,
	KEY_PRIME_DECIMAL_POINT,
	KEY_PRIME_PLUS,
	/* 0xc0 */
	/**
	 * @brief Show the Favorites menu.
	 */
	KEY_FAV = 0xc0,
	/* 0xd0 */
	/**
	 * @brief Launch video player app.
	 */
	KEY_APP_VIDEO_PLAYER = 0xd0,
	/**
	 * @brief Launch Besta professional vocabulary dictionary.
	 */
	KEY_APP_DICT_EN_PRO = 0xd4,
	/* 0xe0 */
	/**
	 * @brief Launch Besta Chinese-English dictionary.
	 */
	KEY_APP_DICT_ZH_EN_BESTA = 0xe8,
	/**
	 * @brief Launch Besta Japanese-Chinese dictionary.
	 */
	KEY_APP_DICT_JA_ZH_BESTA = 0xe9,
	/**
	 * @brief Launch Besta Chinese-Japanese dictionary.
	 */
	KEY_APP_DICT_ZH_JA_BESTA = 0xea,
	/* 0xf0 */
	/**
	 * @brief Launch Oxford dictionary.
	 */
	KEY_APP_DICT_EN_OXFORD = 0xf2,
	/**
	 * @brief Launch Besta English-Chinese dictionary.
	 */
	KEY_APP_DICT_EN_ZH_BESTA = 0xf3,
	/**
	 * @brief Show the edit menu (cut/copy/paste).
	 */
	KEY_EDIT = 0xf5,
	/**
	 * @brief Launch the unified search app.
	 */
	KEY_SEARCH = 0xf6,
	/**
	 * @brief Open the Backlight adjustment menu.
	 * @details May be an alias to #KEY_VOL_BACKLIGHT.
	 */
	KEY_BACKLIGHT = 0xf8,
	/**
	 * @brief Dictionary: Parrot the current word and compare the pronunciations.
	 */
	KEY_COMPARE = 0xf9,
	/**
	 * @brief Open the Volume adjustment menu.
	 * @details May be an alias to #KEY_VOL_BACKLIGHT.
	 */
	KEY_VOL = 0xfa,
	/**
	 * @brief Display battery indicator popup.
	 */
	KEY_BATTERY = 0xfb,
	/**
	 * @brief Event on USB cable insertion.
	 */
	KEY_USB_INSERTION = 0xfe,
	/**
	 * @brief Dictionary: Repeatedly read out the current word.
	 */
	KEY_REPEAT = 0xff,
	/* 0x100+ */
	KEY_INS = 0x101,
	KEY_SHIFT_PGUP = 0xe047,
	KEY_PRIME_HOME = 0xe047,
	KEY_SHIFT_PGDN = 0xe04f,
};



/**
 * @brief UI event types.
 */
enum ui_event_type_e : int {
	/**
	 * @brief Invalid/cleared.
	 */
	UI_EVENT_TYPE_INVALID = 0,
	/**
	 * @brief Beginning of touch/pen down event.
	 */
	UI_EVENT_TYPE_TOUCH_BEGIN = 1,
	/**
	 * @brief Touch/pen move event.
	 */
	UI_EVENT_TYPE_TOUCH_MOVE = 2,
	/**
	 * @brief End of touch/pen up event.
	 */
	UI_EVENT_TYPE_TOUCH_END = 8,

	UI_EVENT_TYPE_TICK = 0xf,

	UI_EVENT_TYPE_TICK_2 = 0x100010,
	
	UI_EVENT_TYPE_REDRAW = 0x4000,
	/**
	 * @brief Key(s) pressed.
	 */
	UI_EVENT_TYPE_KEY = 16,
	/**
	 * @brief Key(s) released.
	 * @details Available on S3C and TCC boards.
	 */
	UI_EVENT_TYPE_KEY_UP = 0x100000,
};


/**
 * @brief Multipress/multitouch event.
 * @details This is a simplified version of the main UI event struct, that only contains the necessary fields to
 * represent a multitouch or a key-press event. Used on Prime G1.
 */
struct UIMultipressEvent {
	/**
	 * @brief Type of event.
	 * @see ui_event_type_e List of event types.
	 */
	unsigned int type;
	/**
	 * @brief Finger ID of a touch event.
	 */
	unsigned short finger_id;
	union {
		struct {
			/**
			 * @brief Keycode for the first pressed key.
			 */
			unsigned short key_code0;
			/**
			 * @brief Keycode for the second pressed key (maybe unused).
			 */
			unsigned short key_code1;
		};
		struct {
			/**
			 * @brief The X coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_x;
			/**
			 * @brief The Y coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_y;
		};
	};
	/**
	 * @brief Unknown. Maybe unused and probably padding.
	 */
	unsigned short unk_0xb;
};

/**
 * @brief Structure for low level UI events (Prime G1 extension).
 * @details Define `MUTEKI_HAS_PRIME_UI_EVENT` as 1 to make this the underlying type of ui_event_t.
 */
struct ui_event_prime_s {
	/**
	 * @brief Event recipient.
	 * @details If set to `NULL`, the event is a broadcast event (e.g. input event). Otherwise, the
	 * widget's ui_component_t::on_event callback will be called with this event.
	 */
	VirtPtr2<void> recipient; // 0-4
	/**
	 * @brief The type of event (0x10 being key event)
	 * @see ui_event_type_e List of event types.
	 */
	ui_event_type_e event_type; // 4-8 16: key (?).
	union {
		struct {
			/**
			 * @brief Keycode for the first pressed key.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_KEY.
			 */
			unsigned short key_code0; // 8-10
			/**
			 * @brief Keycode for the second pressed key.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_KEY.
			 * @note Depending on the exact keys pressed simultaneously, this is not always accurate. Moreover,
			 * some devices may lack support of simultaneous key presses.
			 */
			unsigned short key_code1; // 10-12 sometimes set when 2 keys are pressed simultaneously. Does not always work.
		};
		struct {
			/**
			 * @brief The X coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_x;
			/**
			 * @brief The Y coordinate of where the touch event is located, in pixels.
			 * @details Only available when ::event_type is ::UI_EVENT_TYPE_TOUCH_BEGIN,
			 * ::UI_EVENT_TYPE_TOUCH_MOVE, or ::UI_EVENT_TYPE_TOUCH_END.
			 */
			unsigned short touch_y;
		};
	};
	/**
	 * @brief Unknown.
	 * @details Set along with a ::KEY_USB_INSERTION event. Seems to point to some data. Exact purpose unknown.
	 */
	VirtPtr usb_data; // 12-16 pointer that only shows up on USB insertion event.
	/**
	 * @brief Unknown.
	 * @details Maybe used on event types other than touch and key press.
	 */
	VirtPtr unk16; // 16-20 sometimes a pointer especially on unstable USB connection? junk data?
	/**
	 * @brief Unknown.
	 * @details Seems to be always 0, although ClearEvent() explicitly sets this to 0. Maybe used on event types other
	 * than touch and key press.
	 */
	VirtPtr unk20; // 20-24 seems to be always 0. Unused?
	/**
	 * @brief Number of valid multipress events available for processing.
	 */
	unsigned short available_multipress_events; // 24-26
	/**
	 * @brief Unknown. Sometimes can be 0x2 on startup.
	 */
	unsigned short unk_0x1a; // 26-28
	/**
	 * @brief The multipress events.
	 */
	UIMultipressEvent multipress_events[8]; // 28-124
};

