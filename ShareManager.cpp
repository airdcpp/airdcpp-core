/*
 * Copyright (C) 2001-2011 Jacek Sieka, arnetheduck on gmail point com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include "stdinc.h"
#include "ShareManager.h"

#include "ResourceManager.h"

#include "CryptoManager.h"
#include "ClientManager.h"
#include "LogManager.h"
#include "HashManager.h"
#include "QueueManager.h"

#include "AdcHub.h"
#include "SimpleXML.h"
#include "StringTokenizer.h"
#include "File.h"
#include "FilteredFile.h"
#include "BZUtils.h"
#include "Transfer.h"
#include "UserConnection.h"
#include "Download.h"
#include "HashBloom.h"
#include "SearchResult.h"
#include "Wildcards.h"
#include "AirUtil.h"
#include "format.h"

#ifdef _WIN32
# include <ShlObj.h>
#else
# include <dirent.h>
# include <sys/stat.h>
# include <unistd.h>
# include <fnmatch.h>
#endif

#include <limits>

namespace dcpp {

#define SHARE_CACHE_VERSION "1"


ShareManager::ShareManager() : hits(0), refreshing(false),
	lastFullUpdate(GET_TICK()), lastIncomingUpdate(GET_TICK()), bloom(1<<20), sharedSize(0), ShareCacheDirty(false), GeneratingFULLXmlList(false),
	updateSize(true), totalShareSize(0), xml_saving(false), lastSave(GET_TICK()), aShutdown(false), allSearches(0), stoppedSearches(0)
{ 
	//We MUST have atleast the full filelist at all times!
	FileList* fl = new FileList(FileListALL);
	fileLists.insert(make_pair(FileListALL, fl));

	SettingsManager::getInstance()->addListener(this);
	TimerManager::getInstance()->addListener(this);
	QueueManager::getInstance()->addListener(this);

	RAR_regexp.Init("[Rr0-9][Aa0-9][Rr0-9]");
}

ShareManager::~ShareManager() {

	SettingsManager::getInstance()->removeListener(this);
	TimerManager::getInstance()->removeListener(this);
	QueueManager::getInstance()->removeListener(this);

	join();
	w.join();

	for(auto i = fileLists.begin(); i != fileLists.end(); ++i) 
		delete i->second;

	fileLists.clear();
}

void ShareManager::shutdown() {
	//abort buildtree and refresh, we are shutting down.
	aShutdown = true;

	if(ShareCacheDirty || !Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml"))
		saveXmlList();

	try {
		StringList lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2");
		//clear refs so we can delete filelists.
		RLock l(cs);
		for(auto f = fileLists.begin(); f != fileLists.end(); ++f) {
			if(f->first == FileListALL) 
				continue;
			if(f->second->bzXmlRef.get()) 
				f->second->bzXmlRef.reset(); 
			
		}

		for(StringList::const_iterator i = lists.begin(); i != lists.end(); ++i) {
			File::deleteFile(*i); // cannot delete the current filelist due to the bzxmlref.
		}
		lists.clear();
		
		//leave the latest filelist undeleted, and rename it to files.xml.bz2
		FileList* fl =  fileLists.find(FileListALL)->second;
		
		if(fl->bzXmlRef.get()) 
			fl->bzXmlRef.reset(); 

			if(!Util::fileExists(Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2"))				
				File::renameFile(fl->getBZXmlFile(), ( Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2") ); 
				
		} catch(...) {
		//ignore, we just failed to delete
		}
		
}

ShareManager::Directory::Directory(const string& aName, const ShareManager::Directory::Ptr& aParent, RootDirectory* aRoot) :
	size(0),
	name(aName),
	parent(aParent.get()),
	fileTypes(1 << SearchManager::TYPE_DIRECTORY),
	root(aRoot)
{
}

string ShareManager::Directory::getADCPath() const noexcept {
	if(!getParent())
		return '/' + name + '/';
	return getParent()->getADCPath() + name + '/';
}

string ShareManager::Directory::getFullName() const noexcept {
	if(!getParent())
		return getName() + '\\';
	return getParent()->getFullName() + getName() + '\\';
}

void ShareManager::Directory::addType(uint32_t type) noexcept {
	if(!hasType(type)) {
		fileTypes |= (1 << type);
		if(getParent())
			getParent()->addType(type);
	}
}

bool ShareManager::isHubExcluded(const string& sharepath, const Client* client) const {

	if(!client || client->getUnShared().empty())
		return false;

	if(std::binary_search(client->getUnShared().begin(), client->getUnShared().end(), sharepath))
		return true;

	return false;
}
bool ShareManager::isExcluded(const string& sharepath, const ClientList& clients) const {
	if(clients.empty())
		return false;

	for(ClientList::const_iterator i = clients.begin(); i != clients.end(); ++i) {
		if(isHubExcluded(sharepath, *i))
			continue;
		return false; //if even one of the hubs says its not excluded it means we should share it.
	}
	return true;
}
string ShareManager::getRealPath(const TTHValue& root) {
	RLock l(cs); //better the possible small freeze than a crash right?
	string result = ""; 
	for(auto m = directories.begin(); m != directories.end(); ++m) {
		auto i = m->second->getRoot()->tthIndex.find(const_cast<TTHValue*>(&root)); 
			if(i != m->second->getRoot()->tthIndex.end()) {
				result = i->second->getRealPath();
				break;
			}
	}
	return result;
}

string ShareManager::Directory::getRealPath(const std::string& path, bool validate/*true*/) const {
	if(getParent()) {
		return getParent()->getRealPath(getName() + PATH_SEPARATOR_STR + path, validate);
	}else if(!getRoot()->getPath().empty()) {
		string root = getRoot()->getPath() + path;

		if(!validate) //no extra checks for finding the file while loading share cache.
			return root;

		/*check for the existance here if we have moved the file/folder and only refreshed the new location.
		should we even look, what's moved is moved, user should refresh both locations.*/
		if(Util::fileExists(root))
			return root;
		else
			return ShareManager::getInstance()->findRealRoot(getName(), path);
	} else { //shouldnt need to go here
		return ShareManager::getInstance()->findRealRoot(getName(), path);
	}
}

string ShareManager::findRealRoot(const string& virtualRoot, const string& virtualPath) const {
	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i) {  
		if(stricmp(i->second, virtualRoot) == 0) {
			std::string name = i->first + virtualPath;
			dcdebug("Matching %s\n", name.c_str());
			if(FileFindIter(name) != FileFindIter()) {
				return name;
			}
		}
	}
	
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

int64_t ShareManager::Directory::getSize() const noexcept {
	int64_t tmp = size;
	for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i)
		tmp+=i->second->getSize();
	return tmp;
}

//ApexDC
size_t ShareManager::Directory::countFiles() const noexcept {
	size_t tmp = files.size();
	for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i)
		tmp+=i->second->countFiles();
	return tmp;
}
//End

string ShareManager::toVirtual(const TTHValue& tth, Client* client) const  {
	
	RLock l(cs);

	FileList* fl = getFileList(client);
	if(tth == fl->getbzXmlRoot()) {
		return Transfer::USER_LIST_NAME_BZ;
	} else if(tth == fl->getxmlRoot()) {
		return Transfer::USER_LIST_NAME;
	}

	for(auto m = directories.begin(); m != directories.end(); ++m) {
		auto i = m->second->getRoot()->tthIndex.find(const_cast<TTHValue*>(&tth)); 
			if(i != m->second->getRoot()->tthIndex.end()) 
				return i->second->getADCPath();
	}
	//nothing found throw;
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

//should allways return a valid pointer!
ShareManager::FileList* ShareManager::getFileList(Client* client) const{
	if(client && !client->getUnShared().empty()) {
		auto i = fileLists.find(AirUtil::stripHubUrl(client->getHubUrl()));
		if(i != fileLists.end())
			return i->second;
	} 
		
	return fileLists.find(FileListALL)->second;
}

string ShareManager::toReal(const string& virtualFile, bool isInSharingHub, const HintedUser& aUser, const string& userSID)  {
	return toRealWithSize(virtualFile, isInSharingHub, aUser, userSID).first;
}

pair<string, int64_t> ShareManager::toRealWithSize(const string& virtualFile, bool isInSharingHub, const HintedUser& aUser, const string& userSID) {
	
	if(virtualFile == "MyList.DcLst") 
		throw ShareException("NMDC-style lists no longer supported, please upgrade your client");

	if(virtualFile == Transfer::USER_LIST_NAME_BZ || virtualFile == Transfer::USER_LIST_NAME) {
		Client* client = NULL;

		if(!aUser.user->isNMDC()) {
			client = ClientManager::getInstance()->findClient(aUser, userSID);
		}
		if(aUser.hint.empty() && !aUser.user->isNMDC()) //debug info.
			LogManager::getInstance()->message("toReal() hint url empty!! Report this to Night, User: " + Util::toString(ClientManager::getInstance()->getNicks(aUser)), LogManager::LOG_WARNING);

		FileList* fl = generateXmlList(client);
		if (!isInSharingHub) { //Hide Share Mod
			return make_pair((Util::getPath(Util::PATH_USER_CONFIG) + "Emptyfiles.xml.bz2"), 0);
		}
		return make_pair(fl->getBZXmlFile(), 0);
	}
	ClientList clients;
	if(!aUser.user->isNMDC()) {
		ClientManager::getInstance()->ListClients(aUser.user, clients);
	}
	RLock l(cs);
	try {
		auto i = findFile(virtualFile, clients);
		return make_pair(i->getRealPath(), i->getSize());
	}catch(ShareException&) {
		TempShareInfo i = findTempShare(aUser.user->getCID().toBase32(), virtualFile);
		return make_pair(i.path, i.size);
	}
}

TTHValue ShareManager::getTTH(const string& virtualFile, const HintedUser& aUser, const string& userSID) const {
	Client* client = NULL;

	if(virtualFile == Transfer::USER_LIST_NAME_BZ) {
		client = ClientManager::getInstance()->findClient(aUser, userSID);
		RLock l(cs);
		return getFileList(client)->getbzXmlRoot();
	} else if(virtualFile == Transfer::USER_LIST_NAME) {
		client = ClientManager::getInstance()->findClient(aUser, userSID);
		RLock l(cs);
		return getFileList(client)->getxmlRoot();
	}
	ClientList clients;
	if(!aUser.user->isNMDC()) {
		ClientManager::getInstance()->ListClients(aUser.user, clients);
	}
	RLock l(cs);
	return findFile(virtualFile, clients)->getTTH();
}

MemoryInputStream* ShareManager::getTree(const string& virtualFile, const HintedUser& aUser, const string& userSID) const {
	TigerTree tree;
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		if(!HashManager::getInstance()->getTree(TTHValue(virtualFile.substr(4)), tree))
			return 0;
	} else {
		try {

			TTHValue tth = getTTH(virtualFile, aUser, userSID);
			HashManager::getInstance()->getTree(tth, tree);
		} catch(const Exception&) {
			return 0;
		}
	}

	ByteVector buf = tree.getLeafData();
	return new MemoryInputStream(&buf[0], buf.size());
}

AdcCommand ShareManager::getFileInfo(const string& aFile, Client* client) {
	
	if(aFile == Transfer::USER_LIST_NAME) {
		FileList* fl = generateXmlList(client);
		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getxmlListLen()));
		cmd.addParam("TR", fl->getxmlRoot().toBase32());
		return cmd;
	} else if(aFile == Transfer::USER_LIST_NAME_BZ) {
		FileList* fl = generateXmlList(client);

		AdcCommand cmd(AdcCommand::CMD_RES);
		cmd.addParam("FN", aFile);
		cmd.addParam("SI", Util::toString(fl->getbzXmlListLen()));
		cmd.addParam("TR", fl->getbzXmlRoot().toBase32());
		return cmd;
	}

	if(aFile.compare(0, 4, "TTH/") != 0)
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);

	TTHValue val(aFile.substr(4));
	
	RLock l(cs);

	for(auto m = directories.begin(); m != directories.end(); ++m) {
		//if(isHubExcluded(m->first, client)) continue;
		auto i = m->second->getRoot()->tthIndex.find(const_cast<TTHValue*>(&val)); 
		if(i != m->second->getRoot()->tthIndex.end()) {
			const Directory::File& f = *i->second;
			AdcCommand cmd(AdcCommand::CMD_RES);
			cmd.addParam("FN", f.getADCPath());
			cmd.addParam("SI", Util::toString(f.getSize()));
			cmd.addParam("TR", f.getTTH().toBase32());
			return cmd;
		}
	}
	//not found throw
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

ShareManager::TempShareInfo ShareManager::findTempShare(const string& aKey, const string& virtualFile) {
		
	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		Lock l(tScs);
		TTHValue tth(virtualFile.substr(4));
		auto Files = tempShares.equal_range(tth);
		for(auto i = Files.first; i != Files.second; ++i) {
			if(i->second.key.empty() || (i->second.key == aKey)) // if no key is set, it means its a hub share.
				return i->second;
		}
	}	
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);		
}
bool ShareManager::addTempShare(const string& aKey, TTHValue& tth, const string& filePath, int64_t aSize, bool adcHub) {
	//first check if already exists in Share.
	if(isFileShared(tth, Util::getFileName(filePath))) {
		return true;
	} else if(adcHub) {
		Lock l(tScs);
		auto Files = tempShares.equal_range(tth);
		for(auto i = Files.first; i != Files.second; ++i) {
			if(i->second.key == aKey)
				return true;
			}
		//didnt exist.. fine, add it.
		tempShares.insert(make_pair(tth, TempShareInfo(aKey, filePath, aSize)));
		return true;
	}
	return false;
}
void ShareManager::removeTempShare(const string& aKey, TTHValue& tth) {
	Lock l(tScs);
	auto Files = tempShares.equal_range(tth);
	for(auto i = Files.first; i != Files.second; ++i) {
		if(i->second.key == aKey) {
			tempShares.erase(i);
			break;
		}
	}
}

ShareManager::DirMultiMap ShareManager::findVirtuals(const string& virtualPath, const ClientList& clients) const {

	Dirs virtuals; //since we are mapping by realpath, we can have more than 1 same virtualnames
	DirMultiMap ret;
	if(virtualPath.empty() || virtualPath[0] != '/') {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	string::size_type start = virtualPath.find('/', 1);
	if(start == string::npos || start == 1) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	virtuals = getByVirtual( virtualPath.substr(1, start - 1), clients);
	if(virtuals.empty()) {
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	Directory::Ptr d;
	for(Dirs::const_iterator k = virtuals.begin(); k != virtuals.end(); k++) {
		string::size_type i = start; // always start from the begin.
		string::size_type j = i + 1;
		d = *k;

		if(virtualPath.find('/', j) == string::npos) {	  // we only have root virtualpaths.
			ret.insert(make_pair(virtualPath.substr(j), d));
		
		} else {
			Directory::MapIter mi;
			while((i = virtualPath.find('/', j)) != string::npos) {
				if(d){
					mi = d->directories.find(virtualPath.substr(j, i - j));
					j = i + 1;
					if(mi != d->directories.end()) {   //if we found something, look for more.
						d = mi->second;
					} else {
						d = NULL;   //make the pointer null so we can check if found something or not.
						break;
					}
				}
			}

			if(d != NULL) 
				ret.insert(make_pair(virtualPath.substr(j), d));
		}
	}
	if(ret.empty()) {
	//if we are here it means we didnt find anything, throw.
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	return ret;
}

ShareManager::Directory::File::Set::const_iterator ShareManager::findFile(const string& virtualFile, const ClientList& clients) const {
	//if a user has a hold of our file, probobly from another hub, just return it.

	if(virtualFile.compare(0, 4, "TTH/") == 0) {
		TTHValue tth(virtualFile.substr(4));
		
		for(auto m = directories.begin(); m != directories.end(); ++m) {
			if(isExcluded(m->first, clients))
				continue;
			auto i = m->second->getRoot()->tthIndex.find(const_cast<TTHValue*>(&tth)); 
			if(i != m->second->getRoot()->tthIndex.end())
				return i->second;
		}
		//nothing found in any of tthIndex throw.
		throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
	}

	DirMultiMap dirs = findVirtuals(virtualFile, clients);
	for(DirMultiMap::iterator v = dirs.begin(); v != dirs.end(); ++v) {
		Directory::File::Set::const_iterator it = find_if(v->second->files.begin(), v->second->files.end(),
		Directory::File::StringComp(v->first));
		if(it != v->second->files.end())
			return it;
	}
	throw ShareException(UserConnection::FILE_NOT_AVAILABLE);
}

void ShareManager::getRealPaths(const string& path, StringList& ret) {
	if(path.empty())
		throw ShareException("empty virtual path");


	string dir;
	DirMultiMap dirs = findVirtuals(path, ClientList());


	if(*(path.end() - 1) == '/') {
		Directory::Ptr d;
		for(auto i = dirs.begin(); i != dirs.end(); ++i) {
			d = i->second;
			if(d->getParent()) {
				dir = d->getParent()->getRealPath(d->getName());
				if(dir[dir.size() -1] != '\\') 
					dir += "\\";
				ret.push_back( dir );
			} else {
				dir = d->getRoot()->getPath();
				if(dir.empty())
					return;

				if(dir[dir.size() -1] != '\\') 
					dir += "\\";
					
				ret.push_back( dir );
			}
		}
	} else { //its a file
		ret.push_back(findFile(path, ClientList())->getRealPath());
	}
}
string ShareManager::validateVirtual(const string& aVirt) const noexcept {
	string tmp = aVirt;
	string::size_type idx = 0;

	while( (idx = tmp.find_first_of("\\/"), idx) != string::npos) {
		tmp[idx] = '_';
	}
	return tmp;
}

bool ShareManager::hasVirtual(const string& virtualName) const noexcept {
	RLock l(cs);
	return getByVirtual(virtualName, ClientList()) != Dirs();
}

void ShareManager::load(SimpleXML& aXml) {
	WLock l(cs);

	aXml.resetCurrentChild();
	if(aXml.findChild("Share")) {
		aXml.stepIn();
		while(aXml.findChild("Directory")) {
			string realPath = aXml.getChildData();
			if(realPath.empty()) {
				continue;
			}
			// make sure realPath ends with a PATH_SEPARATOR
			if(realPath[realPath.size() - 1] != PATH_SEPARATOR) {
				realPath += PATH_SEPARATOR;
			}
						
		//	if(!Util::fileExists(realPath))
		//		continue;

			const string& virtualName = aXml.getChildAttrib("Virtual");
			string vName = validateVirtual(virtualName.empty() ? Util::getLastDir(realPath) : virtualName);
			shares.insert(std::make_pair(realPath, vName));
			if(directories.find(realPath) == directories.end()) {
				Directory::Ptr dp = Directory::create(vName);
				Directory::RootDirectory* root = new Directory::RootDirectory(realPath);
				dp->setRoot(root);
				directories.insert(std::make_pair(realPath, dp));
			}
		}
		aXml.stepOut();
	}
	if(aXml.findChild("NoShare")) {
		aXml.stepIn();
		while(aXml.findChild("Directory"))
			notShared.push_back(aXml.getChildData());
	
		aXml.stepOut();
	}
	if(aXml.findChild("incomingDirs")) {
		aXml.stepIn();
		while(aXml.findChild("incoming"))
			incoming.push_back(aXml.getChildData());
	
		aXml.stepOut();
	}
}

static const string SDIRECTORY = "Directory";
static const string SFILE = "File";
static const string SNAME = "Name";
static const string SSIZE = "Size";
static const string STTH = "TTH";
static const string PATH = "Path";
static const string DATE = "Date";

struct ShareLoader : public SimpleXMLReader::CallBack {
	ShareLoader(ShareManager::DirMap& aDirs) : dirs(aDirs), cur(0), depth(0) { }
	void startTag(const string& name, StringPairList& attribs, bool simple) {

		
		if(name == SDIRECTORY) {
			const string& name = getAttrib(attribs, SNAME, 0);
			string path = getAttrib(attribs, PATH, 1);
			const string& date = getAttrib(attribs, DATE, 2);

			if(path[path.length() - 1] != PATH_SEPARATOR)
				path += PATH_SEPARATOR;


			if(!name.empty()) {
				if(depth == 0) {
						ShareManager::DirMap::const_iterator i = dirs.find(path); 
						if(i != dirs.end()) {
							cur = i->second;
							cur->setLastWrite(Util::toUInt32(date));
							lastFileIter = cur->files.begin();
						}
				} else if(cur) {
					cur = ShareManager::Directory::create(name, cur);
					cur->setLastWrite(Util::toUInt32(date));
					cur->getParent()->directories[cur->getName()] = cur;
					lastFileIter = cur->files.begin();
					try {
					ShareManager::getInstance()->addReleaseDir(cur->getFullName());
					}catch(...) { }
				}
			}

			if(simple) {
				if(cur) {
					cur = cur->getParent();
					if(cur)
						lastFileIter = cur->files.begin();
				}
			} else {
				depth++;
			}
		} else if(cur && name == SFILE) {
			const string& fname = getAttrib(attribs, SNAME, 0);
			const string& size = getAttrib(attribs, SSIZE, 1);   
			if(fname.empty() || size.empty() ) {
				dcdebug("Invalid file found: %s\n", fname.c_str());
				return;
			}
			/*dont save TTHs, check them from hashmanager, just need path and size.
			this will keep us sync to hashindex */
			try {
				lastFileIter = cur->files.insert(lastFileIter, ShareManager::Directory::File(fname, Util::toInt64(size), cur, HashManager::getInstance()->getTTH(cur->getRealPath(fname, false), Util::toInt64(size))));
			}catch(Exception& e) { 
				dcdebug("Error loading filelist %s \n", e.getError().c_str());
			}
		}
	}
	void endTag(const string& name, const string&) {
		if(name == SDIRECTORY) {
			depth--;
			if(cur) {
				cur = cur->getParent();
				if(cur)
					lastFileIter = cur->files.begin();
			}
		}
	}

private:
	ShareManager::DirMap& dirs;

	ShareManager::Directory::File::Set::iterator lastFileIter;
	ShareManager::Directory::Ptr cur;
	size_t depth;
};

bool ShareManager::loadCache() {
	try {
		{
			WLock l(cs);
			ShareLoader loader(directories);
			//look for shares.xml
			dcpp::File ff(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml", dcpp::File::READ, dcpp::File::OPEN, false);
			SimpleXMLReader(&loader).parse(ff);
			for(DirMap::const_iterator i = directories.begin(); i != directories.end(); ++i) {
				updateIndices(*i->second, *i->second->getRoot());
			}
			totalShareSize = updateSizes();
		} //lock free

		sortReleaseList();
	}catch(SimpleXMLException& e) {
		LogManager::getInstance()->message("Error Loading shares.xml: "+ e.getError(), LogManager::LOG_ERROR);
		return false;
	} catch(...) {
		return false;
	}

	try { //not vital to our cache loading.
			FileList* fl; 
			{
				RLock l(cs);
				fl = getFileList(NULL);
			}
			fl->setBZXmlFile( Util::getPath(Util::PATH_USER_CONFIG) + "files.xml.bz2");
			if(!Util::fileExists(fl->getBZXmlFile())) {  //only generate if we dont find old filelist
				generateXmlList(NULL, true);
			} else {
				fl->bzXmlRef = unique_ptr<File>(new File(fl->getBZXmlFile(), File::READ, File::OPEN));
			}
		} catch(...) { }

	return true;
}

void ShareManager::save(SimpleXML& aXml) {
	RLock l(cs);

	aXml.addTag("Share");
	aXml.stepIn();
	for(StringMapIter i = shares.begin(); i != shares.end(); ++i) {
		aXml.addTag("Directory", i->first);
		aXml.addChildAttrib("Virtual", i->second);
		
	}
	aXml.stepOut();
	aXml.addTag("NoShare");
	aXml.stepIn();
	for(StringIter j = notShared.begin(); j != notShared.end(); ++j) {
		aXml.addTag("Directory", *j);
	}
	aXml.stepOut();
	
	aXml.addTag("incomingDirs");
	aXml.stepIn();
	for(StringIter k = incoming.begin(); k != incoming.end(); ++k) {
		aXml.addTag("incoming", *k); //List Vname as incoming
	}
	aXml.stepOut();

} 

void ShareManager::addDirectory(const string& realPath, const string& virtualName)  {
//	if(!Util::fileExists(realPath))
//		return;

	if(realPath.empty() || virtualName.empty()) {
		throw ShareException(STRING(NO_DIRECTORY_SPECIFIED));
	}

	if (!checkHidden(realPath)) {
		throw ShareException(STRING(DIRECTORY_IS_HIDDEN));
	}

	if(stricmp(SETTING(TEMP_DOWNLOAD_DIRECTORY), realPath) == 0) {
		throw ShareException(STRING(DONT_SHARE_TEMP_DIRECTORY));
	}

#ifdef _WIN32
	//need to throw here, so throw the error and dont use airutil
	TCHAR path[MAX_PATH];
	::SHGetFolderPath(NULL, CSIDL_WINDOWS, NULL, SHGFP_TYPE_CURRENT, path);
	string windows = Text::fromT((tstring)path) + PATH_SEPARATOR;
	// don't share Windows directory
	if(strnicmp(realPath, windows, windows.length()) == 0) {
		char buf[MAX_PATH];
		snprintf(buf, sizeof(buf), CSTRING(CHECK_FORBIDDEN), realPath.c_str());
		throw ShareException(buf);
	}
#endif

	list<string> removeMap;
	{
		RLock l(cs);
		
		StringMap a = shares;
		for(StringMapIter i = a.begin(); i != a.end(); ++i) {
			if(strnicmp(realPath, i->first, i->first.length()) == 0) {
				// Trying to share an already shared directory
				removeMap.push_front(i->first);
			} else if(strnicmp(realPath, i->first, realPath.length()) == 0) {
				// Trying to share a parent directory
				removeMap.push_front(i->first);	
			}
		}
	}

	for(list<string>::const_iterator i = removeMap.begin(); i != removeMap.end(); i++) {
		removeDirectory(*i);
	}
	
	HashManager::HashPauser pauser;	
	
	Directory::Ptr dp = buildTree(realPath, Directory::Ptr(), true);
	string vName = validateVirtual(virtualName);
	dp->setName(vName);
	ShareManager::Directory::RootDirectory* root = new ShareManager::Directory::RootDirectory(realPath);
	dp->setRoot(root);
	dp->setLastWrite(findLastWrite(realPath));

	{
		WLock l(cs);

		shares.insert(std::make_pair(realPath, vName));
		directories.insert(make_pair(realPath, dp));
		updateIndices(*dp, *root);

		setDirty(true);
	}
		//after the wlock on purpose, these have own locking
		dp->findDirsRE(false);
		sortReleaseList();

}

void ShareManager::removeDirectory(const string& realPath) {
	if(realPath.empty())
		return;

	HashManager::getInstance()->stopHashing(realPath);
	{
		WLock l(cs);

		StringMapIter i = shares.find(realPath);
		if(i == shares.end()) {
			return;
		}
		shares.erase(i);

		DirMap::iterator j = directories.find(realPath);
		if(j == directories.end())
			return;
	
		j->second->findDirsRE(true);
		directories.erase(j);
		rebuildIndices();
		setDirty(true);
	}
	sortReleaseList();

}

void ShareManager::renameDirectory(const string& realPath, const string& virtualName)  {

	WLock l(cs);
	string vName = validateVirtual(virtualName);
	
	StringMapIter i = shares.find(realPath);
	if(i == shares.end()) {
		return;
	}
	shares.erase(i);
	shares.insert(make_pair(realPath, vName));

	DirMap::iterator j = directories.find(realPath);
	if(j == directories.end())
		return;

	j->second->setName(vName);
	setDirty(true);
}

ShareManager::Dirs ShareManager::getByVirtual(const string& virtualName, const ClientList& clients) const throw() {
	if(virtualName.empty())
		return Dirs();
	
	Dirs temp;

	for(DirMap::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		if(isExcluded(i->first, clients))
			continue;

		if(stricmp(i->second->getName(), virtualName) == 0) {
			temp.push_back(i->second);
		}
	}
	if(!temp.empty())
		return temp;

	return Dirs();
}

int64_t ShareManager::getShareSize(const string& realPath) const noexcept {
	RLock l(cs);
	
	/*maybe too much but we are asking for every root folder in every case, so might aswell update them*/
	if(updateSize)  
		updateSizes();

	DirMap::const_iterator j = directories.find(realPath);
	if(j != directories.end()) {
		return j->second->getRoot()->getSize();
	}
	return -1;

}

int64_t ShareManager::updateSizes() const{
	int64_t tmp = 0;
	int64_t total = 0;
	Directory::RootDirectory* root;
	
	for(auto m = directories.begin(); m != directories.end(); ++m) {
		tmp = 0;
		root = m->second->getRoot();

		for(auto i = root->tthIndex.begin(); i != root->tthIndex.end(); ++i) {
			tmp += i->second->getSize();
		}
		total += tmp;
		root->setSize(tmp);
	}
	updateSize = false;
	return total;
}

int64_t ShareManager::getShareSize(Client* client) const noexcept {
	RLock l(cs);
	
	if(updateSize)   // the client that hits here first will update the directory sizes.
		totalShareSize = updateSizes();

	if(!client || client->getUnShared().empty()) // nmdc hub or just no excluded share folders, we can return full share size.
		return totalShareSize;

	int64_t tmp = 0;
	for(auto m = directories.begin(); m != directories.end(); ++m) {
		if(std::binary_search(client->getUnShared().begin(), client->getUnShared().end(), m->first))
			continue;
		tmp += m->second->getRoot()->getSize();
	}
	
	return tmp;
}

size_t ShareManager::getSharedFiles() const noexcept {
	RLock l(cs);
	size_t tmp = 0;

	for(auto m = directories.begin(); m != directories.end(); ++m)
		tmp += m->second->getRoot()->tthIndex.size();

	return tmp;
}

bool ShareManager::isDirShared(const string& directory) {

	string dir = AirUtil::getReleaseDir(directory);
	if (dir.empty())
		return false;
	/*we need to protect this, but same mutex used here is too much, it will freeze during refreshing even with chat releasenames*/
	Lock l(dirnamelist);
	if (std::binary_search(dirNameList.begin(), dirNameList.end(), dir)) {
		return true;
	}
		
	return false;
}

bool ShareManager::isFileShared(const TTHValue aTTH, const string& fileName) {
	RLock l (cs);

	for(auto m = directories.begin(); m != directories.end(); ++m) {
		auto files = m->second->getRoot()->tthIndex.equal_range(const_cast<TTHValue*>(&aTTH));
		for(auto i = files.first; i != files.second; ++i) {
			if(stricmp(fileName.c_str(), i->second->getName().c_str()) == 0) {
				return true;
			}
		}
	}
	return false;
}

tstring ShareManager::getDirPath(const string& directory, bool validateDir) {
	string dir = directory;
	if (validateDir) {
		dir = AirUtil::getReleaseDir(directory);
	if (dir.empty())
 		return Util::emptyStringT;
 	}
 	
 	string found = Util::emptyString;
 	string dirNew;
 	for(auto j = directories.begin(); j != directories.end(); ++j) {
		dirNew = j->second->getFullName();
 	if (validateDir) {
 		dirNew = AirUtil::getReleaseDir(dirNew);
 	}
 	
 	if (!dirNew.empty()) {
 		if (dir == dirNew) {
 			found=dirNew;
 			break;
 			}
 		}
 	found = j->second->find(dir, validateDir);
 	if(!found.empty())
 		break;
 	}
 	
 	if (found.empty())
 		return Util::emptyStringT;
 	
 	StringList ret;
 	try {
 		getRealPaths(Util::toAdcFile(found), ret);
 	} catch(const ShareException&) {
 		return Util::emptyStringT;
 	}
 	
 	if (!ret.empty()) {
 		return Text::toT(ret[0]);
 	}
 	
	return Util::emptyStringT;
 }


string ShareManager::Directory::find(const string& dir, bool validateDir) {
	string ret = Util::emptyString;
	string dirNew = dir;
	if (validateDir)
		dirNew = AirUtil::getReleaseDir(getFullName());

	if (!dirNew.empty()) {
		if (dir == dirNew) {
			return getFullName();
		}
	}

	for(auto l = directories.begin(); l != directories.end(); ++l) {
		ret = l->second->find(dir, validateDir);
		if(!ret.empty())
			break;
	}
	return ret;
}


void ShareManager::sortReleaseList() {
	Lock l(dirnamelist);
	sort(dirNameList.begin(), dirNameList.end());
}

void ShareManager::Directory::findDirsRE(bool remove) {
	for(auto l = directories.begin(); l != directories.end(); ++l) {
		 l->second->findDirsRE(remove);
	}

	if (remove) {
		ShareManager::getInstance()->deleteReleaseDir(this->getFullName());
	} else {
		ShareManager::getInstance()->addReleaseDir(this->getFullName());
	}
}

void ShareManager::addReleaseDir(const string& aName) {
	string dir = AirUtil::getReleaseDir(aName);
	if (dir.empty())
		return;

	Lock l(dirnamelist);
	dirNameList.push_back(dir);
}

void ShareManager::deleteReleaseDir(const string& aName) {

	string dir = AirUtil::getReleaseDir(aName);
	if (dir.empty())
		return;

//	hmm, dont see a situation when the name list could change during remove looping.
	for(StringList::const_iterator i = dirNameList.begin(); i != dirNameList.end(); ++i) {
		if ((*i) == dir) {
			Lock l(dirnamelist);
			dirNameList.erase(i);
			return;
		}
	}
}

ShareManager::Directory::Ptr ShareManager::buildTree(const string& aName, const Directory::Ptr& aParent, bool checkQueued /*false*/, bool create/*true*/) {
	Directory::Ptr dir;

	if(create)
		dir = Directory::create(Util::getLastDir(aName), aParent);
	else
		dir = aParent;
	

	Directory::File::Set::iterator lastFileIter = dir->files.begin();

	FileFindIter end;


#ifdef _WIN32
	for(FileFindIter i(aName + "*"); i != end && !aShutdown; ++i) {
#else
	//the fileiter just searches directorys for now, not sure if more 
	//will be needed later
	//for(FileFindIter i(aName + "*"); i != end; ++i) {
	for(FileFindIter i(aName); i != end; ++i) {
#endif
		string name = i->getFileName();
		if(name.empty()) {
			LogManager::getInstance()->message("Invalid file name found while hashing folder "+ aName + ".", LogManager::LOG_WARNING);
			return false;
		}

		if(!BOOLSETTING(SHARE_HIDDEN) && i->isHidden())
			continue;

		if(i->isDirectory()) {
			string newName = aName + name + PATH_SEPARATOR;

			if (!AirUtil::checkSharedName(newName, true)) {
				continue;
			}

			//check queue so we dont add incomplete stuff to share.
			if(checkQueued) {
				if (std::binary_search(bundleDirs.begin(), bundleDirs.end(), Text::toLower(newName))) {
					continue;
				}
			}

			if(shareFolder(newName)) {
				Directory::Ptr tmpDir = buildTree(newName, dir, checkQueued);
				tmpDir->setLastWrite(i->getLastWriteTime()); //add the date starting from the first found directory.
				dir->directories[name] = tmpDir;
			}
		} else {
			// Not a directory, assume it's a file...
			string path = aName + name;
			int64_t size = i->getSize();

			if (!AirUtil::checkSharedName(path, false, true, size)) {
				continue;
			}

			try {
				if(HashManager::getInstance()->checkTTH(path, size, i->getLastWriteTime())) 
					lastFileIter = dir->files.insert(lastFileIter, Directory::File(name, size, dir, HashManager::getInstance()->getTTH(path, size)));
			} catch(const HashException&) {
			}
		}
	}
	return dir;
}

bool ShareManager::checkHidden(const string& aName) const {
	FileFindIter ff = FileFindIter(aName.substr(0, aName.size() - 1));

	if (ff != FileFindIter()) {
		return (BOOLSETTING(SHARE_HIDDEN) || !ff->isHidden());
	}

	return true;
}

uint32_t ShareManager::findLastWrite(const string& aName) const {
	FileFindIter ff = FileFindIter(aName.substr(0, aName.size() - 1));

	if (ff != FileFindIter()) {
		return ff->getLastWriteTime();
	}

	return 0;
}

void ShareManager::updateIndices(Directory& dir, Directory::RootDirectory& root) {
	bloom.add(Text::toLower(dir.getName()));
//reset the size to avoid increasing the share size
	//on every refresh.
	dir.size = 0;
	for(Directory::MapIter i = dir.directories.begin(); i != dir.directories.end(); ++i) {
		updateIndices(*i->second, root);
	}

	dir.size = 0;

	for(Directory::File::Set::iterator i = dir.files.begin(); i != dir.files.end(); ) {
		updateIndices(dir, i++, root);
	}
}

void ShareManager::rebuildIndices() {
	sharedSize = 0;
	bloom.clear();

	for(DirMap::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		i->second->getRoot()->tthIndex.clear();
		updateIndices(*i->second, *i->second->getRoot()); //supply root here or get it afterwards?
	}

}

void ShareManager::updateIndices(Directory& dir, const Directory::File::Set::iterator& i, Directory::RootDirectory& root) {

	auto files = root.tthIndex.equal_range(const_cast<TTHValue*>(&i->getTTH()));
	for(auto k = files.first; k != files.second; ++k) {
		if(stricmp((*i).getRealPath(false), k->second->getRealPath(false)) == 0) {
			return;
		}
	}

	const Directory::File& f = *i;

	dir.size+=f.getSize();
	sharedSize += f.getSize();

	dir.addType(getType(f.getName()));

	root.tthIndex.insert(make_pair(const_cast<TTHValue*>(&f.getTTH()), i));
	bloom.add(Text::toLower(f.getName()));
}

int ShareManager::refresh( const string& aDir ){
	int result = REFRESH_PATH_NOT_FOUND;

	if(refreshing.test_and_set()) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_IN_PROGRESS), LogManager::LOG_INFO);
		return REFRESH_IN_PROGRESS;
	}
	string path = aDir;

	if(path[ path.length() -1 ] != PATH_SEPARATOR)
		path += PATH_SEPARATOR;

	{
		RLock l(cs);
		refreshPaths.clear();
			
		DirMap::iterator i = directories.find(path); //case insensitive
		if(i == directories.end()) {
			//loopup the Virtualname selected from share and add it to refreshPaths List
			for(StringMap::const_iterator j = shares.begin(); j != shares.end(); ++j) {
				if( stricmp( j->second, aDir ) == 0 ) {
					refreshPaths.push_back( j->first );
					result = REFRESH_STARTED;
				}
			}
		} else {
			refreshPaths.push_back(i->first);
			result = REFRESH_STARTED;
		}
	}

	if(result == REFRESH_STARTED)
		result = initRefreshThread(ShareManager::REFRESH_DIRECTORY | ShareManager::REFRESH_UPDATE);
		
	if(result == REFRESH_PATH_NOT_FOUND)
		refreshing.clear();

	return result;
}


int ShareManager::refresh(int aRefreshOptions){

	if(refreshing.test_and_set()) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_IN_PROGRESS), LogManager::LOG_INFO);
		return REFRESH_IN_PROGRESS;
	}
	int result = REFRESH_STARTED;

	if(aRefreshOptions & REFRESH_INCOMING) {
		result = REFRESH_PATH_NOT_FOUND;
		{
			RLock l(cs);  
			refreshPaths.clear();

			lastIncomingUpdate = GET_TICK();
			for(StringIter d = incoming.begin(); d != incoming.end(); ++d) {
			
				std::string realpath = *d;
				DirMap::const_iterator i = directories.find(realpath);
				if(i != directories.end()) {
					refreshPaths.push_back( i->first ); //add all matching realpaths to refreshpaths
					result = REFRESH_STARTED;
				}
			}
	
		}
		if(result == REFRESH_PATH_NOT_FOUND)
			refreshing.clear();
	}
	
	if(result == REFRESH_STARTED)
		initRefreshThread(aRefreshOptions);
	
	return result;
}
int ShareManager::initRefreshThread(int aRefreshOptions)  {
	
	refreshOptions = aRefreshOptions;
	join();

	try {
		start();
		if(refreshOptions & REFRESH_BLOCKING) { 
			join();
		} else {
			setThreadPriority(Thread::LOW);
		}

	} catch(const ThreadException& e) {
		LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FAILED) + " " + e.getError(), LogManager::LOG_WARNING);
		refreshing.clear();
	}

	return REFRESH_STARTED;
}

StringPairList ShareManager::getDirectories(int refreshOptions) const noexcept {
	RLock l(cs);
	StringPairList ret;
	if(refreshOptions & REFRESH_ALL) {
		for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i) {
			ret.push_back(make_pair(i->second, i->first));
		}
	} else if(refreshOptions & REFRESH_DIRECTORY){
		for(StringIterC j = refreshPaths.begin(); j != refreshPaths.end(); ++j) {
			std::string bla = *j;
			// lookup in share the realpaths for refreshpaths
			ret.push_back(make_pair(shares.find(bla)->second, bla));
		}
	}
	return ret;
}

int ShareManager::run() {
	
	StringPairList dirs = getDirectories(refreshOptions);

	string msg;
	if(refreshOptions & REFRESH_ALL) {
		lastFullUpdate = GET_TICK();
		msg = STRING(FILE_LIST_REFRESH_INITIATED);
	} else if (refreshOptions & REFRESH_INCOMING) {
		msg = STRING(FILE_LIST_REFRESH_INITIATED_INCOMING);
	} else if (refreshOptions & REFRESH_DIRECTORY) {
		if (dirs.size() == 1) {
			msg = str(boost::format(STRING(FILE_LIST_REFRESH_INITIATED_RPATH)) % dirs.front().second);
		} else {
			if(find_if(dirs.begin(), dirs.end(), [dirs](pair<string,string>& dp) { return dp.first != dirs.front().first; }) == dirs.end()) {
				msg = str(boost::format(STRING(FILE_LIST_REFRESH_INITIATED_VPATH)) % dirs.front().first);
			} else {
				msg = str(boost::format(STRING(FILE_LIST_REFRESH_INITIATED_X_VPATH)) % dirs.size());
			}
		}
	}

	HashManager::HashPauser pauser;
	
	if (!msg.empty())
		LogManager::getInstance()->message(msg, LogManager::LOG_INFO);

	bundleDirs.clear();
	QueueManager::getInstance()->getForbiddenPaths(bundleDirs, dirs);
	DirMap newDirs;

	for(StringPairIter i = dirs.begin(); i != dirs.end(); ++i) {
		if (checkHidden(i->second)) {
			Directory::Ptr dp = buildTree(i->second, Directory::Ptr(), true);
			if(aShutdown) goto end;  //abort refresh
			dp->setName(i->first);
			ShareManager::Directory::RootDirectory* root = new ShareManager::Directory::RootDirectory(i->second);
			dp->setRoot(root);
			dp->setLastWrite(findLastWrite(i->second));
			newDirs.insert(make_pair(i->second, dp));
		}
	}

	{		
		WLock l(cs);

		//only go here when needed
		if(refreshOptions & REFRESH_DIRECTORY){ 
		
			for(StringPairIter i = dirs.begin(); i != dirs.end(); ++i) {
				DirMap::const_iterator m = directories.find(i->second);
				if(m != directories.end()) { 
					m->second->findDirsRE(true);
					directories.erase(m);
				}
			}

		} else if(refreshOptions & REFRESH_ALL) {
			directories.clear();
			Lock l(dirnamelist);
			dirNameList.clear();
		}
		
		directories.insert(newDirs.begin(), newDirs.end());
				
		rebuildIndices();
		setDirty(true);  //forceXmlRefresh
	}
	
	for(DirMap::iterator i = newDirs.begin(); i != newDirs.end(); ++i)
		i->second->findDirsRE(false);
			
	sortReleaseList();
	
	if(refreshOptions & REFRESH_UPDATE) {
		ClientManager::getInstance()->infoUpdated();
	}

	if(refreshOptions & REFRESH_BLOCKING) {
		generateXmlList(NULL, true);
		saveXmlList();
	}
	LogManager::getInstance()->message(STRING(FILE_LIST_REFRESH_FINISHED), LogManager::LOG_INFO);

end:
	bundleDirs.clear();
	refreshing.clear();
	return 0;
}
		
void ShareManager::getBloom(ByteVector& v, size_t k, size_t m, size_t h) const {
	dcdebug("Creating bloom filter, k=%u, m=%u, h=%u\n", k, m, h);
	WLock l(cs);
	
	HashBloom bloom;
	bloom.reset(k, m, h);
	for(auto m = directories.begin(); m != directories.end(); ++m) {
		for(auto i = m->second->getRoot()->tthIndex.begin(); i != m->second->getRoot()->tthIndex.end(); ++i) {
			bloom.add(*i->first);
		}
	}
	bloom.copy_to(v);
}

//forwards the calls to createFileList for creating the filelist that was reguested.
ShareManager::FileList* ShareManager::generateXmlList(Client* client, bool forced /*false*/) {
	string flname = "";
	FileList* fl = NULL;
	if(client && !client->getUnShared().empty()) {

		flname = AirUtil::stripHubUrl(client->getHubUrl());
		{
			WLock l(cs);
			auto i = fileLists.find(flname);
			if(i == fileLists.end()) {//no filelist yet, create one.
				fl = new FileList(flname);
				fileLists.insert(make_pair(flname, fl));
			} else {
				fl = i->second;
			}
		}//lock freed!

		Lock c(client->cs);  //lock we cannot create the same filelist 2 times, but we want to be able to generate other lists simultaniously, a bit overkill? eh :)
		createFileList(client, fl, flname, forced);
		return fl;
	} else { // no hub or no excludefolders, use full list
		{
			RLock l(cs);
			fl = fileLists.find(FileListALL)->second; 
		}
		if(!forced) {
			if(GeneratingFULLXmlList.test_and_set())
				return fl;
		}
		createFileList(NULL, fl, flname, forced);
		GeneratingFULLXmlList.clear();
		return fl;
	}
}

void ShareManager::createFileList(Client* client, FileList* fl, const string& flname, bool forced) {
	
	if(forced && fl->xmlDirty || fl->forceXmlRefresh || (fl->xmlDirty && (fl->getlastXmlUpdate() + 15 * 60 * 1000 < GET_TICK() || fl->getlastXmlUpdate() < lastFullUpdate))) {

		fl->listN++;

		try {

			string newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files" + Util::toString(fl->listN) + flname + ".xml.bz2";
			SimpleXML xml;
			xml.addTag("FileListing");
			xml.addChildAttrib("Version", 1);
			xml.addChildAttrib("CID", ClientManager::getInstance()->getMe()->getCID().toBase32());
			xml.addChildAttrib("Base", string("/"));
			xml.addChildAttrib("Generator", string(APPNAME " " VERSIONSTRING));
			xml.stepIn();
			{
				RLock l(cs);
				for(DirMap::const_iterator i = directories.begin(); i != directories.end(); ++i) {
					if(isHubExcluded(i->first, client)) continue;
						i->second->toXml(xml, true);
				}
			}
			{
				File f(newXmlName, File::WRITE, File::TRUNCATE | File::CREATE);
				// We don't care about the leaves...
				CalcOutputStream<TTFilter<1024*1024*1024>, false> bzTree(&f);
				FilteredOutputStream<BZFilter, false> bzipper(&bzTree);
				CountOutputStream<false> count(&bzipper);
				CalcOutputStream<TTFilter<1024*1024*1024>, false> newXmlFile(&count);
			
				xml.stepOut();
				newXmlFile.write(SimpleXML::utf8Header);

				xml.toXML(&newXmlFile);
				newXmlFile.flush();

				fl->setxmlListLen(count.getCount());

				newXmlFile.getFilter().getTree().finalize();
				bzTree.getFilter().getTree().finalize();
	
				fl->setxmlRoot(newXmlFile.getFilter().getTree().getRoot());
				fl->setbzXmlRoot(bzTree.getFilter().getTree().getRoot());
			}

			string emptyXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "Emptyfiles.xml.bz2"; // Hide Share Mod
			if(!Util::fileExists(emptyXmlName)) {
				FilteredOutputStream<BZFilter, true> emptyXmlFile(new File(emptyXmlName, File::WRITE, File::TRUNCATE | File::CREATE));
				emptyXmlFile.write(SimpleXML::utf8Header);
				emptyXmlFile.write("<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"/\" Generator=\"DC++ " DCVERSIONSTRING "\">\r\n"); // Hide Share Mod
				emptyXmlFile.write("</FileListing>");
				emptyXmlFile.flush();
			}

			if(fl->bzXmlRef.get()) {
				fl->bzXmlRef.reset();
				File::deleteFile(fl->getBZXmlFile());
			}

			try {
				File::renameFile(newXmlName, Util::getPath(Util::PATH_USER_CONFIG) + "files" + flname + ".xml.bz2");
				newXmlName = Util::getPath(Util::PATH_USER_CONFIG) + "files" + flname + ".xml.bz2";
			} catch(const FileException& e) {
				dcdebug("error renaming filelist: ", e.getError());
				// Ignore, this is for caching only...
			}
			fl->bzXmlRef = unique_ptr<File>(new File(newXmlName, File::READ, File::OPEN));
			fl->setBZXmlFile(newXmlName);
			fl->setbzXmlListLen(File::getSize(newXmlName));

			//cleanup old filelists we failed to delete before due to uploading them.
			StringList lists = File::findFiles(Util::getPath(Util::PATH_USER_CONFIG), "files?*.xml.bz2");
			std::for_each(lists.begin(), lists.end(), File::deleteFile);

		} catch(const Exception&) {
			// No new file lists...
		}
		fl->xmlDirty = false;
		fl->forceXmlRefresh = false;
		fl->setlastXmlUpdate(GET_TICK());

	}
}

#define LITERAL(n) n, sizeof(n)-1

void ShareManager::saveXmlList(bool verbose /* false */) {

	if(xml_saving)
		return;

	xml_saving = true;

	RLock l(cs);
	string indent;
	try {
		//create a backup first incase we get interrupted on creation.
		string newCache = Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml.tmp";
		File ff(newCache, File::WRITE, File::TRUNCATE | File::CREATE);
		BufferedOutputStream<false> xmlFile(&ff);
	
		xmlFile.write(SimpleXML::utf8Header);
		xmlFile.write(LITERAL("<Share Version=\"" SHARE_CACHE_VERSION "\">\r\n"));
		indent +='\t';

		for(DirMap::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			i->second->toXmlList(xmlFile, i->first, indent);
		}

		xmlFile.write(LITERAL("</Share>"));
		xmlFile.flush();
		ff.close();
		File::deleteFile(Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml");
		File::renameFile(newCache,  (Util::getPath(Util::PATH_USER_CONFIG) + "Shares.xml"));
	}catch(Exception& e){
		LogManager::getInstance()->message("Error Saving Shares.xml: " + e.getError(), LogManager::LOG_WARNING);
	}

	//delete xmlFile;
	xml_saving = false;
	ShareCacheDirty = false;
	lastSave = GET_TICK();
	if (verbose || BOOLSETTING(SHOW_USELESS_SPAM))
		LogManager::getInstance()->message("shares.xml saved.", LogManager::LOG_INFO);
}

void ShareManager::Directory::toXmlList(OutputStream& xmlFile, const string& path, string& indent){
	string tmp, tmp2;
	
	xmlFile.write(indent);
	xmlFile.write(LITERAL("<Directory Name=\""));
	xmlFile.write(SimpleXML::escape(name, tmp, true));
	xmlFile.write(LITERAL("\" Path=\""));
	xmlFile.write(SimpleXML::escape(path, tmp, true));
	xmlFile.write(LITERAL("\" Date=\""));
	xmlFile.write(SimpleXML::escape(Util::toString(lastwrite), tmp, true));
	xmlFile.write(LITERAL("\">\r\n"));

	indent += '\t';
	for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i) {
		if(path[ path.length() -1 ] == PATH_SEPARATOR )
			i->second->toXmlList(xmlFile, path + i->first, indent);
		else
		i->second->toXmlList(xmlFile, path + PATH_SEPARATOR + i->first, indent);
	}

	for(Directory::File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;

		xmlFile.write(indent);
		xmlFile.write(LITERAL("<File Name=\""));
		xmlFile.write(SimpleXML::escape(f.getName(), tmp2, true));
		xmlFile.write(LITERAL("\" Size=\""));
		xmlFile.write(Util::toString(f.getSize()));
		xmlFile.write(LITERAL("\"/>\r\n"));
	}

	indent.erase(indent.length()-1);
	xmlFile.write(indent);
	xmlFile.write(LITERAL("</Directory>\r\n"));
}

MemoryInputStream* ShareManager::generateTTHList(const string& dir, bool recurse, bool isInSharingHub, const HintedUser& aUser) {
	
	if(!isInSharingHub)
		return NULL;
	
	string tths;
	string tmp;
	StringOutputStream sos(tths);

	try{
		ClientList clients;
		if(!aUser.user->isNMDC()) {
			ClientManager::getInstance()->ListClients(aUser.user, clients);
		}
		RLock l(cs);
		DirMultiMap result = findVirtuals(dir, clients); 
		for(DirMultiMap::const_iterator it = result.begin(); it != result.end(); ++it) {
			dcdebug("result name %s \n", it->second->getName());
			it->second->toTTHList(sos, tmp, recurse);
		}
	} catch(...) {
		return NULL;
	}

	if (tths.size() == 0) {
		dcdebug("Partial NULL");
		return NULL;
	} else {
		//LogManager::getInstance()->message(tths);
		return new MemoryInputStream(tths);
	}
}

void ShareManager::Directory::toTTHList(OutputStream& tthList, string& tmp2, bool recursive) {
	dcdebug("toTTHList2");
	if (recursive) {
		for(auto i = directories.begin(); i != directories.end(); ++i) {
			i->second->toTTHList(tthList, tmp2, recursive);
		}
	}
	for(Directory::File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;
		tmp2.clear();
		tthList.write(f.getTTH().toBase32(tmp2));
		tthList.write(LITERAL(" "));
	}
}

MemoryInputStream* ShareManager::generatePartialList(const string& dir, bool recurse, bool isInSharingHub, const HintedUser& aUser, const string& userSID) {
	if(dir[0] != '/' || dir[dir.size()-1] != '/')
		return 0;
	

	if(!isInSharingHub) {
		string xml = SimpleXML::utf8Header;
		string tmp;
		xml += "<FileListing Version=\"1\" CID=\"" + ClientManager::getInstance()->getMe()->getCID().toBase32() + "\" Base=\"" + SimpleXML::escape(dir, tmp, false) + "\" Generator=\"" APPNAME " " VERSIONSTRING "\">\r\n";
		StringOutputStream sos(xml);
		xml += "</FileListing>";
		return new MemoryInputStream(xml);
	 }

	string xml;
	xml = SimpleXML::utf8Header;
	string basedate = Util::emptyString;

	SimpleXML sXml;   //use simpleXML so we can easily add the end tags and check what virtuals have been created.
	sXml.addTag("FileListing");
	sXml.addChildAttrib("Version", 1);
	sXml.addChildAttrib("CID", ClientManager::getInstance()->getMe()->getCID().toBase32());
	sXml.addChildAttrib("Base", dir);
	sXml.addChildAttrib("Generator", string(APPNAME " " VERSIONSTRING));
	sXml.stepIn();

	if(dir == "/") {
		Client* client = NULL; 
		if(!aUser.user->isNMDC()) {
			client = ClientManager::getInstance()->findClient(aUser, userSID);
		}

		if(aUser.hint.empty() && !aUser.user->isNMDC() && !client) //debug info.
			LogManager::getInstance()->message("generatePartialList() hint url empty!! Report this to Night, User: " + Util::toString(ClientManager::getInstance()->getNicks(aUser)), LogManager::LOG_WARNING);

		RLock l(cs);
		for(DirMap::const_iterator i = directories.begin(); i != directories.end(); ++i) {
			if(isHubExcluded(i->first, client)) //check only from the hub the reguest came from.
				continue;

			i->second->toXml(sXml, recurse);
		}
	} else {
		dcdebug("wanted %s \n", dir);
		try {
			ClientList clients;
			if(!aUser.user->isNMDC()) {
				ClientManager::getInstance()->ListClients(aUser.user, clients);
			}
			RLock l(cs);
			DirMultiMap result = findVirtuals(dir, clients); 
			Directory::Ptr root;
			for(DirMultiMap::const_iterator it = result.begin(); it != result.end(); ++it) {
				dcdebug("result name %s \n", it->second->getName());
				root = it->second;

				if(basedate.empty() || (Util::toUInt32(basedate) < root->getLastWrite())) //compare the dates and add the last modified
					basedate = Util::toString(root->getLastWrite());
			
				for(Directory::Map::const_iterator it2 = root->directories.begin(); it2 != root->directories.end(); ++it2) {
					it2->second->toXml(sXml, recurse);
				}
				root->filesToXml(sXml);
			}
		} catch(...) {
			return NULL;
		}
	}
	sXml.stepOut();
	sXml.addChildAttrib("BaseDate", basedate);

	StringOutputStream sos(xml);
	sXml.toXML(&sos);

	if (xml.empty()) {
		dcdebug("Partial NULL");
		return NULL;
	} else {
		return new MemoryInputStream(xml);
	}
}

void ShareManager::Directory::toXml(SimpleXML& xmlFile, bool fullList){
	bool create = true;

	xmlFile.resetCurrentChild();
	
	while( xmlFile.findChild("Directory") ){
		if( stricmp(xmlFile.getChildAttrib("Name"), name) == 0 ){
			string curdate = xmlFile.getChildAttrib("Date");
			if(!curdate.empty() && Util::toUInt32(curdate) < lastwrite) //compare the dates and add the last modified
				xmlFile.replaceChildAttrib("Date", Util::toString(lastwrite));
			
			create = false;
			break;	
		}
	}

	if(create) {
		xmlFile.addTag("Directory");
		xmlFile.forceEndTag();
		xmlFile.addChildAttrib("Name", name);
		xmlFile.addChildAttrib("Date", Util::toString(lastwrite));
	}

	if(fullList) {
		xmlFile.stepIn();
		for(Map::const_iterator i = directories.begin(); i != directories.end(); ++i) {
				i->second->toXml(xmlFile, true);
		}

		filesToXml(xmlFile);
		xmlFile.stepOut();
	} else {
		if((!directories.empty() || !files.empty())) {
			if(xmlFile.getChildAttrib("Incomplete").empty()) {
				xmlFile.addChildAttrib("Incomplete", 1);
			}
			int64_t size = Util::toInt64(xmlFile.getChildAttrib("Size"));
			xmlFile.replaceChildAttrib("Size", Util::toString(getSize() + size));   //make the size accurate with virtuals, added a replace or add function to simpleXML
		}
	}
}

void ShareManager::Directory::filesToXml(SimpleXML& xmlFile) const {
	for(Directory::File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
		const Directory::File& f = *i;

		xmlFile.addTag("File");;
		xmlFile.addChildAttrib("Name", f.getName());
		xmlFile.addChildAttrib("Size", Util::toString(f.getSize()));
		xmlFile.addChildAttrib("TTH", f.getTTH().toBase32());
	}
}

// These ones we can look up as ints (4 bytes...)...

static const char* typeAudio[] = { ".mp3", ".mp2", ".mid", ".wav", ".ogg", ".wma", ".669", ".aac", ".aif", ".amf", ".ams", ".ape", ".dbm", ".dmf", ".dsm", ".far", ".mdl", ".med", ".mod", ".mol", ".mp1", ".mp4", ".mpa", ".mpc", ".mpp", ".mtm", ".nst", ".okt", ".psm", ".ptm", ".rmi", ".s3m", ".stm", ".ult", ".umx", ".wow" };
static const char* typeCompressed[] = { ".rar", ".zip", ".ace", ".arj", ".hqx", ".lha", ".sea", ".tar", ".tgz", ".uc2" };
static const char* typeDocument[] = { ".nfo", ".htm", ".doc", ".txt", ".pdf", ".chm" };
static const char* typeExecutable[] = { ".exe", ".com" };
static const char* typePicture[] = { ".jpg", ".gif", ".png", ".eps", ".img", ".pct", ".psp", ".pic", ".tif", ".rle", ".bmp", ".pcx", ".jpe", ".dcx", ".emf", ".ico", ".psd", ".tga", ".wmf", ".xif" };
static const char* typeVideo[] = { ".vob", ".mpg", ".mov", ".asf", ".avi", ".wmv", ".ogm", ".mkv", ".pxp", ".m1v", ".m2v", ".mpe", ".mps", ".mpv", ".ram" };

static const string type2Audio[] = { ".au", ".it", ".ra", ".xm", ".aiff", ".flac", ".midi", };
static const string type2Compressed[] = { ".gz" };
static const string type2Picture[] = { ".jpeg", ".ai", ".ps", ".pict", ".tiff" };
static const string type2Video[] = { ".mpeg", ".rm", ".divx", ".mp1v", ".mp2v", ".mpv1", ".mpv2", ".qt", ".rv", ".vivo" };

#define IS_TYPE(x) ( type == (*((uint32_t*)x)) )
#define IS_TYPE2(x) (stricmp(aString.c_str() + aString.length() - x.length(), x.c_str()) == 0) //hmm lower conversion...

bool ShareManager::checkType(const string& aString, int aType) {

	if(aType == SearchManager::TYPE_ANY)
		return true;

	if(aString.length() < 5)
		return false;
	
	const char* c = aString.c_str() + aString.length() - 3;
	if(!Text::isAscii(c))
		return false;

	uint32_t type = '.' | (Text::asciiToLower(c[0]) << 8) | (Text::asciiToLower(c[1]) << 16) | (((uint32_t)Text::asciiToLower(c[2])) << 24);

	switch(aType) {
	case SearchManager::TYPE_AUDIO:
		{
			for(size_t i = 0; i < (sizeof(typeAudio) / sizeof(typeAudio[0])); i++) {
				if(IS_TYPE(typeAudio[i])) {
					return true;
				}
			}
			for(size_t i = 0; i < (sizeof(type2Audio) / sizeof(type2Audio[0])); i++) {
				if(IS_TYPE2(type2Audio[i])) {
					return true;
				}
			}
		}
		break;
	case SearchManager::TYPE_COMPRESSED:
		{
			for(size_t i = 0; i < (sizeof(typeCompressed) / sizeof(typeCompressed[0])); i++) {
				if(IS_TYPE(typeCompressed[i])) {
					return true;
				}
			}
			if( IS_TYPE2(type2Compressed[0]) ) {
				return true;
			}
		}
		break;
	case SearchManager::TYPE_DOCUMENT:
		for(size_t i = 0; i < (sizeof(typeDocument) / sizeof(typeDocument[0])); i++) {
			if(IS_TYPE(typeDocument[i])) {
				return true;
			}
		}
		break;
	case SearchManager::TYPE_EXECUTABLE:
		if(IS_TYPE(typeExecutable[0]) || IS_TYPE(typeExecutable[1])) {
			return true;
		}
		break;
	case SearchManager::TYPE_PICTURE:
		{
			for(size_t i = 0; i < (sizeof(typePicture) / sizeof(typePicture[0])); i++) {
				if(IS_TYPE(typePicture[i])) {
					return true;
				}
			}
			for(size_t i = 0; i < (sizeof(type2Picture) / sizeof(type2Picture[0])); i++) {
				if(IS_TYPE2(type2Picture[i])) {
					return true;
				}
			}
		}
		break;
	case SearchManager::TYPE_VIDEO:
		{
			for(size_t i = 0; i < (sizeof(typeVideo) / sizeof(typeVideo[0])); i++) {
				if(IS_TYPE(typeVideo[i])) {
					return true;
				}
			}
			for(size_t i = 0; i < (sizeof(type2Video) / sizeof(type2Video[0])); i++) {
				if(IS_TYPE2(type2Video[i])) {
					return true;
				}
			}
		}
		break;
	default:
		dcassert(0);
		break;
	}
	return false;
}

SearchManager::TypeModes ShareManager::getType(const string& aFileName) noexcept {
	if(aFileName[aFileName.length() - 1] == PATH_SEPARATOR) {
		return SearchManager::TYPE_DIRECTORY;
	}
	 /*
	 optimize, check for compressed(rar) and audio first, the ones sharing the most are probobly sharing rars or mp3.
	 a test to match with regexp for rars first, otherwise it will match everything and end up setting type any for  .r01 ->
	 */
	try{ 
		if(RAR_regexp.match(aFileName, aFileName.length()-4) > 0)
			return SearchManager::TYPE_COMPRESSED;
	}catch(...) { } //not vital if it fails, just continue the type check.
	
	if(checkType(aFileName, SearchManager::TYPE_AUDIO))
		return SearchManager::TYPE_AUDIO;
	else if(checkType(aFileName, SearchManager::TYPE_VIDEO))
		return SearchManager::TYPE_VIDEO;
	else if(checkType(aFileName, SearchManager::TYPE_DOCUMENT))
		return SearchManager::TYPE_DOCUMENT;
	else if(checkType(aFileName, SearchManager::TYPE_COMPRESSED))
		return SearchManager::TYPE_COMPRESSED;
	else if(checkType(aFileName, SearchManager::TYPE_PICTURE))
		return SearchManager::TYPE_PICTURE;
	else if(checkType(aFileName, SearchManager::TYPE_EXECUTABLE))
		return SearchManager::TYPE_EXECUTABLE;

	return SearchManager::TYPE_ANY;
}

/**
 * Alright, the main point here is that when searching, a search string is most often found in 
 * the filename, not directory name, so we want to make that case faster. Also, we want to
 * avoid changing StringLists unless we absolutely have to --> this should only be done if a string
 * has been matched in the directory name. This new stringlist should also be used in all descendants,
 * but not the parents...
 */
void ShareManager::Directory::search(SearchResultList& aResults, StringSearch::List& aStrings, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) const noexcept {
	// Skip everything if there's nothing to find here (doh! =)
	if(!hasType(aFileType))
		return;

	StringSearch::List* cur = &aStrings;
	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(StringSearch::List::const_iterator k = aStrings.begin(); k != aStrings.end(); ++k) {
		if(k->match(name)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(aStrings));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		cur = newStr.get();
	}

	bool sizeOk = (aSearchType != SearchManager::SIZE_ATLEAST) || (aSize == 0);
	if( (cur->empty()) && 
		(((aFileType == SearchManager::TYPE_ANY) && sizeOk) || (aFileType == SearchManager::TYPE_DIRECTORY)) ) {
		// We satisfied all the search words! Add the directory...(NMDC searches don't support directory size)
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, 0, getFullName(), TTHValue()));
		aResults.push_back(sr);
		ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
	}

	if(aFileType != SearchManager::TYPE_DIRECTORY) {
		for(File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {
			
			if(aSearchType == SearchManager::SIZE_ATLEAST && aSize > i->getSize()) {
				continue;
			} else if(aSearchType == SearchManager::SIZE_ATMOST && aSize < i->getSize()) {
				continue;
			}	
			StringSearch::List::const_iterator j = cur->begin();
			for(; j != cur->end() && j->match(i->getName()); ++j) 
				;	// Empty
			
			if(j != cur->end())
				continue;
			
			// Check file type...
			if(checkType(i->getName(), aFileType)) {
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->getSize(), getFullName() + i->getName(), i->getTTH()));
				aResults.push_back(sr);
				ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
				if(aResults.size() >= maxResults) {
					break;
				}
			}
		}
	}

	for(Directory::Map::const_iterator l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		l->second->search(aResults, *cur, aSearchType, aSize, aFileType, aClient, maxResults);
	}
}
//NMDC Search
void ShareManager::search(SearchResultList& results, const string& aString, int aSearchType, int64_t aSize, int aFileType, Client* aClient, StringList::size_type maxResults) noexcept {
	RLock l(cs);
	if(aFileType == SearchManager::TYPE_TTH) {
		if(aString.compare(0, 4, "TTH:") == 0) {
			TTHValue tth(aString.substr(4));
			for(auto a = directories.begin(); a != directories.end(); ++a) {
				auto i = a->second->getRoot()->tthIndex.find(const_cast<TTHValue*>(&tth));
				if(i != a->second->getRoot()->tthIndex.end()) {
					SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second->getSize(), 
						i->second->getParent()->getFullName() + i->second->getName(), i->second->getTTH()));

					results.push_back(sr);
					ShareManager::getInstance()->addHits(1);
					break;
				}
			} //lookup in temp shares, nmdc too?
		}
		return;
	}
	StringTokenizer<string> t(Text::toLower(aString), '$');
	StringList& sl = t.getTokens();
	allSearches++;
	if(!bloom.match(sl)) {
		stoppedSearches++;
		return;
	}

	StringSearch::List ssl;
	for(StringList::const_iterator i = sl.begin(); i != sl.end(); ++i) {
		if(!i->empty()) {
			ssl.push_back(StringSearch(*i));
		}
	}
	if(ssl.empty())
		return;

	for(DirMap::const_iterator j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
		j->second->search(results, ssl, aSearchType, aSize, aFileType, aClient, maxResults);
	}
}

string ShareManager::getBloomStats() {
	vector<string> s;
	string ret = "Total StringSearches: " + Util::toString(allSearches) + ", stopped " + Util::toString((stoppedSearches > 0) ? (((double)stoppedSearches / (double)allSearches)*100) : 0) + " % (" + Util::toString(stoppedSearches) + " searches)";
	//ret += "Bloom size: " + Util::toString(bloom.getSize()) + ", length " + Util::toString(bloom.getLength());
	return ret;
}

namespace {
	inline uint16_t toCode(char a, char b) { return (uint16_t)a | ((uint16_t)b)<<8; }
}

ShareManager::AdcSearch::AdcSearch(const StringList& params) : include(&includeX), gt(0), 
	lt(numeric_limits<int64_t>::max()), hasRoot(false), isDirectory(false)
{
	for(StringIterC i = params.begin(); i != params.end(); ++i) {
		const string& p = *i;
		if(p.length() <= 2)
			continue;

		uint16_t cmd = toCode(p[0], p[1]);
		if(toCode('T', 'R') == cmd) {
			hasRoot = true;
			root = TTHValue(p.substr(2));
			return;
		} else if(toCode('A', 'N') == cmd) {
			includeX.push_back(StringSearch(p.substr(2)));		
		} else if(toCode('N', 'O') == cmd) {
			exclude.push_back(StringSearch(p.substr(2)));
		} else if(toCode('E', 'X') == cmd) {
			ext.push_back(p.substr(2));
		} else if(toCode('G', 'R') == cmd) {
			auto exts = AdcHub::parseSearchExts(Util::toInt(p.substr(2)));
			ext.insert(ext.begin(), exts.begin(), exts.end());
		} else if(toCode('R', 'X') == cmd) {
			noExt.push_back(p.substr(2));
		} else if(toCode('G', 'E') == cmd) {
			gt = Util::toInt64(p.substr(2));
		} else if(toCode('L', 'E') == cmd) {
			lt = Util::toInt64(p.substr(2));
		} else if(toCode('E', 'Q') == cmd) {
			lt = gt = Util::toInt64(p.substr(2));
		} else if(toCode('T', 'Y') == cmd) {
			isDirectory = (p[2] == '2');
		}
	}
}

bool ShareManager::AdcSearch::isExcluded(const string& str) {
	for(StringSearch::List::iterator i = exclude.begin(); i != exclude.end(); ++i) {
		if(i->match(str))
			return true;
	}
	return false;
}

bool ShareManager::AdcSearch::hasExt(const string& name) {
	if(ext.empty())
		return true;
	if(!noExt.empty()) {
		ext = StringList(ext.begin(), set_difference(ext.begin(), ext.end(), noExt.begin(), noExt.end(), ext.begin()));
		noExt.clear();
	}
	for(auto i = ext.cbegin(), iend = ext.cend(); i != iend; ++i) {
		if(name.length() >= i->length() && stricmp(name.c_str() + name.length() - i->length(), i->c_str()) == 0)
			return true;
	}
	return false;
}

void ShareManager::Directory::search(SearchResultList& aResults, AdcSearch& aStrings, StringList::size_type maxResults) const noexcept {
	StringSearch::List* old = aStrings.include;

	unique_ptr<StringSearch::List> newStr;

	// Find any matches in the directory name
	for(StringSearch::List::const_iterator k = aStrings.include->begin(); k != aStrings.include->end(); ++k) {
		if(k->match(name) && !aStrings.isExcluded(name)) {
			if(!newStr.get()) {
				newStr = unique_ptr<StringSearch::List>(new StringSearch::List(*aStrings.include));
			}
			newStr->erase(remove(newStr->begin(), newStr->end(), *k), newStr->end());
		}
	}

	if(newStr.get() != 0) {
		aStrings.include = newStr.get();
	}

	bool sizeOk = (aStrings.gt == 0);
	if( aStrings.include->empty() && aStrings.ext.empty() && sizeOk ) {
		// We satisfied all the search words! Add the directory...
		SearchResultPtr sr(new SearchResult(SearchResult::TYPE_DIRECTORY, getSize(), getFullName(), TTHValue()));
		aResults.push_back(sr);
		ShareManager::getInstance()->setHits(ShareManager::getInstance()->getHits()+1);
	}

	if(!aStrings.isDirectory) {
		for(File::Set::const_iterator i = files.begin(); i != files.end(); ++i) {

			if(!(i->getSize() >= aStrings.gt)) {
				continue;
			} else if(!(i->getSize() <= aStrings.lt)) {
				continue;
			}	

			if(aStrings.isExcluded(i->getName()))
				continue;

			StringSearch::List::const_iterator j = aStrings.include->begin();
			for(; j != aStrings.include->end() && j->match(i->getName()); ++j) 
				;	// Empty

			if(j != aStrings.include->end())
				continue;

			// Check file type...
			if(aStrings.hasExt(i->getName())) {

				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
					i->getSize(), getFullName() + i->getName(), i->getTTH()));
				aResults.push_back(sr);
				ShareManager::getInstance()->addHits(1);
				if(aResults.size() >= maxResults) {
					return;
				}
			}
		}
	}

	for(Directory::Map::const_iterator l = directories.begin(); (l != directories.end()) && (aResults.size() < maxResults); ++l) {
		l->second->search(aResults, aStrings, maxResults);
	}

	//faster to check this?
	if (aStrings.include->size() != old->size())
		aStrings.include = old;
}


void ShareManager::search(SearchResultList& results, const StringList& params, StringList::size_type maxResults, const Client* client, const CID& cid) noexcept {

	AdcSearch srch(params);	

	RLock l(cs);

	if(srch.hasRoot) {
		bool found = false;
		for(auto m = directories.begin(); m != directories.end(); ++m) {
			if(isHubExcluded(m->first, client))
				continue;

			auto i = m->second->getRoot()->tthIndex.find(const_cast<TTHValue*>(&srch.root));
			if(i != m->second->getRoot()->tthIndex.end()) {
				SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, 
					i->second->getSize(), i->second->getParent()->getFullName() + i->second->getName(), 
					i->second->getTTH()));
				results.push_back(sr);
				addHits(1);
				found = true;
				break;
			}
		}
		if(!found) {
			Lock l(tScs);
			auto Files = tempShares.equal_range(srch.root);
			for(auto i = Files.first; i != Files.second; ++i) {
				if(i->second.key.empty() || (i->second.key == cid.toBase32())) { // if no key is set, it means its a hub share.
					SearchResultPtr sr(new SearchResult(SearchResult::TYPE_FILE, i->second.size, "tmp\\" + Util::getFileName(i->second.path), i->first));
					results.push_back(sr);
					addHits(1);
				}
			}
		}
		return;
	}

	allSearches++;
	for(StringSearch::List::const_iterator i = srch.includeX.begin(); i != srch.includeX.end(); ++i) {
		if(!bloom.match(i->getPattern())) {
			stoppedSearches++;
			return;
		}
	}

	for(DirMap::const_iterator j = directories.begin(); (j != directories.end()) && (results.size() < maxResults); ++j) {
		if(isHubExcluded(j->first, client))
			continue;
		j->second->search(results, srch, maxResults);
	}
}
void ShareManager::cleanIndices(Directory::Ptr& dir) {

	if(!dir->directories.empty()) {
		for(auto i = dir->directories.begin(); i != dir->directories.end(); ++i) {
				cleanIndices(i->second);
			}
		}

	if(!dir->files.empty()) {
		Directory::RootDirectory* root = dir->findRoot();
		for(auto i = dir->files.begin(); i != dir->files.end(); ++i) {
			auto flst = root->tthIndex.equal_range(const_cast<TTHValue*>(&i->getTTH()));
				for(auto f = flst.first; f != flst.second; ++f) {
					if(stricmp(f->second->getRealPath(false), i->getRealPath(false)) == 0) {
						root->tthIndex.erase(f);
						break;
					}
				}
			}
		}

	dir->files.clear();
	dir->directories.clear();
}

void ShareManager::on(QueueManagerListener::BundleHashed, const string& path) noexcept {
	{
		WLock l(cs);

		Directory::Ptr dir = findDirectory(path, true, true);
		if (!dir) {
			LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_SHARING_FAILED)) % 
				Util::getLastDir(path).c_str()), LogManager::LOG_WARNING);
			return;
		}

		/* get rid of any existing crap we might have in the bundle directory and refresh it.
		done at this point as the file and directory pointers should still be valid, if there are any */
		cleanIndices(dir);

		buildTree(path, dir, false, /*create*/false);  //we dont need to create with buildtree, we have already created in findDirectory.
		updateIndices(*dir, *dir->findRoot());
		setDirty(true); //forceXmlRefresh
	}

	sortReleaseList();

	LogManager::getInstance()->message(str(boost::format(STRING(BUNDLE_SHARED)) % path.c_str()), LogManager::LOG_INFO);
}

bool ShareManager::allowAddDir(const string& path) noexcept {
	//LogManager::getInstance()->message("QueueManagerListener::BundleFilesMoved");
	{
		RLock l(cs);
		for(auto i = shares.begin(); i != shares.end(); i++) {
			if(strnicmp(i->first, path, i->first.size()) == 0) { //check if we have a share folder.
				//check the skiplist
				StringList sl = StringTokenizer<string>(path.substr(i->first.length()), PATH_SEPARATOR).getTokens();
				string fullPath = i->first;
				for(auto i = sl.begin(); i != sl.end(); ++i) {
					fullPath += Text::toLower(*i) + PATH_SEPARATOR;
					if (!AirUtil::checkSharedName(fullPath, true, true) || !shareFolder(fullPath)) {
						return false;
					}
				}
				return true;
			}
		}
	}
	return false;
}

ShareManager::Directory::Ptr ShareManager::findDirectory(const string& fname, bool allowAdd, bool report) {
	auto mi = find_if(directories.begin(), directories.end(), [&](pair<string, Directory::Ptr> dp) { return strnicmp(fname, dp.first, dp.first.length()) == 0; });
	if (mi != directories.end()) {
		auto curDir = mi->second;
		StringList sl = StringTokenizer<string>(fname.substr(mi->first.length()), PATH_SEPARATOR).getTokens();
		string fullPathLower = Text::toLower(mi->first);
		for(auto i = sl.begin(); i != sl.end(); ++i) {
			fullPathLower += Text::toLower(*i) + PATH_SEPARATOR;
			auto j = curDir->directories.find(*i);
			if (j != curDir->directories.end()) {
				curDir = j->second;
			} else if (!allowAdd || !AirUtil::checkSharedName(fullPathLower, true, report) || !shareFolder(fullPathLower)) {
				return NULL;
			} else {
				auto newDir = Directory::create(*i, curDir);
				newDir->setLastWrite(GET_TIME());
				curDir->directories[*i] = newDir;
				addReleaseDir(newDir->getFullName());
				curDir = newDir;
			}
		}
		return curDir;
	}
	return NULL;
}

void ShareManager::onFileHashed(const string& fname, const TTHValue& root) noexcept {
	WLock l(cs);
	Directory::Ptr d = findDirectory(Util::getFilePath(fname), true, false);
	if (!d) {
		return;
	}

	auto i = d->findFile(Util::getFileName(fname));
	if(i != d->files.end()) {
		Directory::RootDirectory* rootDir = d->findRoot();
		// Get rid of false constness...
		auto files = rootDir->tthIndex.equal_range(const_cast<TTHValue*>(&i->getTTH()));
		for(auto k = files.first; k != files.second; ++k) {
			if(stricmp(fname.c_str(), k->second->getRealPath(false).c_str()) == 0) {
				rootDir->tthIndex.erase(k);
				break;
			}
		}

		Directory::File* f = const_cast<Directory::File*>(&(*i));
		f->setTTH(root);
		rootDir->tthIndex.insert(make_pair(const_cast<TTHValue*>(&f->getTTH()), i));
	} else {
		string name = Util::getFileName(fname);
		int64_t size = File::getSize(fname);
		auto it = d->files.insert(Directory::File(name, size, d, root)).first;
		updateIndices(*d, it, *d->findRoot());
	}
		
	setDirty();
}

void ShareManager::on(TimerManagerListener::Minute, uint64_t tick) noexcept {

	if(SETTING(SHARE_SAVE_TIME) > 0){
		if(ShareCacheDirty && lastSave + SETTING(SHARE_SAVE_TIME) *60 *1000 <= tick)
			saveXmlList();
	}

	if(SETTING(INCOMING_REFRESH_TIME) > 0 && !incoming.empty()){
		if(lastIncomingUpdate + SETTING(INCOMING_REFRESH_TIME) * 60 * 1000 <= tick) {
			refresh(ShareManager::REFRESH_DIRECTORY | ShareManager::REFRESH_UPDATE | ShareManager::REFRESH_INCOMING);
		}
	}
	if(SETTING(AUTO_REFRESH_TIME) > 0) {
		if(lastFullUpdate + SETTING(AUTO_REFRESH_TIME) * 60 * 1000 <= tick) {
			refresh(ShareManager::REFRESH_ALL | ShareManager::REFRESH_UPDATE);
		}
	}
}

bool ShareManager::shareFolder(const string& path, bool thoroughCheck /* = false */, bool QuickCheck /*false*/ ) const {
	
	if(QuickCheck) //for validating shared realpaths.
		return shares.find(path) != shares.end();

	if(thoroughCheck)	// check if it's part of the share before checking if it's in the exclusions
	{
		bool result = false;
		for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i)
		{
			// is it a perfect match
			if((path.size() == i->first.size()) && (stricmp(path, Text::toLower(i->first)) == 0))
				return true;
			else if (path.size() > i->first.size()) // this might be a subfolder of a shared folder
			{
				// if the left-hand side matches and there is a \ in the remainder then it is a subfolder
				if((stricmp(path.substr(0, i->first.size()), i->first) == 0) && (path.find('\\', i->first.size()) != string::npos))
				{
					result = true;
					break;
				}
			}
		}

		if(!result)
			return false;
	}

	// check if it's an excluded folder or a sub folder of an excluded folder
	for(StringIterC j = notShared.begin(); j != notShared.end(); ++j)
	{		
		if(compare(path, *j) == 0)
			return false;

		if(thoroughCheck)
		{
			if(path.size() > (*j).size())
			{
				if((stricmp(path.substr(0, (*j).size()), *j) == 0) && (path[(*j).size()] == '\\'))
					return false;
			}
		}
	}
	return true;
}

int64_t ShareManager::addExcludeFolder(const string &path) {
	
	HashManager::getInstance()->stopHashing(path);
	
	// make sure this is a sub folder of a shared folder
	bool result = false;
	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i)
	{
		if(path.size() > i->first.size())
		{
			string temp = path.substr(0, i->first.size());
			if(stricmp(temp, i->first) == 0)
			{
				result = true;
				break;
			}
		}
	}

	if(!result)
		return 0;

	// Make sure this not a subfolder of an already excluded folder
	for(StringIterC j = notShared.begin(); j != notShared.end(); ++j)
	{
		if(path.size() >= (*j).size())
		{
			string temp = path.substr(0, (*j).size());
			if(stricmp(temp, *j) == 0)
				return 0;
		}
	}

	// remove all sub folder excludes
	int64_t bytesNotCounted = 0;
	for(StringIter j = notShared.begin(); j != notShared.end(); ++j)
	{
		if(path.size() < (*j).size())
		{
			string temp = (*j).substr(0, path.size());
			if(stricmp(temp, path) == 0)
			{
				bytesNotCounted += Util::getDirSize(*j);
				j = notShared.erase(j);
				j--;
			}
		}
	}

	// add it to the list
	notShared.push_back(Text::toLower(path));

	int64_t bytesRemoved = Util::getDirSize(path);

	return (bytesRemoved - bytesNotCounted);
}

int64_t ShareManager::removeExcludeFolder(const string &path, bool returnSize /* = true */) {
	int64_t bytesAdded = 0;
	// remove all sub folder excludes
	for(StringIter j = notShared.begin(); j != notShared.end(); ++j)
	{
		if(path.size() <= (*j).size())
		{
			string temp = (*j).substr(0, path.size());
			if(stricmp(temp, path) == 0)
			{
				if(returnSize) // this needs to be false if the files don't exist anymore
					bytesAdded += Util::getDirSize(*j);
				
				j = notShared.erase(j);
				j--;
			}
		}
	}
	
	return bytesAdded;
}

vector<pair<string, StringList>> ShareManager::getGroupedDirectories() const noexcept {
	vector<pair<string, StringList>> ret;
	//use getInstance()->LockRead() to lock this. and remember to unLockRead().
	for(StringMap::const_iterator i = shares.begin(); i != shares.end(); ++i) {
		auto retVirtual = find_if(ret.begin(), ret.end(), CompareFirst<string, StringList>(i->second));
		if (retVirtual != ret.end()) {
			retVirtual->second.insert(upper_bound(retVirtual->second.begin(), retVirtual->second.end(), i->first), i->first);
		} else {
			StringList tmp;
			tmp.push_back(i->first);
			ret.push_back(make_pair(i->second, tmp));
		}
	}
	sort(ret.begin(), ret.end());
	return ret;
}

} // namespace dcpp

/**
 * @file
 * $Id: ShareManager.cpp 473 2010-01-12 23:17:33Z bigmuscle $
 */
