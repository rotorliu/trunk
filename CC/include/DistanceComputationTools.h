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

#ifndef DISTANCE_COMPUTATION_TOOLS_HEADER
#define DISTANCE_COMPUTATION_TOOLS_HEADER

//Local
#include "CCCoreLib.h"
#include "CCToolbox.h"
#include "CCConst.h"
#include "DgmOctree.h"

namespace CCLib
{

class GenericTriangle;
class GenericIndexedMesh;
class GenericCloud;
class GenericIndexedCloudPersist;
class ReferenceCloud;
class GenericProgressCallback;
struct OctreeAndMeshIntersection;

//! Several entity-to-entity distances computation algorithms (cloud-cloud, cloud-mesh, point-triangle, etc.)
class CC_CORE_LIB_API DistanceComputationTools : public CCToolbox
{
public: //distance to clouds or meshes

	//! Cloud-to-cloud "Hausdorff" distance computation parameters
	struct Cloud2CloudDistanceComputationParams
	{
		//! Level of subdivision of the octree at witch to apply the distance computation algorithm
		/** If set to 0 (default) the algorithm will try to guess the best level automatically.
		**/
		unsigned char octreeLevel;

		//! Maximum search distance (true distance won't be computed if greater)
		/** Set to -1 to deactivate (default).
			\warning Not compatible with closest point set determination (see CPSet)
		**/
		ScalarType maxSearchDist;

		//! Whether to use multi-thread or single thread mode
		/** If maxSearchDist >= 0, single thread mode is forced.
		**/
		bool multiThread;

		//! Type of local 3D modeling to use
		/** Default: NO_MODEL. Otherwise see CC_LOCAL_MODEL_TYPES.
		**/
		CC_LOCAL_MODEL_TYPES localModel;

		//! Whether to use a fixed number of neighbors or a (sphere) radius for nearest neighbours seach
		/** For local models only (i.e. ignored if localModel = NO_MODEL).
		**/
		bool useSphericalSearchForLocalModel;

		//! Number of neighbours for nearest neighbours seach (local model)
		/** For local models only (i.e. ignored if localModel = NO_MODEL).
			Ignored if useSphericalSearchForLocalModel is true.
		**/
		unsigned kNNForLocalModel;

		//! Radius for nearest neighbours seach (local model)
		/** For local models only (i.e. ignored if localModel = NO_MODEL).
			Ignored if useSphericalSearchForLocalModel is true.
		**/
		ScalarType radiusForLocalModel;

		//! Whether to use an approximation for local model computation
		/** For local models only (i.e. ignored if localModel = NO_MODEL).
			Computation is much faster but less "controlled".
		**/
		bool reuseExistingLocalModels;

		//! Container of (references to) points to store the "Closest Point Set"
		/** The Closest Point Set corresponds to (the reference to) each compared point's closest neighbour.
			\warning Not compatible with max search distance (see maxSearchDist)
		**/
		ReferenceCloud* CPSet;

		//! Whether to keep the existing distances as is (if any) or not
		/** By default, any previous distances/scalar values stored in the 'enabled' scalar field will be
			reset before computing them again.
		**/
		bool resetFormerDistances;

		//! Default constructor/initialization
		Cloud2CloudDistanceComputationParams()
			: octreeLevel(0)
			, maxSearchDist(-1.0)
			, multiThread(true)
			, localModel(NO_MODEL)
			, useSphericalSearchForLocalModel(false)
			, kNNForLocalModel(0)
			, radiusForLocalModel(0)
			, reuseExistingLocalModels(false)
			, CPSet(0)
			, resetFormerDistances(true)
		{}
	};

	//! Computes the "nearest neighbour distance" between two point clouds (formerly named "Hausdorff distance")
	/** The main algorithm and its different versions (with or without local modeling) are described in
		Daniel Girardeau-Montaut's PhD manuscript (Chapter 2, section 2.3). It is the standard way to compare
		directly two dense (and globally close) point clouds.
		\warning The current scalar field of the compared cloud should be enabled. By default it will be reset to
		NAN_VALUE but one can avoid this by definining the Cloud2CloudDistanceComputationParams::resetFormerDistances
		parameters to false. But even in this case, only values above Cloud2CloudDistanceComputationParams::maxSearchDist
		will remain untouched.
		\warning Max search distance (Cloud2CloudDistanceComputationParams::maxSearchDist) is not compatible with the
		determination the closest point set (Cloud2CloudDistanceComputationParams::CPSet)
		\param comparedCloud the compared cloud (the distances will be computed on these points)
		\param referenceCloud the reference cloud (the distances will be computed relatively to these points)
		\param params distance computation parameters
		\param progressCb the client application can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
		\param compOctree the pre-computed octree of the compared cloud (warning: both octrees must have the same cubical bounding-box - it is automatically computed if 0)
		\param refOctree the pre-computed octree of the reference cloud (warning: both octrees must have the same cubical bounding-box - it is automatically computed if 0)
		\return 0 if ok, a negative value otherwise
	**/
	static int computeCloud2CloudDistance(	GenericIndexedCloudPersist* comparedCloud,
											GenericIndexedCloudPersist* referenceCloud,
											Cloud2CloudDistanceComputationParams& params,
											GenericProgressCallback* progressCb = 0,
											DgmOctree* compOctree = 0,
											DgmOctree* refOctree = 0);

	//! Computes the distance between a point cloud and a mesh
	/** The algorithm, inspired from METRO by Cignoni et al., is described
		in Daniel Girardeau-Montaut's PhD manuscript (Chapter 2, section 2.2).
		It is the general way to compare a point cloud with a triangular mesh.
		\param pointCloud the compared cloud (the distances will be computed on these points)
		\param theMesh the reference mesh (the distances will be computed relatively to its triangles)
		\param octreeLevel the level of subdivision of the octree at witch to apply the algorithm
		\param maxSearchDist if greater than 0 (default value: '-1'), then the algorithm won't compute distances over this value (acceleration)
		\param useDistanceMap if true, the distances over "maxSearchDist" will be aproximated by a Distance Transform (acceleration)
		\param signedDistances if true, the computed distances will be signed (in this case, the Distance Transform can't used computed and therefore useDistanceMap will be ignored)
		\param flipNormals specify whether triangle normals should be computed in the 'direct' order (true) or 'indirect' (false)
		\param multiThread specify whether to use multi-thread or single thread mode (if maxSearchDist>=0, single thread mode is forced)
		\param progressCb the client application can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
		\param cloudOctree the pre-computed octree of the compared cloud (warning: its bounding box should be equal to the union of both point cloud and mesh bbs and it should be cubical - it is automatically computed if 0)
		\return 0 if ok, a negative value otherwise
	**/
	static int computeCloud2MeshDistance(	GenericIndexedCloudPersist* pointCloud,
											GenericIndexedMesh* theMesh,
											unsigned char octreeLevel,
											ScalarType maxSearchDist = -1.0,
											bool useDistanceMap = false,
											bool signedDistances = false,
											bool flipNormals = false,
											bool multiThread = true,
											GenericProgressCallback* progressCb = 0,
											DgmOctree* cloudOctree = 0);

public: //approximate distances to clouds or meshes

	//! Computes approximate distances between two point clouds
	/** This methods uses an exact Distance Transform to approximate the real distances.
		Therefore, the greater the octree level is (it is used to determine the grid step), the finer
		the result will be (but more memory and time will be needed).
		\param comparedCloud the compared cloud
		\param referenceCloud the reference cloud
		\param octreeLevel the octree level at which to compute the Distance Transform
		\param maxSearchDist max search distance (or any negative value if no max distance is defined)
		\param progressCb the client application can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
		\param compOctree the pre-computed octree of the compared cloud (warning: both octrees must have the same cubical bounding-box - it is automatically computed if 0)
		\param refOctree the pre-computed octree of the reference cloud (warning: both octrees must have the same cubical bounding-box - it is automatically computed if 0)
		\return negative error code or a positive value in case of success
	**/
	static int computeApproxCloud2CloudDistance(GenericIndexedCloudPersist* comparedCloud,
												GenericIndexedCloudPersist* referenceCloud,
												unsigned char octreeLevel,
												PointCoordinateType maxSearchDist = -PC_ONE,
												GenericProgressCallback* progressCb = 0,
												DgmOctree* compOctree = 0,
												DgmOctree* refOctree = 0);

public: //distance to simple entities (triangles, planes, etc.)

	//! Computes the distance between a point and a triangle
	/** WARNING: if not signed, the returned distance is SQUARED!
		\param P a 3D point
		\param theTriangle a 3D triangle
		\param signedDist whether to compute the signed or positive (SQUARED) distance
		\return the distance between the point and the triangle
	**/
	static ScalarType computePoint2TriangleDistance(const CCVector3* P, const GenericTriangle* theTriangle, bool signedDist);

	//! Computes the (signed) distance between a point and a plane
	/** \param P a 3D point
		\param planeEquation plane equation: [a,b,c,d] as 'ax+by+cz=d' with norm(a,bc)==1
		\return the signed distance between the point and the plane
	**/
	static ScalarType computePoint2PlaneDistance(const CCVector3* P, const PointCoordinateType* planeEquation);

	//! Error estimators
	enum ERROR_MEASURES
	{
		RMS,						/**< Root Mean Square error **/
		MAX_DIST_68_PERCENT,		/**< Max distance @ 68% (1 sigma) **/
		MAX_DIST_95_PERCENT,		/**< Max distance @ 98% (2 sigmas) **/
		MAX_DIST_99_PERCENT,		/**< Max distance @ 99% (3 sigmas) **/
		MAX_DIST,					/**< Max distance **/
	};

	//! Computes the "distance" (see ERROR_MEASURES) between a point cloud and a plane
	/** \param cloud a point cloud
		\param planeEquation plane equation: [a,b,c,d] as 'ax+by+cz=d'
		\param measureType measure type
	**/
	static ScalarType ComputeCloud2PlaneDistance(	CCLib::GenericCloud* cloud,
													const PointCoordinateType* planeEquation,
													ERROR_MEASURES measureType);

	//! Computes the maximum distance between a point cloud and a plane
	/** WARNING: this method uses the cloud global iterator
		\param cloud a point cloud
		\param planeEquation plane equation: [a,b,c,d] as 'ax+by+cz=d'
		\param percent percentage of lowest values ignored
		\return the max distance @ 'percent' % between the point and the plane
	**/
	static ScalarType ComputeCloud2PlaneRobustMax(	GenericCloud* cloud,
													const PointCoordinateType* planeEquation,
													float percent);

	//! Computes the maximum distance between a point cloud and a plane
	/** WARNING: this method uses the cloud global iterator
		\param cloud a point cloud
		\param planeEquation plane equation: [a,b,c,d] as 'ax+by+cz=d'
		\return the max distance between the point and the plane
	**/
	static ScalarType ComputeCloud2PlaneMaxDistance(GenericCloud* cloud,
													const PointCoordinateType* planeEquation);

	//! Computes the Root Mean Square (RMS) distance between a cloud and a plane
	/** Sums the squared distances between each point of the cloud and the plane, then computes the mean value.
		WARNING: this method uses the cloud global iterator
		\param cloud a point cloud
		\param planeEquation plane equation: [a,b,c,d] as 'ax+by+cz=d'
		\return the RMS of distances (or NaN if an error occurred)
	**/
	static ScalarType computeCloud2PlaneDistanceRMS(	GenericCloud* cloud,
														const PointCoordinateType* planeEquation);

	//! Returns the (squared) distance from a point to a segment
	/** \param P 3D point
		\param A first point of the segment
		\param B first point of the segment
		\param onlyOrthogonal computes distance only if P lies 'in front' of AB (returns -1.0 otherwise)
		\return squared distance (or potentially -1.0 if onlyOrthogonal is true)
	**/
	static PointCoordinateType ComputeSquareDistToSegment(	const CCVector2& P,
															const CCVector2& A,
															const CCVector2& B,
															bool onlyOrthogonal = false);

public: //other methods

	//! Computes geodesic distances over a point cloud "surface" (starting from a seed point)
	/** This method uses the FastMarching algorithm. Thereofre it needs an octree level as input
		parameter in order to create the corresponding 3D grid. The greater this level is, the finer
		the result will be, but more memory will be required as well.
		Moreover to get an interesting result the cells size should not be too small (the propagation
		will be stoped more easily on any encountered 'hole').
		\param cloud the point cloud
		\param seedPointIndex the index of the point from where to start the propagation
		\param octreeLevel the octree at which to perform the Fast Marching propagation
		\param progressCb the client application can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
		\return true if the method succeeds
	**/
	static bool computeGeodesicDistances(	GenericIndexedCloudPersist* cloud,
											unsigned seedPointIndex,
											unsigned char octreeLevel,
											GenericProgressCallback* progressCb = 0);

	//! Computes the differences between two scalar fields associated to equivalent point clouds
	/** The compared cloud should be smaller or equal to the reference cloud. Its points should be
		at the same position in space as points in the other cloud. The algorithm simply computes
		the difference between the scalar values associated to each couple of equivalent points.
		\warning The result is stored in the active scalar field (input) of the comparedCloud.
		\warning Moreover, the output scalar field should be different than the input scalar field!
		\warning Be sure to activate an OUTPUT scalar field on both clouds
		\param comparedCloud the compared cloud
		\param referenceCloud the reference cloud
		\param progressCb the client application can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
	**/
	static int diff(GenericIndexedCloudPersist* comparedCloud,
					GenericIndexedCloudPersist* referenceCloud,
					GenericProgressCallback* progressCb = 0);

	//! Return codes for DistanceComputationTools::synchronizeOctrees
	enum SOReturnCode { EMPTY_CLOUD, SYNCHRONIZED, DISJOINT, OUT_OF_MEMORY };

	//! Synchronizes (and re-build if necessary) two octrees
	/** Initializes the octrees before computing the distance between two clouds.
		Check if both octree have the same sizes and limits (in 3D) and rebuild
		them if necessary.
		\param comparedCloud the cloud corresponding to the first octree
		\param referenceCloud the cloud corresponding to the second octree
		\param comparedOctree the first octree
		\param referenceOctree the second octree
		\param maxSearchDist max search distance (or any negative value if no max distance is defined)
		\param progressCb the client method can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
		\return return code
	**/
	static SOReturnCode synchronizeOctrees(	GenericIndexedCloudPersist* comparedCloud,
											GenericIndexedCloudPersist* referenceCloud,
											DgmOctree* &comparedOctree,
											DgmOctree* &referenceOctree,
											PointCoordinateType maxSearchDist = -PC_ONE,
											GenericProgressCallback* progressCb = 0);

	//! Returns whether multi-threading (parallel) computation is supported or not
	static bool MultiThreadSupport();

protected:

	//! Intersects a mesh with a grid structure
	/** This method is used by computeCloud2MeshDistance.
		\param theIntersection a specific structure to store the result of the intersection
		\param octreeLevel the octree subdivision level corresponding to the grid
		\param progressCb the client method can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
	**/
	static int intersectMeshWithOctree(	OctreeAndMeshIntersection* theIntersection,
										unsigned char octreeLevel,
										GenericProgressCallback* progressCb = 0);

	//! Computes the distances between a point cloud and a mesh projected into a grid structure
	/** This method is used by computeCloud2MeshDistance, after intersectMeshWithOctree has been called.
		\param theIntersection a specific structure corresponding the intersection of the mesh with the grid
		\param octreeLevel the octree subdivision level corresponding to the grid
		\param signedDistances specify whether to compute signed or positive (squared) distances
		\param flipTriangleNormals if 'signedDistances' is true,  specify whether triangle normals should be computed in the 'direct' order (true) or 'indirect' (false)
		\param multiThread whether to use parallel processing or not
		\param maxSearchDist if greater than 0 (default value: '-1'), then the algorithm won't compute distances over this value (ignored if multiThread is true)
		\param progressCb the client method can get some notification of the process progress through this callback mechanism (see GenericProgressCallback)
		\return -1 if an error occurred (e.g. not enough memory) and 0 otherwise
	**/
	static int computeCloud2MeshDistanceWithOctree(	OctreeAndMeshIntersection* theIntersection,
													unsigned char octreeLevel,
													bool signedDistances,
													bool flipTriangleNormals,
													bool multiThread = false,
													ScalarType maxSearchDist = -1.0,
													GenericProgressCallback* progressCb = 0);

	//! Computes the "nearest neighbour distance" without local modeling for all points of an octree cell
	/** This method has the generic syntax of a "cellular function" (see DgmOctree::localFunctionPtr).
		Specific parameters are transmitted via the "additionalParameters" structure.
		There are 3 additional parameters :
		- (GenericCloud*) the compared cloud
		- (GenericCloud*) the reference cloud
		- (DgmOctree*) the octree corresponding to the compared cloud
		\param cell structure describing the cell on which processing is applied
		\param additionalParameters see method description
		\param nProgress optional (normalized) progress notification (per-point)
	**/
	static bool computeCellHausdorffDistance(	const DgmOctree::octreeCell& cell,
												void** additionalParameters,
												NormalizedProgress* nProgress = 0);

	//! Computes the "nearest neighbour distance" with local modeling for all points of an octree cell
	/** This method has the generic syntax of a "cellular function" (see DgmOctree::localFunctionPtr).
		Specific parameters are transmitted via the "additionalParameters" structure.
		There are 4 additional parameters :
		- (GenericCloud*) the compared cloud
		- (GenericCloud*) the reference cloud
		- (DgmOctree*) the octree corresponding to the compared cloud
		- (CC_LOCAL_MODEL_TYPES*) type of local model to apply
		\param cell structure describing the cell on which processing is applied
		\param additionalParameters see method description
		\param nProgress optional (normalized) progress notification (per-point)
	**/
	static bool computeCellHausdorffDistanceWithLocalModel(	const DgmOctree::octreeCell& cell,
															void** additionalParameters,
															NormalizedProgress* nProgress = 0);
};

}

#endif //DISTANCE_COMPUTATION_TOOLS_HEADER
