//##########################################################################
//#                                                                        #
//#                     CLOUDCOMPARE PLUGIN: qFacets                       #
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
//#                           COPYRIGHT: BRGM                              #
//#                                                                        #
//##########################################################################

#ifndef QFACET_FAST_MARCHING_FOR_FACET_EXTRACTION_HEADER
#define QFACET_FAST_MARCHING_FOR_FACET_EXTRACTION_HEADER

//CCLib
#include <FastMarching.h>
#include <GenericProgressCallback.h>
#include <DistanceComputationTools.h>
#include <DgmOctree.h>

//qCC_db
#include <ccAdvancedTypes.h>

class ccGenericPointCloud;
class ccPointCloud;

//! Fast Marching algorithm for planar facets extraction (qFacets plugin)
/** Extends the FastMarching class.
**/
class FastMarchingForFacetExtraction : public CCLib::FastMarching
{
public:

	//! Static entry point (helper)
	static int ExtractPlanarFacets(	ccPointCloud* theCloud,
									unsigned char octreeLevel,
									ScalarType maxError,
									CCLib::DistanceComputationTools::ERROR_MEASURES errorMeasure,
									bool useRetroProjectionError = true,
									CCLib::GenericProgressCallback* progressCb = 0,
									CCLib::DgmOctree* _theOctree = 0);

	//! Default constructor
	FastMarchingForFacetExtraction();

	//! Destructor
	virtual ~FastMarchingForFacetExtraction();

	//! Initializes the grid with a point cloud (and ist corresponding octree)
	/** The points should be associated to an (active) scalar field.
		The Fast Marching grid will have the same dimensions as
		the input octree considered at a given level of subdivision.
		\param cloud the point cloud
		\param theOctree the associated octree
		\param gridLevel the level of subdivision
		\param maxError maximum error allowed by 'propagated' facet
		\param errorMeasure error measure
		\param useRetroProjectionError whether to use retro-projection error in propagation
		\return a negative value if something went wrong
	**/
	int init(	ccGenericPointCloud* cloud,
				CCLib::DgmOctree* theOctree,
				unsigned char gridLevel,
				ScalarType maxError,
				CCLib::DistanceComputationTools::ERROR_MEASURES errorMeasure,
				bool useRetroProjectionError,
				CCLib::GenericProgressCallback* progressCb = 0);

	//! Updates a list of point flags, indicating the points alreay processed
	/** \return the number of newly flagged points
	**/
	unsigned updateFlagsTable(	ccGenericPointCloud* theCloud,
								GenericChunkedArray<1,unsigned char>& flags,
								unsigned facetIndex);

	//inherited methods (see FastMarchingAlgorithm)
	int propagate();
	bool setSeedCell(const Tuple3i& pos);

protected:

	//! A Fast Marching grid cell for planar facets extraction
	class PlanarCell : public CCLib::FastMarching::Cell
	{
	public:
		//! Default constructor
		PlanarCell()
			: Cell()
			, N(0,0,0)
			, C(0,0,0)
			, cellCode(0)
			, planarError(0)
		{}

		///! Destructor
		virtual ~PlanarCell() {}

		//! The local cell normal
		CCVector3 N;
		//! The local cell center
		CCVector3 C;
		//! the code of the equivalent cell in the octree
		CCLib::DgmOctree::OctreeCellCodeType cellCode;
		//! Cell planarity error
		ScalarType planarError;
	};

	//inherited methods (see FastMarchingAlgorithm)
	virtual float computeTCoefApprox(CCLib::FastMarching::Cell* currentCell, CCLib::FastMarching::Cell* neighbourCell) const;
	virtual int step();
	virtual void initTrialCells();
	virtual bool instantiateGrid(unsigned size) { return instantiateGridTpl<PlanarCell*>(size); }

	//! Sets the propagation timings as distances for each point
	/** \return true if ok, false otherwise
	**/
	bool setPropagationTimingsAsDistances();

	//! Adds a given cell's points to the current facet and returns the resulting RMS
	ScalarType addCellToCurrentFacet(unsigned index);

	//! Current facet points
	CCLib::ReferenceCloud* m_currentFacetPoints;

	//! Current facet error
	ScalarType m_currentFacetError;

	//! Max facet error
	ScalarType m_maxError;

	//! Error measrue
	CCLib::DistanceComputationTools::ERROR_MEASURES m_errorMeasure;

	//! Whether to use retro-projection error in propagation
	bool m_useRetroProjectionError;
};

#endif //QFACET_FAST_MARCHING_FOR_FACET_EXTRACTION_HEADER
