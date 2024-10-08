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

#ifndef DCPLUSPLUS_DCPP_SIMPLE_XML_H
#define DCPLUSPLUS_DCPP_SIMPLE_XML_H

#include <airdcpp/forward.h>
#include <airdcpp/util/Util.h>

#include <airdcpp/core/io/xml/SimpleXMLReader.h>

#include <boost/noncopyable.hpp>

namespace dcpp {

/**
 * A simple XML class that loads an XML-ish structure into an internal tree
 * and allows easy access to each element through a "current location".
 */
class SimpleXML : private boost::noncopyable
{
public:
	SimpleXML();
	~SimpleXML() = default;
	
	void addTag(const string& aName, const string& aData = Util::emptyString);
	void addTag(const string& aName, int aData) {
		addTag(aName, Util::toString(aData));
	}
	void addTag(const string& aName, int64_t aData) {
		addTag(aName, Util::toString(aData));
	}

	void forceEndTag(bool force = true) const {
		checkChildSelected();
		(*currentChild)->forceEndTag = force;
	}

	template<typename T>
	void addAttrib(const string& aName, const T& aData) {
		addAttrib(aName, Util::toString(aData));
	}

	void addAttrib(const string& aName, const string& aData);
	void addAttrib(const string& aName, bool aData) {	
		addAttrib(aName, string(aData ? "1" : "0"));
	}
	
	template <typename T>
    void addChildAttrib(const string& aName, const T& aData) const {	
		addChildAttrib(aName, Util::toString(aData));
	}
	void addChildAttrib(const string& aName, const string& aData) const;
	void addChildAttrib(const string& aName, bool aData) const {	
		addChildAttrib(aName, string(aData ? "1" : "0"));
	}
	void replaceChildAttrib(const string& aName, const string& aData) const;

	const string& getData() const {
		dcassert(current);
		return current->data;
	}

	void setData(const string& aData) {
		dcassert(current);
		current->data = aData;
	}
	
	void stepIn();
	void stepOut();

	void resetCurrentChild() noexcept;

	bool findChild(const string& aName) noexcept;

	const string& getChildData() const {
		checkChildSelected();
		return (*currentChild)->data;
	}

	const string& getChildAttrib(const string& aName, const string& aDefault = Util::emptyString) const {
		checkChildSelected();
		return (*currentChild)->getAttrib(aName, aDefault);
	}

	int getIntChildAttrib(const string& aName) const {
		checkChildSelected();
		return Util::toInt(getChildAttrib(aName));
	}
	time_t getTimeChildAttrib(const string& aName) const {
		checkChildSelected();
		return Util::toTimeT(getChildAttrib(aName));
	}
	bool getBoolChildAttrib(const string& aName) const {
		checkChildSelected();
		const string& tmp = getChildAttrib(aName);

		return (tmp.size() > 0) && tmp[0] == '1';
	}
	
	void fromXML(const string& aXML, int aFlags = 0);
	string toXML();
	string childToXML() const;
	void toXML(OutputStream* f);
	
	static const string& escape(const string& str, string& tmp, bool aAttrib, bool aLoading = false) {
		if(needsEscape(str, aAttrib, aLoading)) {
			tmp = str;
			return escape(tmp, aAttrib, aLoading);
		}
		return str;
	}
	static string& escape(string& aString, bool aAttrib, bool aLoading = false);
	/** 
	 * This is a heuristic for whether escape needs to be called or not. The results are
 	 * only guaranteed for false, i e sometimes true might be returned even though escape
	 * was not needed...
	 */
	static bool needsEscape(const string& aString, bool aAttrib, bool aLoading = false) {
		return (aLoading ? aString.find('&') : aString.find_first_of(aAttrib ? "<&>'\"" : "<&>")) != string::npos;
	}
	static const string utf8Header;
private:
	class Tag : public boost::noncopyable {
	public:
		using Ptr = Tag *;
		using List = vector<Ptr>;
		using Iter = List::iterator;

		/**
		 * A simple list of children. To find a tag, one must search the entire list.
		 */ 
		List children;
		/**
		 * Attributes of this tag. According to the XML standard the names
		 * must be unique (case-sensitive). (Assuming that we have few attributes here,
		 * we use a vector instead of a (hash)map to save a few bytes of memory and unnecessary
		 * calls to the memory allocator...)
		 */
		StringPairList attribs;
		
		/** Tag name */
		string name;

		/** Tag data, may be empty. */
		string data;
				
		/** Parent tag, for easy traversal */
		Ptr parent;
		bool forceEndTag;

		Tag(const string& aName, const StringPairList& a, Ptr aParent) : attribs(a), name(aName), data(), parent(aParent), forceEndTag(false) { 
		}

		Tag(const string& aName, const string& d, Ptr aParent) : name(aName), data(d), parent(aParent), forceEndTag(false) { 
		}
		
		const string& getAttrib(const string& aName, const string& aDefault = Util::emptyString) const {
			auto i = ranges::find(attribs | views::keys, aName).base();
			return (i == attribs.end()) ? aDefault : i->second; 
		}
		void toXML(int indent, OutputStream* f, bool noIndent=false) const;
		
		void appendAttribString(string& tmp) const;
		/** Delete all children! */
		~Tag() {
			for(auto& i: children) {
				delete i;
			}
		}
	};

	class TagReader : public SimpleXMLReader::CallBack {
	public:
		explicit TagReader(Tag* root) : cur(root) { }
		void startTag(const string& name, StringPairList& attribs, bool simple);
		void data(const string& data) {
			cur->data += data;
		}
		void endTag(const string&);

		Tag* cur;
	};

	/** Bogus root tag, should have only one child! */
	Tag root;

	/** Current position */
	Tag::Ptr current;

	Tag::Iter currentChild;

	void checkChildSelected() const noexcept {
		dcassert(current);
		dcassert(currentChild != current->children.end());
	}

	bool found = false;
};

} // namespace dcpp

#endif // !defined(SIMPLE_XML_H)