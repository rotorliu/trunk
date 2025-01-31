//##########################################################################
//#                                                                        #
//#                            CLOUDCOMPARE                                #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 of the License.               #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

//Always first
#include "ccIncludeGL.h"

#include "ccPolyline.h"

//Local
#include "ccPointCloud.h"
#include "ccCone.h"

//CCLib
#include <Neighbourhood.h>
#include <PointProjectionTools.h>
#include <CCMiscTools.h>

ccPolyline::ccPolyline(GenericIndexedCloudPersist* associatedCloud)
	: Polyline(associatedCloud)
	, ccShiftedObject("Polyline")
{
	set2DMode(false);
	setForeground(true);
	setVisible(true);
	lockVisibility(false);
	setColor(ccColor::white);
	showVertices(false);
	setVertexMarkerWidth(3);
	setWidth(0);
	showArrow(false,0,0);

	ccGenericPointCloud* cloud = dynamic_cast<ccGenericPointCloud*>(associatedCloud);
	if (cloud)
	{
		setGlobalScale(cloud->getGlobalScale());
		setGlobalShift(cloud->getGlobalShift());
	}
}

ccPolyline::ccPolyline(const ccPolyline& poly)
	: Polyline(0)
	, ccShiftedObject(poly)
{
	ccPointCloud* clone = 0;
	initWith(clone,poly);
}

bool ccPolyline::initWith(ccPointCloud*& vertices, const ccPolyline& poly)
{
	bool success = true;
	if (!vertices)
	{
		ccPointCloud* cloud = dynamic_cast<ccPointCloud*>(poly.m_theAssociatedCloud);
		ccPointCloud* clone = cloud ? cloud->partialClone(&poly) : ccPointCloud::From(&poly);
		if (clone)
		{
			if (cloud)
				clone->setName(cloud->getName()); //as 'partialClone' adds the '.extract' suffix by default
			else
				clone->setGLTransformationHistory(poly.getGLTransformationHistory());
		}
		else
		{
			//not enough memory?
			ccLog::Warning("[ccPolyline::initWith] Not enough memory to duplicate vertices!");
			success = false;
		}

		vertices = clone;
	}

	if (vertices)
	{
		setAssociatedCloud(vertices);
		addChild(vertices);
		//vertices->setEnabled(false);
		assert(m_theAssociatedCloud);
		if (m_theAssociatedCloud)
			addPointIndex(0,m_theAssociatedCloud->size());
	}

	setClosed(poly.m_isClosed);
	set2DMode(poly.m_mode2D);
	setForeground(poly.m_foreground);
	setVisible(poly.isVisible());
	lockVisibility(poly.isVisiblityLocked());
	setColor(poly.m_rgbColor);
	setWidth(poly.m_width);
	showColors(poly.colorsShown());
	showVertices(poly.verticesShown());
	setVertexMarkerWidth(poly.getVertexMarkerWidth());
	setVisible(poly.isVisible());
	showArrow(m_showArrow,m_arrowIndex,m_arrowLength);
	setGlobalScale(poly.getGlobalScale());
	setGlobalShift(poly.getGlobalShift());
	setGLTransformationHistory(poly.getGLTransformationHistory());
	setMetaData(poly.metaData());
	
	return success;
}

void ccPolyline::set2DMode(bool state)
{
	m_mode2D = state;
}

void ccPolyline::setForeground(bool state)
{
	m_foreground = state;
}

void ccPolyline::showArrow(bool state, unsigned vertIndex, PointCoordinateType length)
{
	m_showArrow = state;
	m_arrowIndex = vertIndex;
	m_arrowLength = length;
}

ccBBox ccPolyline::getOwnBB(bool withGLFeatures/*=false*/)
{
	ccBBox emptyBox;
	getBoundingBox(emptyBox.minCorner(), emptyBox.maxCorner());
	emptyBox.setValidity(!is2DMode() && size() != 0);
	return emptyBox;
}

bool ccPolyline::hasColors() const
{
	return true;
}

void ccPolyline::applyGLTransformation(const ccGLMatrix& trans)
{
	//transparent call
	ccHObject::applyGLTransformation(trans);

	//invalidate the bounding-box
	//(and we hope the vertices will be updated as well!)
	m_validBB = false;
}

//unit arrow
static QSharedPointer<ccCone> c_unitArrow(0);

void ccPolyline::drawMeOnly(CC_DRAW_CONTEXT& context)
{
	//no picking enabled on polylines
	if (MACRO_DrawPointNames(context))
		return;

	unsigned vertCount = size();
	if (vertCount < 2)
		return;

	bool draw = false;

	if (MACRO_Draw3D(context))
	{
		draw = !m_mode2D;
	}
	else if (m_mode2D)
	{
		bool drawFG = MACRO_Foreground(context);
		draw = ((drawFG && m_foreground) || (!drawFG && !m_foreground));
	}

	if (draw)
	{
		//standard case: list names pushing
		bool pushName = MACRO_DrawEntityNames(context);
		if (pushName)
			glPushName(getUniqueIDForDisplay());

		if (colorsShown())
			ccGL::Color3v(m_rgbColor.rgb);

		//display polyline
		if (vertCount > 1)
		{
			if (m_width != 0)
			{
				glPushAttrib(GL_LINE_BIT);
				glLineWidth(static_cast<GLfloat>(m_width));
			}

			//DGM: we do the 'GL_LINE_LOOP' manually as I have a strange bug
			//on one on my graphic card with this mode!
			//glBegin(m_isClosed ? GL_LINE_LOOP : GL_LINE_STRIP);
			glBegin(GL_LINE_STRIP);
			for (unsigned i=0; i<vertCount; ++i)
			{
				ccGL::Vertex3v(getPoint(i)->u);
			}
			if (m_isClosed)
			{
				ccGL::Vertex3v(getPoint(0)->u);
			}
			glEnd();

			//display arrow
			if (m_showArrow && m_arrowIndex < vertCount && (m_arrowIndex > 0 || m_isClosed))
			{
				const CCVector3* P0 = getPoint(m_arrowIndex == 0 ? vertCount-1 : m_arrowIndex-1);
				const CCVector3* P1 = getPoint(m_arrowIndex);
				//direction of the last polyline chunk
				CCVector3 u = *P1 - *P0;
				u.normalize();

				if (m_mode2D)
				{
					u *= -m_arrowLength;
					static const PointCoordinateType s_defaultArrowAngle = static_cast<PointCoordinateType>(15.0 * CC_DEG_TO_RAD);
					static const PointCoordinateType cost = cos(s_defaultArrowAngle);
					static const PointCoordinateType sint = sin(s_defaultArrowAngle);
					CCVector3 A(cost * u.x - sint * u.y,  sint * u.x + cost * u.y, 0);
					CCVector3 B(cost * u.x + sint * u.y, -sint * u.x + cost * u.y, 0);
					glBegin(GL_POLYGON);
					ccGL::Vertex3v((A+*P1).u);
					ccGL::Vertex3v((B+*P1).u);
					ccGL::Vertex3v((  *P1).u);
					glEnd();
				}
				else
				{
					if (!c_unitArrow)
					{
						c_unitArrow = QSharedPointer<ccCone>(new ccCone(0.5,0.0,1.0));
						c_unitArrow->showColors(true);
						c_unitArrow->showNormals(false);
						c_unitArrow->setVisible(true);
						c_unitArrow->setEnabled(true);
					}
					if (colorsShown())
						c_unitArrow->setTempColor(m_rgbColor);
					else
						c_unitArrow->setTempColor(context.pointsDefaultCol);
					//build-up unit arrow own 'context'
					CC_DRAW_CONTEXT markerContext = context;
					markerContext.flags &= (~CC_DRAW_ENTITY_NAMES); //we must remove the 'push name flag' so that the sphere doesn't push its own!
					markerContext._win = 0;

					glMatrixMode(GL_MODELVIEW);
					glPushMatrix();
					ccGL::Translate(P1->x,P1->y,P1->z);
					ccGLMatrix rotMat = ccGLMatrix::FromToRotation(CCVector3(0,0,1),u);
					glMultMatrixf(rotMat.inverse().data());
					glScalef(m_arrowLength,m_arrowLength,m_arrowLength);
					ccGL::Translate(0.0,0.0,-0.5);
					c_unitArrow->draw(markerContext);
					glPopMatrix();
				}
			}

			if (m_width != 0)
			{
				glPopAttrib();
			}
		}

		//display vertices
		if (m_showVertices)
		{
			glPushAttrib(GL_POINT_BIT);
			glPointSize((GLfloat)m_vertMarkWidth);

			glBegin(GL_POINTS);
			for (unsigned i=0; i<vertCount; ++i)
			{
				ccGL::Vertex3v(getPoint(i)->u);
			}
			glEnd();

			glPopAttrib();
		}

		if (pushName)
			glPopName();
	}
}

void ccPolyline::setWidth(PointCoordinateType width)
{
	m_width = width;
}

bool ccPolyline::toFile_MeOnly(QFile& out) const
{
	if (!ccHObject::toFile_MeOnly(out))
		return false;

	//we can't save the associated cloud here (as it may be shared by multiple polylines)
	//so instead we save it's unique ID (dataVersion>=28)
	//WARNING: the cloud must be saved in the same BIN file! (responsibility of the caller)
	ccPointCloud* vertices = dynamic_cast<ccPointCloud*>(m_theAssociatedCloud);
	if (!vertices)
	{
		ccLog::Warning("[ccPolyline::toFile_MeOnly] Polyline vertices is not a ccPointCloud structure?!");
		return false;
	}
	uint32_t vertUniqueID = (m_theAssociatedCloud ? (uint32_t)vertices->getUniqueID() : 0);
	if (out.write((const char*)&vertUniqueID,4) < 0)
		return WriteError();

	//number of points (references to) (dataVersion>=28)
	uint32_t pointCount = size();
	if (out.write((const char*)&pointCount,4) < 0)
		return WriteError();

	//points (references to) (dataVersion>=28)
	for (uint32_t i=0; i<pointCount; ++i)
	{
		uint32_t pointIndex = getPointGlobalIndex(i);
		if (out.write((const char*)&pointIndex,4) < 0)
			return WriteError();
	}

	//'global shift & scale' (dataVersion>=39)
	saveShiftInfoToFile(out);
	
	QDataStream outStream(&out);

	//Closing state (dataVersion>=28)
	outStream << m_isClosed;

	//RGB Color (dataVersion>=28)
	outStream << m_rgbColor.r;
	outStream << m_rgbColor.g;
	outStream << m_rgbColor.b;

	//2D mode (dataVersion>=28)
	outStream << m_mode2D;

	//Foreground mode (dataVersion>=28)
	outStream << m_foreground;

	//The width of the line (dataVersion>=31)
	outStream << m_width;

	return true;
}

bool ccPolyline::fromFile_MeOnly(QFile& in, short dataVersion, int flags)
{
	if (!ccHObject::fromFile_MeOnly(in, dataVersion, flags))
		return false;

	if (dataVersion<28)
		return false;

	//as the associated cloud (=vertices) can't be saved directly (as it may be shared by multiple polylines)
	//we only store its unique ID (dataVersion>=28) --> we hope we will find it at loading time (i.e. this
	//is the responsibility of the caller to make sure that all dependencies are saved together)
	uint32_t vertUniqueID = 0;
	if (in.read((char*)&vertUniqueID,4) < 0)
		return ReadError();
	//[DIRTY] WARNING: temporarily, we set the vertices unique ID in the 'm_associatedCloud' pointer!!!
	*(uint32_t*)(&m_theAssociatedCloud) = vertUniqueID;

	//number of points (references to) (dataVersion>=28)
	uint32_t pointCount = 0;
	if (in.read((char*)&pointCount,4) < 0)
		return ReadError();
	if (!reserve(pointCount))
		return false;

	//points (references to) (dataVersion>=28)
	for (uint32_t i=0; i<pointCount; ++i)
	{
		uint32_t pointIndex = 0;
		if (in.read((char*)&pointIndex,4) < 0)
			return ReadError();
		addPointIndex(pointIndex);
	}

	//'global shift & scale' (dataVersion>=39)
	if (dataVersion >= 39)
	{
		if (!loadShiftInfoFromFile(in))
			return ReadError();
	}
	else
	{
		m_globalScale = 1.0;
		m_globalShift = CCVector3d(0,0,0);
	}

	QDataStream inStream(&in);

	//Closing state (dataVersion>=28)
	inStream >> m_isClosed;

	//RGB Color (dataVersion>=28)
	inStream >> m_rgbColor.r;
	inStream >> m_rgbColor.g;
	inStream >> m_rgbColor.b;

	//2D mode (dataVersion>=28)
	inStream >> m_mode2D;

	//Foreground mode (dataVersion>=28)
	inStream >> m_foreground;

	//Width of the line (dataVersion>=31)
	if (dataVersion >= 31)
		ccSerializationHelper::CoordsFromDataStream(inStream,flags,&m_width,1);
	else
		m_width = 0;

	return true;
}

bool ccPolyline::split(	PointCoordinateType maxEdgeLength,
						std::vector<ccPolyline*>& parts)
{
	parts.clear();

	//not enough vertices?
	unsigned vertCount = size();
	if (vertCount <= 2)
	{
		parts.push_back(new ccPolyline(*this));
		return true;
	}

	unsigned startIndex = 0;
	unsigned lastIndex = vertCount-1;
	while (startIndex <= lastIndex)
	{
		unsigned stopIndex = startIndex;
		while (stopIndex < lastIndex && (*getPoint(stopIndex+1) - *getPoint(stopIndex)).norm() <= maxEdgeLength)
		{
			++stopIndex;
		}

		//number of vertices for the current part
		unsigned partSize = stopIndex-startIndex+1;

		//if the polyline is closed we have to look backward for the first segment!
		if (startIndex == 0)
		{
			if (isClosed())
			{
				unsigned realStartIndex = vertCount;
				while (realStartIndex > stopIndex && (*getPoint(realStartIndex-1) - *getPoint(realStartIndex % vertCount)).norm() <= maxEdgeLength)
				{
					--realStartIndex;
				}

				if (realStartIndex == stopIndex)
				{
					//whole loop
					parts.push_back(new ccPolyline(*this));
					return true;
				}
				else if (realStartIndex < vertCount)
				{
					partSize += (vertCount - realStartIndex);
					assert(realStartIndex != 0);
					lastIndex = realStartIndex-1;
					//warning: we shift the indexes!
					startIndex = realStartIndex; 
					stopIndex += vertCount;
				}
			}
			else if (partSize == vertCount)
			{
				//whole polyline
				parts.push_back(new ccPolyline(*this));
				return true;
			}
		}

		if (partSize > 1) //otherwise we skip that point
		{
			//create the corresponding part
			CCLib::ReferenceCloud ref(m_theAssociatedCloud);
			if (!ref.reserve(partSize))
			{
				ccLog::Error("[ccPolyline::split] Not enough memory!");
				return false;
			}

			for (unsigned i=startIndex; i<=stopIndex; ++i)
			{
				ref.addPointIndex(i % vertCount);
			}

			ccPointCloud* vertices = dynamic_cast<ccPointCloud*>(m_theAssociatedCloud);
			ccPointCloud* subset = vertices ? vertices->partialClone(&ref) : ccPointCloud::From(&ref);
			ccPolyline* part = new ccPolyline(subset);
			part->initWith(subset,*this);
			part->setClosed(false); //by definition!
			parts.push_back(part);
		}

		//forward
		startIndex = (stopIndex % vertCount) + 1;
	}

	return true;
}

PointCoordinateType ccPolyline::computeLength() const
{
	PointCoordinateType length = 0;

	unsigned vertCount = size();
	if (vertCount > 1 && m_theAssociatedCloud)
	{
		unsigned lastVert = isClosed() ? vertCount : vertCount-1;
		for (unsigned i=0; i<lastVert; ++i)
		{
			CCVector3 A;
			getPoint(i,A);
			CCVector3 B;
			getPoint((i+1)%vertCount,B);

			length += (B-A).norm();
		}
	}

	return length;
}

unsigned ccPolyline::getUniqueIDForDisplay() const
{
	if (m_parent && m_parent->getParent() && m_parent->getParent()->isA(CC_TYPES::FACET))
		return m_parent->getParent()->getUniqueID();
	else
		return getUniqueID();
}
