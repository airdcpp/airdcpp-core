/*
* Copyright (C) 2011-2014 AirDC++ Project
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

#include "stdafx.h"
#include "Resource.h"

#include "QueueFrame2.h"
#include "MainFrm.h"
#include "PrivateFrame.h"
#include "Async.h"

#include "../client/AirUtil.h"
#include "../client/ShareManager.h"
#include "../client/ClientManager.h"
#include "../client/DownloadManager.h"
#include "ResourceLoader.h"

#define FILE_LIST_NAME _T("File Lists")
#define TEMP_NAME _T("Temp items")

int QueueFrame2::columnIndexes[] = { COLUMN_NAME, COLUMN_SIZE, COLUMN_PRIORITY, COLUMN_STATUS, COLUMN_DOWNLOADED, COLUMN_SOURCES, COLUMN_PATH};

int QueueFrame2::columnSizes[] = { 400, 80, 120, 120, 120, 120, 500 };

static ResourceManager::Strings columnNames[] = { ResourceManager::NAME, ResourceManager::SIZE, ResourceManager::PRIORITY, ResourceManager::STATUS, ResourceManager::DOWNLOADED, ResourceManager::SOURCES, ResourceManager::PATH };

LRESULT QueueFrame2::OnCreate(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled)
{

	CreateSimpleStatusBar(ATL_IDS_IDLEMESSAGE, WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | SBARS_SIZEGRIP);
	ctrlStatus.Attach(m_hWndStatusBar);

	ctrlQueue.Create(m_hWnd, rcDefault, NULL, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
		WS_HSCROLL | WS_VSCROLL | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS, WS_EX_CLIENTEDGE, IDC_QUEUE_LIST);
	ctrlQueue.SetExtendedListViewStyle(LVS_EX_LABELTIP | LVS_EX_HEADERDRAGDROP | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP);
	ctrlQueue.SetImageList(ResourceLoader::getFileImages(), LVSIL_SMALL);

	// Create listview columns
	WinUtil::splitTokens(columnIndexes, SETTING(QUEUEFRAME_ORDER), COLUMN_LAST);
	WinUtil::splitTokens(columnSizes, SETTING(QUEUEFRAME_WIDTHS), COLUMN_LAST);

	for (uint8_t j = 0; j < COLUMN_LAST; j++) {
		int fmt = (j == COLUMN_SIZE || j == COLUMN_DOWNLOADED) ? LVCFMT_RIGHT : LVCFMT_LEFT;
		ctrlQueue.InsertColumn(j, CTSTRING_I(columnNames[j]), fmt, columnSizes[j], j);
	}

	ctrlQueue.setColumnOrderArray(COLUMN_LAST, columnIndexes);
	ctrlQueue.setSortColumn(COLUMN_NAME);
	ctrlQueue.setVisible(SETTING(QUEUEFRAME_VISIBLE));
	ctrlQueue.SetBkColor(WinUtil::bgColor);
	ctrlQueue.SetTextBkColor(WinUtil::bgColor);
	ctrlQueue.SetTextColor(WinUtil::textColor);
	ctrlQueue.setFlickerFree(WinUtil::bgBrush);

	CRect rc(SETTING(QUEUE_LEFT), SETTING(QUEUE_TOP), SETTING(QUEUE_RIGHT), SETTING(QUEUE_BOTTOM));
	if (!(rc.top == 0 && rc.bottom == 0 && rc.left == 0 && rc.right == 0))
		MoveWindow(rc, TRUE);

	{
		//this currently leaves out file lists and temp downloads
		auto qm = QueueManager::getInstance();
		RLock l(qm->getCS());
		for (const auto& b : qm->getBundles() | map_values)
			onBundleAdded(b);
	}

	QueueManager::getInstance()->addListener(this);
	DownloadManager::getInstance()->addListener(this);

	memzero(statusSizes, sizeof(statusSizes));
	statusSizes[0] = 16;
	ctrlStatus.SetParts(6, statusSizes);
	//updateStatus();

	WinUtil::SetIcon(m_hWnd, IDI_QUEUE);
	bHandled = FALSE;
	return 1;
}

void QueueFrame2::UpdateLayout(BOOL bResizeBars /* = TRUE */) {
	RECT rect;
	GetClientRect(&rect);
	// position bars and offset their dimensions
	UpdateBarsPosition(rect, bResizeBars);

	if (ctrlStatus.IsWindow()) {
		CRect sr;
		int w[6];
		ctrlStatus.GetClientRect(sr);
		w[5] = sr.right - 16;
#define setw(x) w[x] = max(w[x+1] - statusSizes[x], 0)
		setw(4); setw(3); setw(2); setw(1);

		w[0] = 16;

		ctrlStatus.SetParts(6, w);

		ctrlStatus.GetRect(0, sr);
	}
	CRect rc = rect;
	ctrlQueue.MoveWindow(&rc);
}


LRESULT QueueFrame2::onClose(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM /*lParam*/, BOOL& bHandled) {
	if (!closed) {
		QueueManager::getInstance()->removeListener(this);
		DownloadManager::getInstance()->removeListener(this);
		closed = true;
		WinUtil::setButtonPressed(IDC_QUEUE2, false);
		PostMessage(WM_CLOSE);
		return 0;
	} else {
		CRect rc;
		if (!IsIconic()){
			//Get position of window
			GetWindowRect(&rc);
			//convert the position so it's relative to main window
			::ScreenToClient(GetParent(), &rc.TopLeft());
			::ScreenToClient(GetParent(), &rc.BottomRight());
			//save the position
			SettingsManager::getInstance()->set(SettingsManager::QUEUE_BOTTOM, (rc.bottom > 0 ? rc.bottom : 0));
			SettingsManager::getInstance()->set(SettingsManager::QUEUE_TOP, (rc.top > 0 ? rc.top : 0));
			SettingsManager::getInstance()->set(SettingsManager::QUEUE_LEFT, (rc.left > 0 ? rc.left : 0));
			SettingsManager::getInstance()->set(SettingsManager::QUEUE_RIGHT, (rc.right > 0 ? rc.right : 0));
		}

		ctrlQueue.saveHeaderOrder(SettingsManager::QUEUEFRAME_ORDER,
			SettingsManager::QUEUEFRAME_WIDTHS, SettingsManager::QUEUEFRAME_VISIBLE);
	
		ctrlQueue.DeleteAllItems();
		for (auto& i : itemInfos)
			delete i.second;

		itemInfos.clear();

		bHandled = FALSE;
		return 0;
	}
}

LRESULT QueueFrame2::onRemove(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	map<string, BundlePtr> bundles;
	QueueItemList queueitems;
	int sel = -1;
	int finishedFiles = 0;
	int fileBundles = 0;
	while ((sel = ctrlQueue.GetNextItem(sel, LVNI_SELECTED)) != -1) {
		QueueItemInfo* qii = (QueueItemInfo*)ctrlQueue.GetItemData(sel);
		if (qii->bundle) {
			finishedFiles += qii->bundle->getFinishedFiles().size();
			if (qii->bundle->isFileBundle())
				fileBundles++;
			bundles.emplace(qii->bundle->getToken(), qii->bundle);
		} else {
			//did we select the bundle to be deleted?
			if (qii->qi->getBundle() && bundles.find(qii->qi->getBundle()->getToken()) == bundles.end()) {
				queueitems.push_back(qii->qi);
			}
		}

	}

	if (bundles.size() >= 1) {
		string tmp;
		int dirBundles = bundles.size() - fileBundles;
		bool moveFinished = false;

		if (bundles.size() == 1) {
			BundlePtr bundle = bundles.begin()->second;
			tmp = STRING_F(CONFIRM_REMOVE_DIR_BUNDLE, bundle->getName().c_str());
			if (!WinUtil::MessageBoxConfirm(SettingsManager::CONFIRM_QUEUE_REMOVAL, Text::toT(tmp))) {
				return 0;
			}
			else {
				if (finishedFiles > 0) {
					tmp = STRING_F(CONFIRM_REMOVE_DIR_FINISHED_BUNDLE, finishedFiles);
					if (WinUtil::showQuestionBox(Text::toT(tmp), MB_ICONQUESTION)) {
						moveFinished = true;
					}
				}
			}
		}
		else {
			tmp = STRING_F(CONFIRM_REMOVE_DIR_MULTIPLE, dirBundles % fileBundles);
			if (!WinUtil::MessageBoxConfirm(SettingsManager::CONFIRM_QUEUE_REMOVAL, Text::toT(tmp))) {
				return 0;
			}
			else {
				if (finishedFiles > 0) {
					tmp = STRING_F(CONFIRM_REMOVE_DIR_FINISHED_MULTIPLE, finishedFiles);
					if (WinUtil::showQuestionBox(Text::toT(tmp), MB_ICONQUESTION)) {
						moveFinished = true;
					}
				}
			}
		}

		MainFrame::getMainFrame()->addThreadedTask([=] {
			for (auto b : bundles | map_values)
				QueueManager::getInstance()->removeBundle(b, false, moveFinished);
		});
	}

	if (queueitems.size() >= 1) {
		if (WinUtil::MessageBoxConfirm(SettingsManager::CONFIRM_QUEUE_REMOVAL, TSTRING(REALLY_REMOVE))) {
			for (auto& qi : queueitems)
				QueueManager::getInstance()->removeFile(qi->getTarget());
		}
	}

	return 0;
}

LRESULT QueueFrame2::onContextMenu(UINT /*uMsg*/, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {

	if (reinterpret_cast<HWND>(wParam) == ctrlQueue && ctrlQueue.GetSelectedCount() > 0) {
		POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
		if (pt.x == -1 && pt.y == -1) {
			WinUtil::getContextMenuPos(ctrlQueue, pt);
		}

		OMenu menu;
		menu.CreatePopupMenu();
		BundleList bl;
		QueueItemList queueItems;

		int sel = -1;
		while ((sel = ctrlQueue.GetNextItem(sel, LVNI_SELECTED)) != -1) {
			QueueItemInfo* qii = (QueueItemInfo*)ctrlQueue.GetItemData(sel);
			if (qii->bundle)
				bl.push_back(qii->bundle);
			else
				queueItems.push_back(qii->qi);
		}

		if (!bl.empty() && queueItems.empty())
			AppendBundleMenu(bl, menu);
		else if (bl.empty() && !queueItems.empty())
			AppendQiMenu(queueItems, menu);

		//common for Qi and Bundle
		menu.AppendMenu(MF_STRING, IDC_REMOVE, CTSTRING(REMOVE));

		menu.open(m_hWnd, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt);
		return TRUE;
	}

	bHandled = FALSE;
	return FALSE;
}

/*Bundle Menu*/
void QueueFrame2::AppendBundleMenu(BundleList& bl, OMenu& bundleMenu) {
	OMenu* removeMenu = bundleMenu.getMenu();
	OMenu* readdMenu = bundleMenu.getMenu();

	if (bl.size() == 1) {
		bundleMenu.InsertSeparatorFirst(TSTRING(BUNDLE));
		WinUtil::appendBundlePrioMenu(bundleMenu, bl);
	} else {
		bundleMenu.InsertSeparatorFirst(CTSTRING_F(X_BUNDLES, bl.size()));
		WinUtil::appendBundlePrioMenu(bundleMenu, bl);
	}

	/* Insert sub menus */
	auto formatUser = [this](Bundle::BundleSource& bs) -> tstring {
		auto& u = bs.user;
		tstring nick = WinUtil::escapeMenu(WinUtil::getNicks(u));
		// add hub hint to menu
		bool addHint = !u.hint.empty(), addSpeed = u.user->getSpeed() > 0;
		nick += _T(" (") + TSTRING(FILES) + _T(": ") + Util::toStringW(bs.files);
		if (addHint || addSpeed) {
			nick += _T(", ");
			if (addSpeed) {
				nick += TSTRING(SPEED) + _T(": ") + Util::formatBytesW(u.user->getSpeed()) + _T("/s)");
			}
			if (addHint) {
				if (addSpeed) {
					nick += _T(", ");
				}
				nick += TSTRING(HUB) + _T(": ") + Text::toT(u.hint);
			}
		}
		nick += _T(")");
		return nick;
	};

	if (bl.size() == 1) {
		BundlePtr b = bl.front();
		//current sources
		auto bundleSources = move(QueueManager::getInstance()->getBundleSources(b));
		if (!bundleSources.empty()) {
			removeMenu->appendItem(TSTRING(ALL), [b] {
				auto sources = move(QueueManager::getInstance()->getBundleSources(b));
				for (auto& si : sources)
					QueueManager::getInstance()->removeBundleSource(b, si.user.user, QueueItem::Source::FLAG_REMOVED);
			}, OMenu::FLAG_THREADED);
			removeMenu->appendSeparator();
		}

		for (auto& bs : bundleSources) {
			auto u = bs.user;
			removeMenu->appendItem(formatUser(bs), [=] { QueueManager::getInstance()->removeBundleSource(b, u, QueueItem::Source::FLAG_REMOVED); }, OMenu::FLAG_THREADED);
		}

		//bad sources
		auto badBundleSources = move(QueueManager::getInstance()->getBadBundleSources(b));

		if (!badBundleSources.empty()) {
			readdMenu->appendItem(TSTRING(ALL), [=] {
				auto sources = move(QueueManager::getInstance()->getBadBundleSources(b));
				for (auto& si : sources)
					QueueManager::getInstance()->readdBundleSource(b, si.user);
			}, OMenu::FLAG_THREADED);
			readdMenu->appendSeparator();
		}

		for (auto& bs : badBundleSources) {
			auto u = bs.user;
			readdMenu->appendItem(formatUser(bs), [=] { QueueManager::getInstance()->readdBundleSource(b, u); }, OMenu::FLAG_THREADED);
		}
		/* Sub menus end */

		// search
		bundleMenu.appendItem(TSTRING(SEARCH_BUNDLE_ALT), [=] {
			auto bundle = b;
			QueueManager::getInstance()->searchBundle(bundle, true);
		}, OMenu::FLAG_THREADED);

		bundleMenu.appendSeparator();

		WinUtil::appendSearchMenu(bundleMenu, b->getName());
		bundleMenu.appendItem(TSTRING(SEARCH_DIRECTORY), [=] {
			WinUtil::searchAny(b->isFileBundle() ? Util::getLastDir(Text::toT(b->getTarget())) : Text::toT(b->getName()));
		});

		bundleMenu.appendItem(TSTRING(OPEN_FOLDER), [=] { WinUtil::openFolder(Text::toT(b->getTarget())); });

		//Todo: move bundles
		//bundleMenu.AppendMenu(MF_STRING, IDC_MOVE, CTSTRING(MOVE_DIR));

		bundleMenu.appendItem(TSTRING(RENAME), [=] { onRenameBundle(b); });

		bundleMenu.appendSeparator();

		readdMenu->appendThis(TSTRING(READD_SOURCE), true);
		removeMenu->appendThis(TSTRING(REMOVE_SOURCE), true);

		bundleMenu.appendItem(TSTRING(USE_SEQ_ORDER), [=] {
			auto bundle = b;
			QueueManager::getInstance()->onUseSeqOrder(bundle);
		}, b->getSeqOrder() ? OMenu::FLAG_CHECKED : 0 | OMenu::FLAG_THREADED);
	}

}

/*QueueItem Menu*/
void QueueFrame2::AppendQiMenu(QueueItemList& ql, OMenu& fileMenu) {

	/* Do we need to control segment counts??
	OMenu segmentsMenu;
	segmentsMenu.CreatePopupMenu();
	segmentsMenu.InsertSeparatorFirst(TSTRING(MAX_SEGMENTS_NUMBER));
	for (int i = IDC_SEGMENTONE; i <= IDC_SEGMENTTEN; i++)
		segmentsMenu.AppendMenu(MF_STRING, i, (Util::toStringW(i - 109) + _T(" ") + TSTRING(SEGMENTS)).c_str());
	*/

	if (ql.size() == 1) {
		QueueItemPtr qi = ql.front();

		OMenu* pmMenu = fileMenu.getMenu();
		OMenu* browseMenu = fileMenu.getMenu();
		OMenu* removeAllMenu = fileMenu.getMenu();
		OMenu* removeMenu = fileMenu.getMenu();
		OMenu* readdMenu = fileMenu.getMenu();
		OMenu* getListMenu = fileMenu.getMenu();


		/* Create submenus */
		//segmentsMenu.CheckMenuItem(qi->getMaxSegments(), MF_BYPOSITION | MF_CHECKED);

		bool hasPMItems = false;
		auto sources = move(QueueManager::getInstance()->getSources(qi));

		//remove all sources from this file
		if (!sources.empty()) {
			removeMenu->appendItem(TSTRING(ALL), [=] {
				auto sources = QueueManager::getInstance()->getSources(qi);
				for (auto& si : sources)
					QueueManager::getInstance()->removeFileSource(qi->getTarget(), si.getUser(), QueueItem::Source::FLAG_REMOVED);
			}, OMenu::FLAG_THREADED);
			removeMenu->appendSeparator();
		}

		for (auto& s : sources) {
			tstring nick = WinUtil::escapeMenu(WinUtil::getNicks(s.getUser()));
			// add hub hint to menu
			if (!s.getUser().hint.empty())
				nick += _T(" (") + Text::toT(s.getUser().hint) + _T(")");

			auto u = s.getUser();
			auto target = qi->getTarget();

			// get list
			getListMenu->appendItem(nick, [=] {
				try {
					QueueManager::getInstance()->addList(u, QueueItem::FLAG_CLIENT_VIEW);
				} catch (const QueueException& e) {
					ctrlStatus.SetText(1, Text::toT(e.getError()).c_str());
				}
			});

			// browse list
			browseMenu->appendItem(nick, [=] {
				try {
					QueueManager::getInstance()->addList(u, QueueItem::FLAG_CLIENT_VIEW | QueueItem::FLAG_PARTIAL_LIST);
				} catch (const QueueException& e) {
					ctrlStatus.SetText(1, Text::toT(e.getError()).c_str());
				}
			});

			// remove source (this file)
			removeMenu->appendItem(nick, [=] { QueueManager::getInstance()->removeFileSource(target, u, QueueItem::Source::FLAG_REMOVED); }, OMenu::FLAG_THREADED);
			//remove source (all files)
			removeAllMenu->appendItem(nick, [=]{ QueueManager::getInstance()->removeSource(u, QueueItem::Source::FLAG_REMOVED); }, OMenu::FLAG_THREADED);

			// PM
			if (s.getUser().user->isOnline()) {
				pmMenu->appendItem(nick, [=] { PrivateFrame::openWindow(u); });
				hasPMItems = true;
			}
		}

		auto badSources = move(QueueManager::getInstance()->getBadSources(qi));
		if (!badSources.empty()) {
			readdMenu->appendItem(TSTRING(ALL), [=] {
				auto sources = QueueManager::getInstance()->getBadSources(qi);
				for (auto& si : sources)
					QueueManager::getInstance()->readdQISource(qi->getTarget(), si.getUser());
			}, OMenu::FLAG_THREADED);
			readdMenu->appendSeparator();
		}

		for (auto& s : badSources) {
			tstring nick = WinUtil::getNicks(s.getUser());
			if (s.isSet(QueueItem::Source::FLAG_FILE_NOT_AVAILABLE)) {
				nick += _T(" (") + TSTRING(FILE_NOT_AVAILABLE) + _T(")");
			}
			else if (s.isSet(QueueItem::Source::FLAG_BAD_TREE)) {
				nick += _T(" (") + TSTRING(INVALID_TREE) + _T(")");
			}
			else if (s.isSet(QueueItem::Source::FLAG_NO_NEED_PARTS)) {
				nick += _T(" (") + TSTRING(NO_NEEDED_PART) + _T(")");
			}
			else if (s.isSet(QueueItem::Source::FLAG_NO_TTHF)) {
				nick += _T(" (") + TSTRING(SOURCE_TOO_OLD) + _T(")");
			}
			else if (s.isSet(QueueItem::Source::FLAG_SLOW_SOURCE)) {
				nick += _T(" (") + TSTRING(SLOW_USER) + _T(")");
			}
			else if (s.isSet(QueueItem::Source::FLAG_UNTRUSTED)) {
				nick += _T(" (") + TSTRING(CERTIFICATE_NOT_TRUSTED) + _T(")");
			}

			// add hub hint to menu
			if (!s.getUser().hint.empty())
				nick += _T(" (") + Text::toT(s.getUser().hint) + _T(")");

			auto u = s.getUser();
			auto target = qi->getTarget();
			readdMenu->appendItem(nick, [=] { QueueManager::getInstance()->readdQISource(target, u); });
		}
		/* Submenus end */

		fileMenu.InsertSeparatorFirst(TSTRING(FILE));
		//fileMenu.AppendMenu(MF_STRING, IDC_SEARCH_ALTERNATES, CTSTRING(SEARCH_FOR_ALTERNATES));

		if (!qi->isSet(QueueItem::FLAG_USER_LIST)) {
			WinUtil::appendPreviewMenu(fileMenu, qi->getTarget());
		}

		//fileMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)segmentsMenu, CTSTRING(MAX_SEGMENTS_NUMBER));

		WinUtil::appendFilePrioMenu(fileMenu, ql);

		browseMenu->appendThis(TSTRING(BROWSE_FILE_LIST), true);
		getListMenu->appendThis(TSTRING(GET_FILE_LIST), true);
		pmMenu->appendThis(TSTRING(SEND_PRIVATE_MESSAGE), true);

		fileMenu.AppendMenu(MF_SEPARATOR);

		ListType::MenuItemList customItems{
			{ TSTRING(MAGNET_LINK), &handleCopyMagnet }
		};

		ctrlQueue.appendCopyMenu(fileMenu, customItems);
		WinUtil::appendSearchMenu(fileMenu, Util::getFilePath(qi->getTarget()));

		fileMenu.AppendMenu(MF_SEPARATOR);
		
		//Todo: move items
		//fileMenu.AppendMenu(MF_STRING, IDC_MOVE, CTSTRING(MOVE_RENAME_FILE));

		fileMenu.appendItem(TSTRING(OPEN_FOLDER), [=] { WinUtil::openFolder(Text::toT(qi->getTarget())); });
		fileMenu.AppendMenu(MF_SEPARATOR);

		readdMenu->appendThis(TSTRING(READD_SOURCE), true);
		removeMenu->appendThis(TSTRING(REMOVE_SOURCE), true);
		removeAllMenu->appendThis(TSTRING(REMOVE_FROM_ALL), true);

		fileMenu.AppendMenu(MF_STRING, IDC_REMOVE_OFFLINE, CTSTRING(REMOVE_OFFLINE));
		//TODO: rechecker
		//fileMenu.AppendMenu(MF_SEPARATOR);
		//fileMenu.AppendMenu(MF_STRING, IDC_RECHECK, CTSTRING(RECHECK_FILE));
	} else {
		fileMenu.InsertSeparatorFirst(TSTRING(FILES));
		//fileMenu.AppendMenu(MF_POPUP, (UINT_PTR)(HMENU)segmentsMenu, CTSTRING(MAX_SEGMENTS_NUMBER));

		WinUtil::appendFilePrioMenu(fileMenu, ql);

		//Todo: move items
		//fileMenu.AppendMenu(MF_STRING, IDC_MOVE, CTSTRING(MOVE_RENAME_FILE));

		fileMenu.AppendMenu(MF_SEPARATOR);
		fileMenu.AppendMenu(MF_STRING, IDC_REMOVE_OFFLINE, CTSTRING(REMOVE_OFFLINE));
		fileMenu.AppendMenu(MF_STRING, IDC_READD_ALL, CTSTRING(READD_ALL));
	}
}

LRESULT QueueFrame2::onRemoveOffline(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int i = -1;
	while ((i = ctrlQueue.GetNextItem(i, LVNI_SELECTED)) != -1) {
		const QueueItemInfo* ii = ctrlQueue.getItemData(i);

		const auto sources = QueueManager::getInstance()->getSources(ii->qi);
		for (const auto& s : sources) {
			if (!s.getUser().user->isOnline()) {
				QueueManager::getInstance()->removeFileSource(ii->qi->getTarget(), s.getUser().user, QueueItem::Source::FLAG_REMOVED);
			}
		}
	}
	return 0;
}
LRESULT QueueFrame2::onReaddAll(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/) {
	int i = -1;
	while ((i = ctrlQueue.GetNextItem(i, LVNI_SELECTED)) != -1) {
		const QueueItemInfo* ii = ctrlQueue.getItemData(i);
		if (ii->bundle)
			continue;

		// re-add all sources
		const auto badSources = QueueManager::getInstance()->getBadSources(ii->qi);
		for (const auto& bs : badSources) {
			QueueManager::getInstance()->readdQISource(ii->qi->getTarget(), bs.getUser());
		}
	}
	return 0;
}

/*
OK, here's the deal, we insert bundles as parents and assume every bundle (except file bundles) to have sub items, thus the + expand icon.
The bundle QueueItems(its sub items) are really created and inserted only at expanding the bundle,
once its expanded we start to collect some garbage when collapsing it to avoid continuous allocations and reallocations.
Notes, Mostly there should be no reason to expand every bundle at least with a big queue,
so this way we avoid creating and updating itemInfos we wont be showing,
with a small queue its more likely for the user to expand and collapse the same items more than once.
*/

LRESULT QueueFrame2::onLButton(UINT /*uMsg*/, WPARAM /*wParam*/, LPARAM lParam, BOOL& bHandled) {

	CPoint pt;
	pt.x = GET_X_LPARAM(lParam);
	pt.y = GET_Y_LPARAM(lParam);

	LVHITTESTINFO lvhti;
	lvhti.pt = pt;

	int pos = ctrlQueue.SubItemHitTest(&lvhti);
	if (pos != -1) {
		CRect rect;
		ctrlQueue.GetItemRect(pos, rect, LVIR_ICON);

		if (pt.x < rect.left) {
			auto i = ctrlQueue.getItemData(pos);
			if ((i->parent == NULL) && i->bundle && !i->bundle->isFileBundle())  {
				if (i->collapsed) {
					//insert the children at first expand, collect some garbage.
					if (ctrlQueue.findChildren(i->bundle->getToken()).empty()) {
						AddBundleQueueItems(i->bundle);
						ctrlQueue.resort();
					} else {
						ctrlQueue.Expand(i, pos);
					}
				} else {
					ctrlQueue.Collapse(i, pos);
				}
			}
		}
	}

	bHandled = FALSE;
	return 0;
}

tstring QueueFrame2::handleCopyMagnet(const QueueItemInfo* aII) {
	return Text::toT(WinUtil::makeMagnet(aII->qi->getTTH(), Util::getFileName(aII->qi->getTarget()), aII->qi->getSize()));
}

void QueueFrame2::onRenameBundle(BundlePtr b) {
	LineDlg dlg;
	dlg.title = TSTRING(RENAME);
	dlg.description = TSTRING(NEW_NAME);
	dlg.line = Text::toT(b->getName());
	if (dlg.DoModal(m_hWnd) == IDOK) {
		auto newName = Util::validatePath(Text::fromT(dlg.line), true);
		if (newName == b->getName()) {
			return;
		}

		MainFrame::getMainFrame()->addThreadedTask([=] {
			QueueManager::getInstance()->renameBundle(b, newName);
		});
	}
}

void QueueFrame2::onBundleAdded(const BundlePtr& aBundle) {
	auto i = itemInfos.find(aBundle->getToken());
	if (i == itemInfos.end()) {
		auto b = itemInfos.emplace(aBundle->getToken(), new QueueItemInfo(aBundle)).first;
		ctrlQueue.insertGroupedItem(b->second, false, !aBundle->isFileBundle()); // file bundles wont be having any children.
	}
}

void QueueFrame2::AddBundleQueueItems(const BundlePtr& aBundle) {
	for (auto& qi : aBundle->getQueueItems()){
		if (qi->isFinished()/* && !SETTING(KEEP_FINISHED_FILES)*/)
			continue;
		auto item = new QueueItemInfo(qi);
		itemInfos.emplace(qi->getTarget(), item);
		ctrlQueue.insertGroupedItem(item, true);
	}
}

//think of this more, now all queueitems get removed first then the whole bundle.
void QueueFrame2::onBundleRemoved(const BundlePtr& aBundle) {
	auto i = itemInfos.find(aBundle->getToken());
	if (i != itemInfos.end()) {
		ctrlQueue.removeGroupedItem(i->second); //also deletes item info
		itemInfos.erase(i);
	}
}

void QueueFrame2::onBundleUpdated(const BundlePtr& aBundle) {
	auto i = itemInfos.find(aBundle->getToken());
	if (i != itemInfos.end()) {
		int x = ctrlQueue.findItem(i->second);
		if (x != -1) {
			ctrlQueue.updateItem(x);
			if (aBundle->getQueueItems().empty())  //remove the + icon we have nothing to expand.
				ctrlQueue.SetItemState(x, INDEXTOSTATEIMAGEMASK(0), LVIS_STATEIMAGEMASK);
		}
	}
}

void QueueFrame2::onQueueItemRemoved(const QueueItemPtr& aQI) {
	auto item = itemInfos.find(aQI->getTarget());
	if (item != itemInfos.end()) {
		ctrlQueue.removeGroupedItem(item->second); //also deletes item info
		itemInfos.erase(item);
	}
}

void QueueFrame2::onQueueItemUpdated(const QueueItemPtr& aQI) {
	auto item = itemInfos.find(aQI->getTarget());
	if (item != itemInfos.end()) {
		auto itemInfo = item->second;
		if (!itemInfo->parent || (itemInfo->parent && !itemInfo->parent->collapsed)) // no need to update if its collapsed right?
			ctrlQueue.updateItem(itemInfo);
	}
}

void QueueFrame2::onQueueItemAdded(const QueueItemPtr& aQI) {
	auto item = itemInfos.find(aQI->getTarget());
	if (item == itemInfos.end()) {
		//queueItem not found, look if we have a parent for it and if its expanded
		if (aQI->getBundle()){
			auto parent = itemInfos.find(aQI->getBundle()->getToken());
			if ((parent == itemInfos.end()) || parent->second->collapsed)
				return;
		}
		auto i = itemInfos.emplace(aQI->getTarget(), new QueueItemInfo(aQI)).first;
		ctrlQueue.insertGroupedItem(i->second, false);
	}
}

void QueueFrame2::on(QueueManagerListener::BundleAdded, const BundlePtr& aBundle) noexcept {
	callAsync([=] { onBundleAdded(aBundle); });
}
void QueueFrame2::on(QueueManagerListener::BundleRemoved, const BundlePtr& aBundle) noexcept{
	callAsync([=] { onBundleRemoved(aBundle); });
}
void QueueFrame2::on(QueueManagerListener::BundleMoved, const BundlePtr& aBundle) noexcept{
	callAsync([=] { onBundleRemoved(aBundle); }); 
}
void QueueFrame2::on(QueueManagerListener::BundleMerged, const BundlePtr& aBundle, const string&) noexcept { 
	callAsync([=] { onBundleUpdated(aBundle); }); 
}
void QueueFrame2::on(QueueManagerListener::BundleSize, const BundlePtr& aBundle) noexcept { 
	callAsync([=] { onBundleUpdated(aBundle); }); 
}
void QueueFrame2::on(QueueManagerListener::BundlePriority, const BundlePtr& aBundle) noexcept { 
	callAsync([=] { onBundleUpdated(aBundle); }); 
}
void QueueFrame2::on(QueueManagerListener::BundleStatusChanged, const BundlePtr& aBundle) noexcept { 
	callAsync([=] { onBundleUpdated(aBundle); }); 
}
void QueueFrame2::on(QueueManagerListener::BundleSources, const BundlePtr& aBundle) noexcept { 
	callAsync([=] { onBundleUpdated(aBundle); }); 
}

void QueueFrame2::on(QueueManagerListener::Removed, const QueueItemPtr& aQI, bool /*finished*/) noexcept{
	if (!aQI->isSet(QueueItem::FLAG_USER_LIST)) callAsync([=] { onQueueItemRemoved(aQI); });
}
void QueueFrame2::on(QueueManagerListener::Added, QueueItemPtr& aQI) noexcept{
	if (!aQI->isSet(QueueItem::FLAG_USER_LIST)) callAsync([=] { onQueueItemAdded(aQI); });
}
void QueueFrame2::on(QueueManagerListener::SourcesUpdated, const QueueItemPtr& aQI) noexcept {
	if (!aQI->isSet(QueueItem::FLAG_USER_LIST)) callAsync([=] { onQueueItemUpdated(aQI); });
}
void QueueFrame2::on(QueueManagerListener::StatusUpdated, const QueueItemPtr& aQI) noexcept{
	if (!aQI->isSet(QueueItem::FLAG_USER_LIST)) callAsync([=] { onQueueItemUpdated(aQI); });
}

void QueueFrame2::on(DownloadManagerListener::BundleTick, const BundleList& tickBundles, uint64_t /*aTick*/) noexcept{
	for (auto& b : tickBundles) {
		callAsync([=] { onBundleUpdated(b); });
	}
}

/*QueueItemInfo functions*/

int QueueFrame2::QueueItemInfo::getImageIndex() const {
	//should bundles have own icon and sub items the file type image?
	if (bundle)
		return bundle->isFileBundle() ? ResourceLoader::getIconIndex(Text::toT(bundle->getTarget())) : ResourceLoader::DIR_NORMAL;
	else 
		return ResourceLoader::getIconIndex(Text::toT(qi->getTarget()));

}

const tstring QueueFrame2::QueueItemInfo::getText(int col) const {

	switch (col) {
		case COLUMN_NAME: return getName();
		case COLUMN_SIZE: return (getSize() != -1) ? Util::formatBytesW(getSize()) : TSTRING(UNKNOWN);
		case COLUMN_STATUS: return getStatusString();
		case COLUMN_DOWNLOADED: return (getSize() > 0) ? Util::formatBytesW(getDownloadedBytes()) + _T(" (") + Util::toStringW((double)getDownloadedBytes()*100.0 / (double)getSize()) + _T("%)") : Util::emptyStringT;
		case COLUMN_PRIORITY: 	
		{
			if (getPriority() == -1)
				return Util::emptyStringT;
			tstring priority = Text::toT(AirUtil::getPrioText(getPriority()));
			if (bundle && bundle->getAutoPriority() || qi && qi->getAutoPriority()) {
				priority += _T(" (") + TSTRING(AUTO) + _T(")");
			}
			return priority;
		}
		case COLUMN_SOURCES: return bundle ? Util::toStringW(bundle->getSources().size()) + _T(" sources") : qi ?  Util::toStringW(qi->getSources().size()) + _T(" sources") : Util::emptyStringT;
		case COLUMN_PATH: return bundle ? Text::toT(bundle->getTarget()) : qi ? Text::toT(qi->getTarget()) : Util::emptyStringT;
		
		default: return Util::emptyStringT;
	}
}

tstring QueueFrame2::QueueItemInfo::getName() const {
	if (bundle)
		return Text::toT(bundle->getName());
	else if (qi) {
		//show files in subdirectories as subdir/file.ext
		string name = qi->getTarget();
		if (qi->getBundle())
			name = name.substr(qi->getBundle()->getTarget().size(), qi->getTarget().size());
		else
			name = qi->getTargetFileName();
		return Text::toT(name);
	}
	return Util::emptyStringT;
}

int64_t QueueFrame2::QueueItemInfo::getSize() const {
	if (bundle)
		return bundle->getSize();
	else if (qi)
		return qi->getSize();
	else
		return -1;
}

int QueueFrame2::QueueItemInfo::getPriority() const {
	return  bundle ? bundle->getPriority() : qi ? qi->getPriority() : -1;
}

tstring QueueFrame2::QueueItemInfo::getStatusString() const {
	//Yeah, think about these a little more, might be nice to see which bundles are really running...
	if (bundle) {
		if (bundle->isPausedPrio()) 
			return TSTRING(PAUSED);
	
		switch (bundle->getStatus()) {
		case Bundle::STATUS_NEW:
		case Bundle::STATUS_QUEUED: return /*bundle->getRunning() ? TSTRING(DOWNLOADING) :*/ TSTRING(QUEUED);
		case Bundle::STATUS_DOWNLOADED:
		case Bundle::STATUS_MOVED: return TSTRING(DOWNLOADED);
		case Bundle::STATUS_FAILED_MISSING:
		case Bundle::STATUS_SHARING_FAILED: return _T("Sharing failed");
		case Bundle::STATUS_FINISHED: return _T("Finished");
		case Bundle::STATUS_HASHING: return _T("Hashing...");
		case Bundle::STATUS_HASH_FAILED: return _T("Hash failed");
		case Bundle::STATUS_HASHED: return TSTRING(HASHING_FINISHED_TOTAL_PLAIN);
		case Bundle::STATUS_SHARED: return TSTRING(SHARED);
		default:
			return Util::emptyStringT;
		}
	} else if(qi) {
		if (qi->isPausedPrio()) 
			return TSTRING(PAUSED);
		else if (qi->isFinished())
			return TSTRING(DOWNLOAD_FINISHED_IDLE);
		else if (qi->isRunning())
			return TSTRING(DOWNLOADING);
		else if (qi->isWaiting())
			return qi->getBundle() && qi->getBundle()->getRunning() ? TSTRING(WAITING) : TSTRING(QUEUED);
		else
			return TSTRING(QUEUED);
	}
	return Util::emptyStringT;
}

int64_t QueueFrame2::QueueItemInfo::getDownloadedBytes() const {
	return bundle ? bundle->getDownloadedBytes() : qi ? qi->getDownloadedBytes() : 0;
}

int QueueFrame2::QueueItemInfo::compareItems(const QueueItemInfo* a, const QueueItemInfo* b, int col) {
	switch (col) {
	case COLUMN_SIZE: return compare(a->getSize(), b->getSize());
	case COLUMN_PRIORITY: return compare(static_cast<int>(a->getPriority()), static_cast<int>(b->getPriority()));
	case COLUMN_DOWNLOADED: return compare(a->getDownloadedBytes(), b->getDownloadedBytes());
	default: 
		return Util::DefaultSort(a->getText(col).c_str(), b->getText(col).c_str());
	}
}