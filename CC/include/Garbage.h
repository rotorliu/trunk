//##########################################################################
//#                                                                        #
//#                               CCLIB                                    #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU Library General Public License as       #
//#  published by the Free Software Foundation; version 2 of the License.  #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#ifndef CC_GARBAGE_HEADER
#define CC_GARBAGE_HEADER

//local
#include "ScalarField.h"

//STL
#include <set>

//! Garbage container (automatically deletes pointers when destroyed)
template<typename C> class Garbage
{
public:
	//! Puts an item in the trash
	inline void add(C* item)
	{
		try
		{
			m_items.insert(item);
		}
		catch (const std::bad_alloc&)
		{
			//what can we do?!
		}
	}

	//! Removes an item from the trash
	/** \warning The item won't be destroyed!
	**/
	inline void remove(C* item)
	{
		m_items.erase(item);
	}

	//! To manually delete an item already in the trash
	inline void destroy(C* item)
	{
		m_items.erase(item);
		delete item;
	}

	//! Destructor
	/** Automatically deletes all items
	**/
	~Garbage()
	{
		//dispose of left over
		typedef typename std::set<C*>::iterator iterator;
		for (iterator it = m_items.begin(); it != m_items.end(); ++it)
			delete *it;
		m_items.clear();
	}

	//! Items to delete
	std::set<C*> m_items;
};

//! Speciailization for ScalarFields
template <> class Garbage<CCLib::ScalarField>
{
public:
	//! Puts an item in the trash
	inline void add(CCLib::ScalarField* item)
	{
		try
		{
			m_items.insert(item);
		}
		catch (const std::bad_alloc&)
		{
			//what can we do?!
		}
	}

	//! Removes an item from the trash
	/** \warning The item won't be destroyed!
	**/
	inline void remove(CCLib::ScalarField* item)
	{
		m_items.erase(item);
	}

	//! Manually deltes an item already the trash
	inline void destroy(CCLib::ScalarField* item)
	{
		m_items.erase(item);
		item->release();
	}

	//! Destructor
	/** Automatically deletes all items
	**/
	~Garbage()
	{
		//dispose of left over
		for (std::set<CCLib::ScalarField*>::iterator it = m_items.begin(); it != m_items.end(); ++it)
			(*it)->release();
		m_items.clear();
	}

	//! Items to delete
	std::set<CCLib::ScalarField*> m_items;
};

#endif //CC_GARBAGE_HEADER
