/*
 * Copyright (C) 2001-2024 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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

#ifndef DCPLUSPLUS_DCPP_SHARE_MANAGER_H
#define DCPLUSPLUS_DCPP_SHARE_MANAGER_H


#include "HashManagerListener.h"
#include "SettingsManagerListener.h"
#include "ShareManagerListener.h"
#include "TimerManagerListener.h"

#include "CriticalSection.h"
#include "DupeType.h"
#include "Exception.h"
#include "Message.h"
#include "MerkleTree.h"
#include "Pointer.h"
#include "ShareDirectory.h"
#include "ShareDirectoryInfo.h"
#include "ShareProfile.h"
#include "ShareRefreshInfo.h"
#include "ShareRefreshTask.h"
#include "ShareStats.h"
#include "Singleton.h"
#include "TempShareItem.h"
#include "TimerManager.h"

namespace dcpp {

class File;
class ErrorCollector;
class HashBloom;
class HashedFile;
class OutputStream;
class MemoryInputStream;
class SearchQuery;
class SharePathValidator;
class ShareTasks;
class ShareTree;

class FileList;

class ShareManager : public Singleton<ShareManager>, public Speaker<ShareManagerListener>, private SettingsManagerListener, 
	private TimerManagerListener, private HashManagerListener, public ShareTasksManager
{
public:
	static void log(const string& aMsg, LogMessage::Severity aSeverity) noexcept;
	static void duplicateFilelistFileLogger(const StringList& aDirectoryPaths, int aDupeFileCount) noexcept;

	SharePathValidator& getValidator() noexcept {
		return *validator.get();
	}

	void startup(StartupLoader& aLoader) noexcept;
	void shutdown(function<void(float)> progressF) noexcept;


	// Validate that the new root can be added in share (sub/parent/existing directory matching)
	// Throws ShareException
	void validateRootPath(const string& aRealPath, bool aMatchCurrentRoots = true) const;

	// Returns virtual path of a TTH
	// Throws ShareException
	string toVirtual(const TTHValue& aTTH, ProfileToken aProfile) const;

	// Returns size and file name of a filelist
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	// Throws ShareException
	pair<int64_t, string> getFileListInfo(const string& virtualFile, ProfileToken aProfile);

	// Get real path and size for a virtual path
	// noAccess_ will be set to true if the file is availabe but not in the supplied profiles
	// Throws ShareException
	void toRealWithSize(const string& aVirtualPath, const ProfileTokenSet& aProfiles, const HintedUser& aUser, string& path_, int64_t& size_, bool& noAccess_);

	// Returns TTH value for a file list (not very useful but the ADC specs...)
	// virtualFile = name requested by the other user (Transfer::USER_LIST_NAME_BZ or Transfer::USER_LIST_NAME)
	// Throws ShareException
	TTHValue getListTTH(const string& aVirtualPath, ProfileToken aProfile) const;

	// Refresh the whole share or in
	RefreshTaskQueueInfo refresh(ShareRefreshType aType, ShareRefreshPriority aPriority, function<void(float)> progressF = nullptr) noexcept;

	// Refresh a single single path or all paths under a virtual name (roots only)
	// Returns nullopt if the path doesn't exist in share
	optional<RefreshTaskQueueInfo> refreshVirtualName(const string& aVirtualName, ShareRefreshPriority aPriority) noexcept;

	// Refresh the specific directories
	// Returns nullopt if the path doesn't exist in share (and it can't be added there)
	optional<RefreshTaskQueueInfo> refreshPathsHooked(ShareRefreshPriority aPriority, const StringList& aPaths, const void* aCaller, const string& aDisplayName = Util::emptyString, function<void(float)> aProgressF = nullptr) noexcept;

	// Refresh the specific directories
	// Throws if the path doesn't exist in share and can't be added there
	RefreshTaskQueueInfo refreshPathsHookedThrow(ShareRefreshPriority aPriority, const StringList& aPaths, const void* aCaller, const string& aDisplayName = Util::emptyString, function<void(float)> aProgressF = nullptr);

	bool isRefreshing() const noexcept;

	// Abort filelist refresh (or an individual refresh task)
	bool abortRefresh(optional<ShareRefreshTaskToken> aToken = nullopt) noexcept;

	bool handleRefreshPath(const string& aPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_) noexcept;
	void onRefreshTaskCompleted(bool aCompleted, const ShareRefreshTask& aTask, const ShareRefreshStats& aTotalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_) noexcept;

	unique_ptr<ShareTasksManager::RefreshTaskHandler> startRefresh(const ShareRefreshTask& aTask) noexcept override;
	void onRefreshQueued(const ShareRefreshTask& aTask) noexcept override;

	ShareRefreshTaskList getRefreshTasks() const noexcept;

	// Throws ShareException in case an invalid path is provided
	void search(SearchResultList& l, SearchQuery& aSearch, const OptionalProfileToken& aProfile, const UserPtr& aUser, const string& aDir, bool aIsAutoSearch = false);

	// Check if a directory is shared
	// You may also give a path in NMDC format and the relevant 
	// directory (+ possible subdirectories) are detected automatically
	bool isAdcDirectoryShared(const string& aAdcPath) const noexcept;

	// Mostly for dupe check with size comparison (partial/exact dupe)
	DupeType isAdcDirectoryShared(const string& aAdcPath, int64_t aSize) const noexcept;

	bool isFileShared(const TTHValue& aTTH) const noexcept;
	bool isFileShared(const TTHValue& aTTH, ProfileToken aProfile) const noexcept;
	bool isRealPathShared(const string& aPath) const noexcept;

	// Returns true if the real path can be added in share
	bool allowShareDirectoryHooked(const string& aPath, const void* aCaller) const noexcept;

	// Validate a file/directory path
	// Throws on errors
	void validatePathHooked(const string& aPath, bool aSkipQueueCheck, const void* aCaller) const;

	// Returns the dupe paths by directory name/NMDC path
	StringList getAdcDirectoryPaths(const string& aAdcPath) const noexcept;

	GroupedDirectoryMap getGroupedDirectories() const noexcept;
	MemoryInputStream* generatePartialList(const string& aVirtualPath, bool aRecursive, const OptionalProfileToken& aProfile) const noexcept;
	MemoryInputStream* generateTTHList(const string& aVirtualPath, bool aRecursive, ProfileToken aProfile) const noexcept;
	MemoryInputStream* getTree(const string& virtualFile, ProfileToken aProfile) const noexcept;

	void saveShareCache(function<void (float)> progressF = nullptr) noexcept;	//for filelist caching

	// Throws ShareException
	AdcCommand getFileInfo(const string& aFile, ProfileToken aProfile);

	int64_t getTotalShareSize(ProfileToken aProfile) const noexcept;

	// Get share size and number of files for a specified profile
	void getProfileInfo(ProfileToken aProfile, int64_t& totalSize_, size_t& fileCount_) const noexcept;
	
	// Adds all shared TTHs (permanent and temp) to the filter
	void getBloom(HashBloom& bloom) const noexcept;

	// Removes path characters from virtual name
	string validateVirtualName(const string& aName) const noexcept;

	// Generate own full filelist on disk
	// Throws ShareException
	string generateOwnList(ProfileToken aProfile);

	bool isTTHShared(const TTHValue& tth) const noexcept;

	// Get real paths for an ADC virtual path
	// Throws ShareException
	void getRealPaths(const string& aVirtualPath, StringList& realPaths_, const OptionalProfileToken& aProfile = nullopt) const;

	StringList getRealPaths(const TTHValue& root) const noexcept;

	int64_t getSharedSize() const noexcept;

	optional<TempShareInfo> addTempShare(const TTHValue& aTTH, const string& aName, const string& aFilePath, int64_t aSize, ProfileToken aProfile, const UserPtr& aUser) noexcept;

	TempShareInfoList getTempShares() const noexcept;
	TempShareInfoList getTempShares(const TTHValue& aTTH) const noexcept;

	bool removeTempShare(TempShareToken aId) noexcept;
	optional<TempShareToken> isTempShared(const UserPtr& aUser, const TTHValue& aTTH) const noexcept;
	//tempShares end

	// Get a printable version of various share-related statistics
	string printStats() const noexcept;

	optional<ShareItemStats> getShareItemStats() const noexcept;
	ShareSearchStats getSearchMatchingStats() const noexcept;

	ShareDirectoryInfoList getRootInfos() const noexcept;
	ShareDirectoryInfoPtr getRootInfo(const string& aPath) const noexcept;

	void addRootDirectories(const ShareDirectoryInfoList& aNewDirs) noexcept;
	void updateRootDirectories(const ShareDirectoryInfoList& renameDirs) noexcept;
	void removeRootDirectories(const StringList& removeDirs) noexcept;

	bool addRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept;
	bool updateRootDirectory(const ShareDirectoryInfoPtr& aDirectoryInfo) noexcept;

	// Removes the root path entirely share (from all profiles)
	bool removeRootDirectory(const string& aPath) noexcept;

	void addProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void removeProfiles(const ShareProfileInfo::List& aProfiles) noexcept;
	void renameProfiles(const ShareProfileInfo::List& aProfiles) noexcept;

	void addProfile(const ShareProfilePtr& aProfile) noexcept;
	void updateProfile(const ShareProfilePtr& aProfile) noexcept;
	bool removeProfile(ProfileToken aToken) noexcept;

	// Convert real path to virtual path. Returns an empty string if not shared.
	string realToVirtualAdc(const string& aPath, const OptionalProfileToken& aToken = nullopt) const noexcept;

	// If allowFallback is true, the default profile will be returned if the requested one is not found
	ShareProfilePtr getShareProfile(ProfileToken aProfile, bool allowFallback = false) const noexcept;

	ShareProfileList getProfiles() const noexcept;
	ShareProfileInfo::List getProfileInfos() const noexcept;

	// Get a profile token by its display name
	OptionalProfileToken getProfileByName(const string& aName) const noexcept;

	void setDefaultProfile(ProfileToken aNewDefault) noexcept;

	// aIsMajor will regenerate the file list on next time when someone requests it
	void setProfilesDirty(const ProfileTokenSet& aProfiles, bool aIsMajor) noexcept;

	void removeCachedFilelists() noexcept;

	void shareBundle(const BundlePtr& aBundle) noexcept;
	void onFileHashed(const string& aRealPath, const HashedFile& aFileInfo) noexcept;

	StringSet getExcludedPaths() const noexcept;
	void addExcludedPath(const string& aPath);
	bool removeExcludedPath(const string& aPath) noexcept;

	void reloadSkiplist();
	void setExcludedPaths(const StringSet& aPaths) noexcept;

	struct ShareLoader;
private:
	const unique_ptr<SharePathValidator> validator;
	const unique_ptr<ShareTasks> tasks;
	const unique_ptr<ShareTree> tree;

	mutable SharedMutex cs;

	friend class Singleton<ShareManager>;
	
	ShareManager();
	~ShareManager();

	// Throws ShareException
	FileList* generateXmlList(ProfileToken aProfile, bool aForced = false);

	// Throws ShareException
	FileList* getFileList(ProfileToken aProfile) const;

	bool loadCache(function<void(float)> progressF) noexcept;

	uint64_t lastFullUpdate = GET_TICK();
	uint64_t lastIncomingUpdate = GET_TICK();
	uint64_t lastSave = 0;
	
	bool shareCacheSaving = false;

	struct RefreshTaskHandler : public ShareTasksManager::RefreshTaskHandler {
		typedef function<bool(const string& aRefreshPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_)> PathRefreshF;
		typedef function<void(bool aCompleted, const ShareRefreshTask& aTask, const ShareRefreshStats& aTotalStats, ShareBloom* bloom_, ProfileTokenSet& dirtyProfiles_)> CompletionF;

		RefreshTaskHandler(ShareBloom* aBloom, PathRefreshF aPathRefreshF, CompletionF aCompletionF);

		void refreshCompleted(bool aCompleted, const ShareRefreshTask& aTask, const ShareRefreshStats& aTotalStats) override;
		bool refreshPath(const string& aRefreshPath, const ShareRefreshTask& aTask, ShareRefreshStats& totalStats) override;

		PathRefreshF const pathRefreshF;
		CompletionF const completionF;

		ShareBloom* bloom;
		ProfileTokenSet dirtyProfiles;


		class ShareBuilder : public ShareRefreshInfo {
		public:
			ShareBuilder(const string& aPath, const ShareDirectory::Ptr& aOldRoot, time_t aLastWrite, ShareBloom& bloom_, ShareManager* sm);

			// Recursive function for building a new share tree from a path
			bool buildTree(const bool& aStopping) noexcept;
		private:
			void buildTree(const string& aPath, const string& aPathLower, const ShareDirectory::Ptr& aCurrentDirectory, const ShareDirectory::Ptr& aOldDirectory, const bool& aStopping);

			bool validateFileItem(const FileItemInfoBase& aFileItem, const string& aPath, bool aIsNew, bool aNewParent, ErrorCollector& aErrorCollector) noexcept;

			const ShareManager& sm;
		};

		typedef shared_ptr<ShareBuilder> ShareBuilderPtr;
		typedef set<ShareBuilderPtr, std::less<ShareBuilderPtr>> ShareBuilderSet;
	};

	// Change the refresh status for a directory and its subroots
	// Safe to call with non-root directories
	void setRefreshState(const string& aPath, ShareRootRefreshState aState, bool aUpdateRefreshTime, const optional<ShareRefreshTaskToken>& aRefreshTaskToken) noexcept;

	// HashManagerListener
	void on(HashManagerListener::FileHashed, const string& aPath, HashedFile& fi) noexcept override { onFileHashed(aPath, fi); }

	// SettingsManagerListener
	void on(SettingsManagerListener::Save, SimpleXML& xml) noexcept override {
		save(xml);
	}
	void on(SettingsManagerListener::Load, SimpleXML& xml) noexcept override {
		load(xml);
	}

	void on(SettingsManagerListener::LoadCompleted, bool aFileLoaded) noexcept override;
	
	// TimerManagerListener
	void on(TimerManagerListener::Minute, uint64_t aTick) noexcept override;

	void load(SimpleXML& aXml);
	void loadProfile(SimpleXML& aXml, const string& aName, ProfileToken aToken);
	void save(SimpleXML& aXml);
	
	ShareProfileList shareProfiles;
}; //sharemanager end

} // namespace dcpp

#endif // !defined(SHARE_MANAGER_H)
