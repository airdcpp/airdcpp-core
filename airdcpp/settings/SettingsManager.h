﻿/*
 * Copyright (C) 20 01-2024 Jacek Sieka, arnetheduck on gmailpoint com
 *
 * This program is free software; you can redistribute it and/or modif
 * it under the terms of the GU Genera Public License as ubished by
 * he FreeSoftare Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef DCPLUSPLUS_DCPP_SETTINGS_MANAGER_H
#define DCPLUSPLUS_DCPP_SETTINGS_MANAGER_H

#include <airdcpp/util/AppUtil.h>
#include <airdcpp/settings/SettingItem.h>
#include <airdcpp/settings/SettingsManagerListener.h>
#include <airdcpp/core/Singleton.h>
#include <airdcpp/core/Speaker.h>
#include <airdcpp/core/version.h>

namespace dcpp {

struct HubSettings;

// Shouldn't really be in core...
enum class ToolbarIconEnum {
	DIVIDER = -1,
	PUBLIC_HUBS,
	RECONNECT,
	FOLLOW_REDIRECT,
	FAVORITE_HUBS,
	USERS,
	RECENT_HUBS,
	QUEUE,
	UPLOAD_QUEUE,
	FINISHED_UPLOADS,
	SEARCH,
	ADL_SEARCH,
	SEARCH_SPY,
	AUTO_SEARCH,
	NOTEPAD,
	SYSTEM_LOG,
	REFRESH_FILELIST,
	EXTENSIONS,
	OPEN_FILELIST,
	OPEN_DOWNLOADS,
	AWAY,
	SETTINGS,
	RSS,
	LAST,
};

#ifdef _WIN32
#define HAVE_GUI 1
#endif

class SettingsManager : public Singleton<SettingsManager>, public Speaker<SettingsManagerListener>
{
public:
	static StringList connectionSpeeds;

	enum StrSetting { STR_FIRST,
		// Generic
		NICK = STR_FIRST, UPLOAD_SPEED, DOWNLOAD_SPEED, DESCRIPTION, DOWNLOAD_DIRECTORY, EMAIL, EXTERNAL_IP, EXTERNAL_IP6,
		LANGUAGE_FILE, HUBLIST_SERVERS, HTTP_PROXY, MAPPER,
		BIND_ADDRESS, BIND_ADDRESS6, SOCKS_SERVER, SOCKS_USER, SOCKS_PASSWORD, CONFIG_VERSION, CONFIG_APP,
		DEFAULT_AWAY_MESSAGE, TIME_STAMPS_FORMAT, PRIVATE_ID, NMDC_ENCODING,

		LOG_DIRECTORY, LOG_FORMAT_POST_DOWNLOAD, LOG_FORMAT_POST_UPLOAD, LOG_FORMAT_MAIN_CHAT, LOG_FORMAT_PRIVATE_CHAT,
 		LOG_FILE_MAIN_CHAT, LOG_FILE_PRIVATE_CHAT, LOG_FILE_STATUS, LOG_FILE_UPLOAD, 
		LOG_FILE_DOWNLOAD, LOG_FILE_SYSTEM, LOG_FORMAT_SYSTEM, LOG_FORMAT_STATUS, 
		TLS_PRIVATE_KEY_FILE, TLS_CERTIFICATE_FILE, TLS_TRUSTED_CERTIFICATES_PATH, 
		COUNTRY_FORMAT, DATE_FORMAT, SKIPLIST_SHARE, FREE_SLOTS_EXTENSIONS, SKIPLIST_DOWNLOAD, HIGH_PRIO_FILES,
		AS_FAILED_DEFAULT_GROUP,

#ifdef HAVE_GUI
		// Windows GUI
		TEXT_FONT, TRANSFERVIEW_ORDER, TRANSFERVIEW_WIDTHS, HUBFRAME_ORDER, HUBFRAME_WIDTHS,
		SEARCHFRAME_ORDER, SEARCHFRAME_WIDTHS, FAVORITESFRAME_ORDER, FAVORITESFRAME_WIDTHS,
		QUEUEFRAME_ORDER, QUEUEFRAME_WIDTHS, PUBLICHUBSFRAME_ORDER, PUBLICHUBSFRAME_WIDTHS,
		USERS_FRAME_ORDER, USERS_FRAME_WIDTHS, FINISHED_ORDER, FINISHED_WIDTHS, ADLSEARCHFRAME_ORDER, ADLSEARCHFRAME_WIDTHS,
		FINISHED_UL_WIDTHS, FINISHED_UL_ORDER, SPYFRAME_WIDTHS, SPYFRAME_ORDER,
		FINISHED_VISIBLE, FINISHED_UL_VISIBLE, DIRECTORYLISTINGFRAME_VISIBLE,
		RECENTFRAME_ORDER, RECENTFRAME_WIDTHS, DIRECTORYLISTINGFRAME_ORDER, DIRECTORYLISTINGFRAME_WIDTHS,
		MAINFRAME_VISIBLE, SEARCHFRAME_VISIBLE, QUEUEFRAME_VISIBLE, HUBFRAME_VISIBLE, UPLOADQUEUEFRAME_VISIBLE,
		EMOTICONS_FILE,

		BEEPFILE, BEGINFILE, FINISHFILE, SOURCEFILE, UPLOADFILE, CHATNAMEFILE, WINAMP_FORMAT,
		KICK_MSG_RECENT_01, KICK_MSG_RECENT_02, KICK_MSG_RECENT_03, KICK_MSG_RECENT_04, KICK_MSG_RECENT_05,
		KICK_MSG_RECENT_06, KICK_MSG_RECENT_07, KICK_MSG_RECENT_08, KICK_MSG_RECENT_09, KICK_MSG_RECENT_10,
		KICK_MSG_RECENT_11, KICK_MSG_RECENT_12, KICK_MSG_RECENT_13, KICK_MSG_RECENT_14, KICK_MSG_RECENT_15,
		KICK_MSG_RECENT_16, KICK_MSG_RECENT_17, KICK_MSG_RECENT_18, KICK_MSG_RECENT_19, KICK_MSG_RECENT_20,
		TOOLBAR_ORDER, UPLOADQUEUEFRAME_ORDER, UPLOADQUEUEFRAME_WIDTHS,
		SOUND_EXC, SOUND_HUBCON, SOUND_HUBDISCON, SOUND_FAVUSER, SOUND_TYPING_NOTIFY,

		BACKGROUND_IMAGE, MPLAYERC_FORMAT, ITUNES_FORMAT, WMP_FORMAT, SPOTIFY_FORMAT, WINAMP_PATH,
		POPUP_FONT, POPUP_TITLE_FONT, POPUPFILE, 
		MEDIATOOLBAR, PASSWORD, HIGHLIGHT_LIST, ICON_PATH,
		AUTOSEARCHFRAME_ORDER, AUTOSEARCHFRAME_WIDTHS, TOOLBAR_POS, TB_PROGRESS_FONT, LAST_SEARCH_FILETYPE, LAST_SEARCH_DISABLED_HUBS, LAST_AS_FILETYPE, LAST_SEARCH_EXCLUDED,
		USERS_FRAME_VISIBLE, LIST_VIEW_FONT, LAST_FL_FILETYPE, AUTOSEARCHFRAME_VISIBLE, RSSFRAME_ORDER, RSSFRAME_WIDTHS, RSSFRAME_VISIBLE,
#endif
		STR_LAST 
	};

	enum IntSetting { INT_FIRST = STR_LAST + 1,
		// Generic
		INCOMING_CONNECTIONS = INT_FIRST, INCOMING_CONNECTIONS6, TCP_PORT, UPLOAD_SLOTS,
		BUFFER_SIZE, DOWNLOAD_SLOTS, MAX_DOWNLOAD_SPEED, MIN_UPLOAD_SPEED, SOCKS_PORT,
		MAX_COMPRESSION, SET_MINISLOT_SIZE, SHUTDOWN_TIMEOUT, EXTRA_SLOTS, EXTRA_PARTIAL_SLOTS, EXTRA_DOWNLOAD_SLOTS,

		DISCONNECT_SPEED, DISCONNECT_FILE_SPEED, DISCONNECT_TIME, REMOVE_SPEED,  
		DISCONNECT_FILESIZE, NUMBER_OF_SEGMENTS, MAX_HASH_SPEED, MAX_PM_HISTORY_LINES, BUNDLE_SEARCH_TIME,
		MINIMUM_SEARCH_INTERVAL, MAX_AUTO_MATCH_SOURCES, 
		UDP_PORT, OUTGOING_CONNECTIONS, SOCKET_IN_BUFFER, SOCKET_OUT_BUFFER,
		AUTO_REFRESH_TIME, AUTO_SEARCH_LIMIT, MAX_COMMAND_LENGTH, TLS_PORT, DOWNCONN_PER_SEC,
		PRIO_HIGHEST_SIZE, PRIO_HIGH_SIZE, PRIO_NORMAL_SIZE, PRIO_LOW_SIZE, 

		BANDWIDTH_LIMIT_START, BANDWIDTH_LIMIT_END, MAX_DOWNLOAD_SPEED_ALTERNATE,
		MAX_UPLOAD_SPEED_ALTERNATE, MAX_DOWNLOAD_SPEED_MAIN, MAX_UPLOAD_SPEED_MAIN,
		SLOTS_ALTERNATE_LIMITING, SLOTS_PRIMARY,

		MAX_FILE_SIZE_SHARED, MIN_SEGMENT_SIZE, AUTO_SLOTS, INCOMING_REFRESH_TIME,
		CONFIG_BUILD_NUMBER, PM_MESSAGE_CACHE, HUB_MESSAGE_CACHE, LOG_MESSAGE_CACHE, MAX_RECENT_HUBS, MAX_RECENT_PRIVATE_CHATS, MAX_RECENT_FILELISTS,

		FAV_DL_SPEED, SETTINGS_PROFILE, LOG_LINES, MAX_MCN_DOWNLOADS, MAX_MCN_UPLOADS,
		RECENT_BUNDLE_HOURS, DISCONNECT_MIN_SOURCES, AUTOPRIO_TYPE, AUTOPRIO_INTERVAL, AUTOSEARCH_EXPIRE_DAYS, TLS_MODE, UPDATE_METHOD,

		FULL_LIST_DL_LIMIT, LAST_LIST_PROFILE, MAX_HASHING_THREADS, HASHERS_PER_VOLUME, SKIP_SUBTRACT, BLOOM_MODE, AWAY_IDLE_TIME,
		HISTORY_SEARCH_MAX, HISTORY_DIR_MAX, HISTORY_EXCLUDE_MAX, MIN_DUPE_CHECK_SIZE, DB_CACHE_SIZE, DL_AUTO_DISCONNECT_MODE, 
		CUR_REMOVED_TREES, CUR_REMOVED_FILES, REFRESH_THREADING,
		MAX_RUNNING_BUNDLES, DEFAULT_SP, UPDATE_CHANNEL,

		AUTOSEARCH_EVERY, AS_DELAY_HOURS,

#ifdef HAVE_GUI
		// Windows GUI
		BACKGROUND_COLOR, TEXT_COLOR, MAIN_WINDOW_STATE,
		MAIN_WINDOW_SIZE_X, MAIN_WINDOW_SIZE_Y, MAIN_WINDOW_POS_X, MAIN_WINDOW_POS_Y, MAX_TAB_ROWS,
		DOWNLOAD_BAR_COLOR, UPLOAD_BAR_COLOR, MENUBAR_LEFT_COLOR, MENUBAR_RIGHT_COLOR, SEARCH_ALTERNATE_COLOUR,
		RESERVED_SLOT_COLOR, IGNORED_COLOR, FAVORITE_COLOR, NORMAL_COLOUR,
		PASIVE_COLOR, OP_COLOR, PROGRESS_BACK_COLOR, PROGRESS_SEGMENT_COLOR, COLOR_DONE,

		MAGNET_ACTION, POPUP_TYPE, SHUTDOWN_ACTION,
		USERLIST_DBLCLICK, TRANSFERLIST_DBLCLICK, CHAT_DBLCLICK,

		TEXT_GENERAL_BACK_COLOR, TEXT_GENERAL_FORE_COLOR,
		TEXT_MYOWN_BACK_COLOR, TEXT_MYOWN_FORE_COLOR,
		TEXT_PRIVATE_BACK_COLOR, TEXT_PRIVATE_FORE_COLOR,
		TEXT_SYSTEM_BACK_COLOR, TEXT_SYSTEM_FORE_COLOR,
		TEXT_SERVER_BACK_COLOR, TEXT_SERVER_FORE_COLOR,
		TEXT_TIMESTAMP_BACK_COLOR, TEXT_TIMESTAMP_FORE_COLOR,
		TEXT_MYNICK_BACK_COLOR, TEXT_MYNICK_FORE_COLOR,
		TEXT_FAV_BACK_COLOR, TEXT_FAV_FORE_COLOR,
		TEXT_OP_BACK_COLOR, TEXT_OP_FORE_COLOR,
		TEXT_URL_BACK_COLOR, TEXT_URL_FORE_COLOR,
		PROGRESS_3DDEPTH,
		PROGRESS_TEXT_COLOR_DOWN, PROGRESS_TEXT_COLOR_UP, ERROR_COLOR, TRANSFER_SPLIT_SIZE,

		TAB_ACTIVE_BG, TAB_ACTIVE_TEXT, TAB_ACTIVE_BORDER, TAB_INACTIVE_BG, TAB_INACTIVE_BG_DISCONNECTED, TAB_INACTIVE_TEXT, 
		TAB_INACTIVE_BORDER, TAB_INACTIVE_BG_NOTIFY, TAB_DIRTY_BLEND, TAB_SIZE, MEDIA_PLAYER,
		POPUP_TIME, MAX_MSG_LENGTH, POPUP_BACKCOLOR, POPUP_TEXTCOLOR, POPUP_TITLE_TEXTCOLOR, 
		TB_IMAGE_SIZE, TB_IMAGE_SIZE_HOT, MAX_RESIZE_LINES,
		SHARE_DUPE_COLOR, TEXT_SHARE_DUPE_BACK_COLOR, 
		TEXT_NORM_BACK_COLOR, TEXT_NORM_FORE_COLOR, 
		FAV_TOP, FAV_BOTTOM, FAV_LEFT, FAV_RIGHT, SYSLOG_TOP, SYSLOG_BOTTOM, SYSLOG_LEFT, SYSLOG_RIGHT, NOTEPAD_TOP, NOTEPAD_BOTTOM,
		NOTEPAD_LEFT, NOTEPAD_RIGHT, QUEUE_TOP, QUEUE_BOTTOM, QUEUE_LEFT, QUEUE_RIGHT, SEARCH_TOP, SEARCH_BOTTOM, SEARCH_LEFT, SEARCH_RIGHT, USERS_TOP, USERS_BOTTOM,
		USERS_LEFT, USERS_RIGHT, FINISHED_TOP, FINISHED_BOTTOM, FINISHED_LEFT, FINISHED_RIGHT, TEXT_TOP, TEXT_BOTTOM, TEXT_LEFT, TEXT_RIGHT, DIRLIST_TOP, DIRLIST_BOTTOM,
		DIRLIST_LEFT, DIRLIST_RIGHT, STATS_TOP, STATS_BOTTOM, STATS_LEFT, STATS_RIGHT, 

		LIST_HL_BG_COLOR, LIST_HL_COLOR, QUEUE_DUPE_COLOR, TEXT_QUEUE_DUPE_BACK_COLOR, QUEUE_SPLITTER_POS,
		WTB_IMAGE_SIZE, TB_PROGRESS_TEXT_COLOR,
		COLOR_STATUS_FINISHED, COLOR_STATUS_SHARED, PROGRESS_LIGHTEN, FAV_USERS_SPLITTER_POS,
#endif
		INT_LAST 
	};

	enum BoolSetting { BOOL_FIRST = INT_LAST + 1,
		// Generic
		ADLS_BREAK_ON_FIRST = BOOL_FIRST,
		ALLOW_UNTRUSTED_CLIENTS, ALLOW_UNTRUSTED_HUBS,
		AUTO_DETECT_CONNECTION, AUTO_DETECT_CONNECTION6, AUTO_FOLLOW, AUTO_KICK, AUTO_KICK_NO_FAVS, AUTO_SEARCH,
		COMPRESS_TRANSFERS,

		DONT_DL_ALREADY_QUEUED, DONT_DL_ALREADY_SHARED, FAV_SHOW_JOINS, FILTER_MESSAGES,
		GET_USER_COUNTRY, GET_USER_INFO, HUB_USER_COMMANDS, KEEP_LISTS,
		LOG_DOWNLOADS, LOG_FILELIST_TRANSFERS, LOG_FINISHED_DOWNLOADS, LOG_MAIN_CHAT,
		LOG_PRIVATE_CHAT, LOG_STATUS_MESSAGES, LOG_SYSTEM, LOG_UPLOADS,

		SOCKS_RESOLVE, NO_AWAYMSG_TO_BOTS, NO_IP_OVERRIDE, PRIO_LOWEST, SHARE_HIDDEN, SHOW_JOINS,
		TIME_DEPENDENT_THROTTLE, TIME_STAMPS,
		SEARCH_PASSIVE, REMOVE_FORBIDDEN, MULTI_CHUNK, AWAY,
		
		SEGMENTS_MANUAL, REPORT_ALTERNATES, AUTO_PRIORITY_DEFAULT,

		AUTO_DETECTION_USE_LIMITED, LOG_SCHEDULED_REFRESHES, AUTO_COMPLETE_BUNDLES,
		ENABLE_SUDP, NMDC_MAGNET_WARN, UPDATE_IP_HOURLY,
		USE_SLOW_DISCONNECTING_DEFAULT, PRIO_LIST_HIGHEST,
		QI_AUTOPRIO, REPORT_ADDED_SOURCES, OVERLAP_SLOW_SOURCES, FORMAT_DIR_REMOTE_TIME,
		LOG_HASHING, USE_PARTIAL_SHARING,
		REPORT_BLOCKED_SHARE, MCN_AUTODETECT, DL_AUTODETECT, UL_AUTODETECT,
		DUPES_IN_FILELIST, DUPES_IN_CHAT, NO_ZERO_BYTE,

		SYSTEM_SHOW_UPLOADS, SYSTEM_SHOW_DOWNLOADS, WIZARD_PENDING, FORMAT_RELEASE,
		USE_ADLS, DUPE_SEARCH, DISALLOW_CONNECTION_TO_PASSED_HUBS, AUTO_ADD_SOURCE,
		SHARE_SKIPLIST_USE_REGEXP, DOWNLOAD_SKIPLIST_USE_REGEXP, HIGHEST_PRIORITY_USE_REGEXP, USE_HIGHLIGHT,
		IP_UPDATE,

		IGNORE_USE_REGEXP_OR_WC, ALLOW_MATCH_FULL_LIST, SHOW_CHAT_NOTIFY, FREE_SPACE_WARN,
		HISTORY_SEARCH_CLEAR, HISTORY_EXCLUDE_CLEAR, HISTORY_DIR_CLEAR, NO_IP_OVERRIDE6, IP_UPDATE6,
		SKIP_EMPTY_DIRS_SHARE, REMOVE_EXPIRED_AS, PM_LOG_GROUP_CID, SHARE_FOLLOW_SYMLINKS, USE_DEFAULT_CERT_PATHS, STARTUP_REFRESH,
		FL_REPORT_FILE_DUPES, USE_UPLOAD_BUNDLES, LOG_IGNORED, REMOVE_FINISHED_BUNDLES, ALWAYS_CCPM,

		POPUP_BOT_PMS, POPUP_HUB_PMS, SORT_FAVUSERS_FIRST,
#ifdef HAVE_GUI
		// Windows GUI
		BOLD_FINISHED_DOWNLOADS, BOLD_FINISHED_UPLOADS, BOLD_HUB, BOLD_PM,
		BOLD_QUEUE, BOLD_SEARCH, BOLD_SYSTEM_LOG, CLEAR_SEARCH, FREE_SLOTS_DEFAULT,
		CONFIRM_ADLS_REMOVAL, CONFIRM_EXIT, CONFIRM_HUB_REMOVAL, CONFIRM_USER_REMOVAL,

		MAGNET_ASK, MAGNET_REGISTER, MINIMIZE_TRAY,
		POPUNDER_FILELIST, POPUNDER_PM, PROMPT_PASSWORD,
		SHOW_MENU_BAR, SHOW_STATUSBAR, SHOW_TOOLBAR,
		SHOW_TRANSFERVIEW, STATUS_IN_CHAT, SHOW_IP_COUNTRY_CHAT,

		TOGGLE_ACTIVE_WINDOW, URL_HANDLER, USE_CTRL_FOR_LINE_HISTORY, USE_SYSTEM_ICONS,
		USERS_FILTER_FAVORITE, USERS_FILTER_ONLINE, USERS_FILTER_QUEUE, USERS_FILTER_WAITING,
		PRIVATE_MESSAGE_BEEP, PRIVATE_MESSAGE_BEEP_OPEN, SHOW_PROGRESS_BARS, MDI_MAXIMIZED,

		SHOW_INFOTIPS, MINIMIZE_ON_STARTUP, CONFIRM_QUEUE_REMOVAL,
		SPY_FRAME_IGNORE_TTH_SEARCHES, OPEN_WAITING_USERS, BOLD_WAITING_USERS, TABS_ON_TOP,
		OPEN_PUBLIC, OPEN_FAVORITE_HUBS, OPEN_FAVORITE_USERS, OPEN_QUEUE,
		OPEN_FINISHED_UPLOADS, OPEN_SEARCH_SPY, OPEN_NOTEPAD, PROGRESSBAR_ODC_STYLE,

		POPUP_AWAY, POPUP_MINIMIZED, POPUP_HUB_CONNECTED, POPUP_HUB_DISCONNECTED, POPUP_FAVORITE_CONNECTED,
		POPUP_DOWNLOAD_START, POPUP_DOWNLOAD_FAILED, POPUP_DOWNLOAD_FINISHED, POPUP_UPLOAD_FINISHED, POPUP_PM, POPUP_NEW_PM,

		UPLOADQUEUEFRAME_SHOW_TREE, SOUNDS_DISABLED, USE_OLD_SHARING_UI,
		TEXT_GENERAL_BOLD, TEXT_GENERAL_ITALIC, TEXT_MYOWN_BOLD, TEXT_MYOWN_ITALIC, TEXT_PRIVATE_BOLD, TEXT_PRIVATE_ITALIC, TEXT_SYSTEM_BOLD,
		TEXT_SYSTEM_ITALIC, TEXT_SERVER_BOLD, TEXT_SERVER_ITALIC, TEXT_TIMESTAMP_BOLD, TEXT_TIMESTAMP_ITALIC,
		TEXT_MYNICK_BOLD, TEXT_MYNICK_ITALIC, TEXT_FAV_BOLD, TEXT_FAV_ITALIC, TEXT_OP_BOLD, TEXT_OP_ITALIC, TEXT_URL_BOLD, TEXT_URL_ITALIC,
		PROGRESS_OVERRIDE_COLORS, PROGRESS_OVERRIDE_COLORS2, MENUBAR_TWO_COLORS, MENUBAR_BUMPED,
		
		SEARCH_SAVE_HUBS_STATE, CONFIRM_HUB_CLOSING, CONFIRM_AS_REMOVAL, 
		POPUNDER_TEXT, LOCK_TB, POPUNDER_PARTIAL_LIST, SHOW_TBSTATUS, SHOW_SHARED_DIRS_DL, EXPAND_BUNDLES,
		
		TEXT_QUEUE_BOLD, TEXT_QUEUE_ITALIC, UNDERLINE_QUEUE, 
		POPUP_BUNDLE_DLS, POPUP_BUNDLE_ULS, LIST_HL_BOLD, LIST_HL_ITALIC, 
		TEXT_DUPE_BOLD, TEXT_DUPE_ITALIC, UNDERLINE_LINKS, UNDERLINE_DUPES, 
		
		SORT_DIRS, TEXT_NORM_BOLD, TEXT_NORM_ITALIC,
		PASSWD_PROTECT, PASSWD_PROTECT_TRAY, BOLD_HUB_TABS_ON_KICK,
		USE_EXPLORER_THEME, TESTWRITE, OPEN_SYSTEM_LOG, OPEN_LOGS_INTERNAL, UC_SUBMENU, SHOW_QUEUE_BARS, EXPAND_DEFAULT,
		FLASH_WINDOW_ON_PM, FLASH_WINDOW_ON_NEW_PM, FLASH_WINDOW_ON_MYNICK,
		
		SERVER_COMMANDS, CLIENT_COMMANDS, PM_PREVIEW, 
		
		HUB_BOLD_TABS, SHOW_WINAMP_CONTROL, BLEND_TABS, TAB_SHOW_ICONS,
		FAV_USERS_SHOW_INFO, SEARCH_USE_EXCLUDED, AUTOSEARCH_BOLD, SHOW_EMOTICON, SHOW_MULTILINE, SHOW_MAGNET, SHOW_SEND_MESSAGE, WARN_ELEVATED,
		CONFIRM_FILE_DELETIONS, CLOSE_USE_MINIMIZE,

		FILTER_FL_SHARED, FILTER_FL_QUEUED, FILTER_FL_INVERSED, FILTER_FL_TOP, FILTER_FL_PARTIAL_DUPES, FILTER_FL_RESET_CHANGE, FILTER_SEARCH_SHARED, 
		FILTER_SEARCH_QUEUED, FILTER_SEARCH_INVERSED, FILTER_SEARCH_TOP, FILTER_SEARCH_PARTIAL_DUPES, FILTER_SEARCH_RESET_CHANGE, SEARCH_ASCH_ONLY, 
		
		USERS_FILTER_IGNORE, NFO_EXTERNAL, SINGLE_CLICK_TRAY, QUEUE_SHOW_FINISHED, 
		FILTER_QUEUE_INVERSED, FILTER_QUEUE_TOP, FILTER_QUEUE_RESET_CHANGE, OPEN_AUTOSEARCH, SAVE_LAST_STATE,

#endif
		BOOL_LAST 
	};

	enum Int64Setting { 
		INT64_FIRST = BOOL_LAST + 1,
		TOTAL_UPLOAD = INT64_FIRST, TOTAL_DOWNLOAD, 
		INT64_LAST, SETTINGS_LAST = INT64_LAST 
	};

	enum { 
		INCOMING_DISABLED = -1, 
		INCOMING_ACTIVE, 
		INCOMING_ACTIVE_UPNP, 
		INCOMING_PASSIVE,
		INCOMING_LAST = 4
	};

	enum { MULTITHREAD_NEVER, MULTITHREAD_MANUAL, MULTITHREAD_ALWAYS, MULTITHREAD_LAST };

	enum { OUTGOING_DIRECT, OUTGOING_SOCKS5, OUTGOING_LAST };

	enum { MAGNET_SEARCH, MAGNET_DOWNLOAD, MAGNET_OPEN };
	
	enum SettingProfile { PROFILE_NORMAL, PROFILE_RAR, PROFILE_LAN, PROFILE_LAST };

	enum { QUEUE_FILE, QUEUE_BUNDLE, QUEUE_ALL, QUEUE_LAST };

	enum { PRIO_DISABLED, PRIO_BALANCED, PRIO_PROGRESS, PRIO_LAST };

	enum { TLS_DISABLED, TLS_ENABLED, TLS_FORCED, TLS_LAST };

	enum { BLOOM_DISABLED, BLOOM_ENABLED, BLOOM_AUTO, BLOOM_LAST };

	static const ResourceManager::Strings encryptionStrings[TLS_LAST];
	static const ResourceManager::Strings bloomStrings[BLOOM_LAST];
	static const ResourceManager::Strings profileStrings[PROFILE_LAST];
	static const ResourceManager::Strings refreshStrings[MULTITHREAD_LAST];
	static const ResourceManager::Strings prioStrings[PRIO_LAST];
	static const ResourceManager::Strings incomingStrings[INCOMING_LAST];
	static const ResourceManager::Strings outgoingStrings[OUTGOING_LAST];
	static const ResourceManager::Strings dropStrings[QUEUE_LAST];
	static const ResourceManager::Strings updateStrings[VERSION_LAST];

	using SettingValue = boost::variant<bool, int, string>;
	using SettingValueList = vector<SettingValue>;

	using SettingKeyList = vector<int>;
	struct SettingChangeHandler {

		using List = vector<SettingChangeHandler>;
		using OnSettingChangedF = std::function<void (const MessageCallback &, const SettingKeyList &)>;

		OnSettingChangedF onChanged;
		SettingKeyList settingKeys;
	};

	void registerChangeHandler(const SettingKeyList& aKeys, SettingChangeHandler::OnSettingChangedF&& changeF) noexcept;

	SettingValue getSettingValue(int aSetting, bool useDefault = true) const noexcept;

	using EnumStringMap = map<int, ResourceManager::Strings>;
	static EnumStringMap getEnumStrings(int aKey, bool aValidateCurrentValue) noexcept;

	const string& get(StrSetting key, bool useDefault = true) const noexcept {
		return (isSet[key] || !useDefault) ? strSettings[key - STR_FIRST] : strDefaults[key - STR_FIRST];
	}

	int get(IntSetting key, bool useDefault = true) const noexcept {
		return (isSet[key] || !useDefault) ? intSettings[key - INT_FIRST] : intDefaults[key - INT_FIRST];
	}
	bool get(BoolSetting key, bool useDefault = true) const noexcept {
		return (isSet[key] || !useDefault) ? boolSettings[key - BOOL_FIRST] : boolDefaults[key - BOOL_FIRST];
	}
	int64_t get(Int64Setting key, bool useDefault = true) const noexcept {
		return (isSet[key] || !useDefault) ? int64Settings[key - INT64_FIRST] : int64Defaults[key - INT64_FIRST];
	}

	// Use forceSet to force the value to be saved to the XML file
	// Used during initial loading as profile defaults haven't been loaded yet (making the default value comparison unreliable) 
	void set(StrSetting key, string const& value, bool aForceSet = false) noexcept;
	void set(IntSetting key, int value, bool aForceSet = false) noexcept;
	void set(BoolSetting key, bool value, bool aForceSet = false) noexcept;
	void set(Int64Setting key, int64_t value, bool aForceSet = false) noexcept;

	void set(BoolSetting key, const string& value) noexcept;
	void set(Int64Setting key, const string& value) noexcept;
	void set(IntSetting key, const string& value) noexcept;

	const string& getDefault(StrSetting key) const noexcept {
		return strDefaults[key - STR_FIRST];
	}

	int getDefault(IntSetting key) const noexcept {
		return intDefaults[key - INT_FIRST];
	}

	bool getDefault(BoolSetting key) const noexcept {
		return boolDefaults[key - BOOL_FIRST];
	}

	int64_t getDefault(Int64Setting key) const noexcept {
		return int64Defaults[key - INT64_FIRST];
	}

	void setDefault(StrSetting key, const string_view& value) noexcept {
		strDefaults[key - STR_FIRST] = value;
	}

	void setDefault(IntSetting key, int value) noexcept {
		intDefaults[key - INT_FIRST] = value;
	}

	void setDefault(BoolSetting key, bool value) noexcept {
		boolDefaults[key - BOOL_FIRST] = value;
	}

	void setDefault(Int64Setting key, int64_t value) noexcept {
		int64Defaults[key - INT64_FIRST] = value;
	}

	template<typename KeyT> bool isDefault(KeyT key) noexcept {
		return !isSet[key] || get(key, false) == getDefault(key);
	}

	// Update the value as set if it differs from the default value 
	template<typename KeyT, typename ValueT> void updateValueSet(KeyT key, ValueT value, bool aForceSet) noexcept {
		if (!isSet[key]) {
			isSet[key] = aForceSet || value != getDefault(key);
		}
	}

	void unsetKey(size_t key) noexcept { isSet[key] = false; }
	bool isKeySet(size_t key) const noexcept { return isSet[key]; }

	void load(StartupLoader& aLoader) noexcept;
	void save() noexcept;
	
	void reloadPages(int group = 0) noexcept {
		fire(SettingsManagerListener::ReloadPages(), group);
	}
	void Cancel() noexcept {
		fire(SettingsManagerListener::Cancel(), 0);
	}

	HubSettings getHubSettings() const noexcept;

	using HistoryList = vector<string>;

	enum HistoryType {
		HISTORY_SEARCH,
		HISTORY_EXCLUDE,
		HISTORY_DOWNLOAD_DIR,
		HISTORY_LAST
	};

	bool addToHistory(const string& aString, HistoryType aType) noexcept;
	void clearHistory(HistoryType aType) noexcept;
	HistoryList getHistory(HistoryType aType) const noexcept;

	void setProfile(int aProfile, const ProfileSettingItem::List& conflicts) noexcept;
	static const ProfileSettingItem::List profileSettings[SettingsManager::PROFILE_LAST];
	void applyProfileDefaults() noexcept;
	string getProfileName(int profile) const noexcept;

	// Reports errors to system log if no custom error function is supplied
	static bool saveSettingFile(SimpleXML& aXML, AppUtil::Paths aPath, const string& aFileName, const MessageCallback& aCustomErrorF = nullptr) noexcept;
	static bool saveSettingFile(const string& aContent, AppUtil::Paths aPath, const string& aFileName, const MessageCallback& aCustomErrorF = nullptr) noexcept;

	// Attempts to load the setting file and creates a backup after completion
	// Settings are recovered automatically from the backup file in case the main setting file is malformed/corrupted
	using XMLParseCallback = std::function<void (SimpleXML &)>;
	using PathParseCallback = std::function<bool (const string &)>;
	static bool loadSettingFile(AppUtil::Paths aPath, const string& aFileName, XMLParseCallback&& aParseCallback, const MessageCallback& aCustomErrorF = nullptr) noexcept;
	static bool loadSettingFile(AppUtil::Paths aPath, const string& aFileName, PathParseCallback&& aParseCallback, const MessageCallback& aCustomErrorF = nullptr) noexcept;

	const SettingChangeHandler::List& getChangeCallbacks() const noexcept {
		return settingChangeHandlers;
	}
private:
	SettingChangeHandler::List settingChangeHandlers;
	boost::regex connectionRegex;

	friend class Singleton<SettingsManager>;
	SettingsManager();
	~SettingsManager() override = default;

	static const string settingTags[SETTINGS_LAST+1];

	string strSettings[STR_LAST - STR_FIRST];
	int    intSettings[INT_LAST - INT_FIRST]{};
	bool boolSettings[BOOL_LAST - BOOL_FIRST]{};
	int64_t int64Settings[INT64_LAST - INT64_FIRST]{};

	string strDefaults[STR_LAST - STR_FIRST];
	int    intDefaults[INT_LAST - INT_FIRST]{};
	bool boolDefaults[BOOL_LAST - BOOL_FIRST]{};
	int64_t int64Defaults[INT64_LAST - INT64_FIRST]{};

	bool isSet[SETTINGS_LAST]{};

	HistoryList history[HISTORY_LAST];
	static const string historyTags[HISTORY_LAST];

	mutable SharedMutex cs;

	static string buildToolbarOrder(const vector<ToolbarIconEnum>& aIcons) noexcept;
	static vector<ToolbarIconEnum> getDefaultToolbarOrder() noexcept;

	void saveSettings(SimpleXML& xml) const;
	void saveHistory(SimpleXML& xml) const;

	void loadSettings(SimpleXML& xml);
	void loadHistory(SimpleXML& xml);

	void ensureValidBindAddresses(const StartupLoader& aLoader) noexcept;
};

// Shorthand accessor macros
#define SETTING(k) (SettingsManager::getInstance()->get(SettingsManager::k, true))

} // namespace dcpp

#endif // !defined(SETTINGS_MANAGER_H)