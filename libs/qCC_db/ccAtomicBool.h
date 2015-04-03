#ifndef ATOMIC_BOOL_HEADER
#define ATOMIC_BOOL_HEADER

//qCC_db
#include "ccObject.h" //for CC_QT5

//Qt
#include <QAtomicInt>

//! Qt 4/5 compatible atomic boolean
class ccAtomicBool
{
public:
	ccAtomicBool() : value(0) {}
	ccAtomicBool(bool state) : value(state ? 1 : 0) {}

	//! Conversion to bool
#ifndef CC_QT5
	inline operator bool() const { return value != 0; }
#else
	inline operator bool() const { return value.load() != 0; }
#endif

	QAtomicInt value;
};

#endif
