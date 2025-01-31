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

#ifndef CC_CAMERA_SENSOR_HEADER
#define CC_CAMERA_SENSOR_HEADER

//local
#include "qCC_db.h"
#include "ccSensor.h"
#include "ccOctree.h"

//CCLib
#include <ReferenceCloud.h>
#include <DgmOctree.h>

//Qt
#include <QSharedPointer>

//system
#include <set>
#include <assert.h>

class ccPointCloud;
class ccMesh;
class ccImage;
class QDir;

//! Camera (projective) sensor
class QCC_DB_LIB_API ccCameraSensor : public ccSensor
{
public: //general

	//! Intrinsic parameters of the camera sensor
	struct QCC_DB_LIB_API IntrinsicParameters
	{
		//! Default initializer
		IntrinsicParameters();

		//! Helper: initializes a IntrinsicParameters structure with the default Kinect parameters
		static void GetKinectDefaults(IntrinsicParameters& params);

		float focal_pix;			/**< focal length (in pixels)**/
		float pixelSize_mm[2];		/**< sensor pixel size (in real dimension, e.g. mm) **/
		float skew;					/**< skew **/
		float vFOV_rad;				/**< vertical field of view (in Radians) **/
		float zNear_mm;				/**< Near plane position **/
		float zFar_mm;				/**< Far plane position **/
		int arrayWidth;				/**< Pixel array width (in pixels) **/
		int arrayHeight;			/**< Pixel array height (in pixels) **/
	};

	//! Supported distortion models
	enum DistortionModel {	NO_DISTORTION_MODEL = 0,			/**< no distortion model **/
							SIMPLE_RADIAL_DISTORTION = 1,		/**< simple radial distortion model (k1,k2) **/
							BROWN_DISTORTION = 2 };				/**< Brown's distortion model (k1,k2,k3,etc.) **/

	//! Lens distortion parameters (interface)
	struct LensDistortionParameters
	{
		//! Virtual destructor
		virtual ~LensDistortionParameters() {}

		//! Returns distortion model type
		virtual DistortionModel getModel() const = 0;

		//! Shared pointer type
		typedef QSharedPointer<LensDistortionParameters> Shared;
	};
	
	//! Simple radial distortion model
	struct QCC_DB_LIB_API RadialDistortionParameters : LensDistortionParameters
	{
		//! Default initializer
		RadialDistortionParameters() : k1(0), k2(0) {}
		
		//inherited from LensDistortionParameters
		inline DistortionModel getModel() const { return SIMPLE_RADIAL_DISTORTION; }

		//! 1st radial distortion coefficient
		float k1;
		//! 2nd radial distortion coefficient
		float k2;
	};

	//! Brown's distortion model + Linear Disparity
	/**	To know how to use K & P parameters, please read:
		"Decentering Distortion of Lenses", Duane C. Brown 
		To know how to use the linearDisparityParams parameter (kinect attribute), please read:
		"Accuracy and Resolution of Kinect Depth Data for Indoor Mapping Applications", K. Khoshelham and S.O. Elberink
	**/
	struct QCC_DB_LIB_API BrownDistortionParameters : LensDistortionParameters
	{
		//! Default initializer
		BrownDistortionParameters();

		//inherited from LensDistortionParameters
		inline DistortionModel getModel() const { return BROWN_DISTORTION; }

		//! Helper: initializes a IntrinsicParameters structure with the default Kinect parameters
		static void GetKinectDefaults(BrownDistortionParameters& params);

		float principalPointOffset[2];		/**< offset of the principal point (in meters) **/
		float linearDisparityParams[2];		/**< contains A and B where : 1/Z = A*d' + B (with Z=depth and d'=normalized disparity) **/
		float K_BrownParams[3];				/**< radial parameters Brown's distortion model **/
		float P_BrownParams[2];				/**< tangential parameters Brown's distortion model **/
	};

	//! Frustum information structure
	/** Used to draw the frustrum associated to a camera sensor.
	**/
	struct QCC_DB_LIB_API FrustumInformation
	{
		//! Default initializer
		FrustumInformation();
		//! Destructor
		~FrustumInformation();

		//! Reserves memory for the frustrum corners cloud
		/** Warning: reset the cloud contents!
		**/
		bool initFrustrumCorners();
		//! Creates the frustrum hull mesh
		/** The frustrum corners must have already been setup!
			\return success
		**/
		bool initFrustrumHull();

		bool isComputed;
		bool drawFrustum;
		bool drawSidePlanes;
		ccPointCloud* frustumCorners;
		ccMesh* frustrumHull;
		CCVector3 center;					/**< center of the circumscribed sphere **/
	};

	//! Default constructor
	ccCameraSensor();
	//! Copy constructor
	ccCameraSensor(const ccCameraSensor& sensor);
	//! Constructor with given intrinsic parameters (and optional uncertainty parameters)
	ccCameraSensor(const IntrinsicParameters& iParams);

	//! Destructor
	virtual ~ccCameraSensor();

	//inherited from ccHObject
	virtual CC_CLASS_ENUM getClassID() const { return CC_TYPES::CAMERA_SENSOR; }
	virtual bool isSerializable() const { return true; }
	virtual ccBBox getOwnBB(bool withGLFeatures = false);
	virtual ccBBox getOwnFitBB(ccGLMatrix& trans);

	//inherited from ccSensor
	virtual bool applyViewport(ccGenericGLDisplay* win = 0);

public: //getters and setters

	//! Sets focal (in pixels)
	void setFocal_pix(float f_pix);
	//! Returns focal (in pixels)
	inline float getFocal_pix() const { return m_intrinsicParams.focal_pix; }

	//! Sets the (vertical) field of view in radians
	void setVerticalFov_rad(float fov_rad);
	//! Returns the (vertical) field of view in radians
	inline float getVerticalFov_rad() const { return m_intrinsicParams.vFOV_rad; }

	//! Returns intrinsic parameters
	const IntrinsicParameters& getIntrinsicParameters() const { return m_intrinsicParams; }
	//! Sets intrinsic parameters 
	void setIntrinsicParameters(const IntrinsicParameters& params);

	//! Returns uncertainty parameters
	const LensDistortionParameters::Shared& getDistortionParameters() const { return m_distortionParams; }
	//! Sets uncertainty parameters 
	void setDistortionParameters(LensDistortionParameters::Shared params) { m_distortionParams = params; }

	//! Returns the camera projection matrix
	/** \param[out] matrix projection matrix (if the method returns true)
		\return whether the matrix could be computed or not (probably due to wrong parameters)
	**/
	bool getProjectionMatrix(ccGLMatrix& matrix);
	
public: //frustrum display

	//! Returns whether the frustum should be displayed or not
	inline bool frustrumIsDrawn() const { return m_frustrumInfos.drawFrustum; }

	//! Sets whether the frustum should be displayed or not
	inline void drawFrustrum(bool state) { m_frustrumInfos.drawFrustum = state; }

	//! Returns whether the frustum planes should be displayed or not
	inline bool frustrumPlanesAreDrawn() const { return m_frustrumInfos.drawSidePlanes; }

	//! Sets whether the frustum planes should be displayed or not
	inline void drawFrustrumPlanes(bool state) { m_frustrumInfos.drawSidePlanes = state; }

public: //coordinate systems conversion methods

	//! Computes the coordinates of a 3D point in the global coordinate system knowing its coordinates in the sensor coordinate system.
	/** \param localCoord local coordinates of the 3D point (input)
		\param globalCoord corresponding global coordinates of the 3D point (output)
	**/
	bool fromLocalCoordToGlobalCoord(const CCVector3& localCoord, CCVector3& globalCoord) const;

	//! Computes the coordinates of a 3D point in the sensor coordinate system knowing its coordinates in the global coordinate system.
	/** \param globalCoord global coordinates of the 3D point (input)
		\param localCoord corresponding local coordinates of the 3D point (output)
	**/
	bool fromGlobalCoordToLocalCoord(const CCVector3& globalCoord, CCVector3& localCoord) const;
	
	//! Computes the coordinates of a 3D point in the global coordinate system knowing its coordinates in the sensor coordinate system.
	/** \param localCoord local coordinates of the 3D point (input)
		\param imageCoord image coordinates of the projected point on the image (output) --> !! Note that the first index is (0,0) and the last (width-1,height-1) !!
		\param withLensError to take lens distortion into account
		\return if operation has succeded (typically, errors occur when the projection of the initial 3D points is not into the image boundaries, or when the 3D point is behind the camera)
	**/
	bool fromLocalCoordToImageCoord(const CCVector3& localCoord, CCVector2& imageCoord, bool withLensError = true) const;

	//! Computes the coordinates of a 3D point in the sensor coordinate system knowing its coordinates in the global coordinate system.
	/** \param imageCoord image coordinates of the pixel (input) --> !! Note that the first index is (0,0) and the last (width-1,height-1) !!
		\param localCoord local coordinates of the corresponding 3D point (output)
		\param depth depth of the output pixel relatively to the camera center
		\param withLensCorrection if we want to correct the initial pixel coordinates with the lens correction formula
		\return if operation has succeded (typically, errors occur when the initial pixel coordinates are not into the image boundaries)
	**/
	bool fromImageCoordToLocalCoord(const CCVector2& imageCoord, CCVector3& localCoord, PointCoordinateType depth, bool withLensCorrection = true) const;

	//! Computes the coordinates of a 3D point in the image knowing its coordinates in the global coordinate system.
	/** \param globalCoord global coordinates of the 3D point
		\param imageCoord to get back the image coordinates of the projected 3D point --> !! Note that the first index is (0,0) and the last (width-1,height-1) !!
		\param withLensError to take lens distortion into account
		\return if operation has succeded (typically, errors occur when the projection of the initial 3D points is not into the image boundaries, or when the 3D point is behind the camera)
	**/ 
	bool fromGlobalCoordToImageCoord(const CCVector3& globalCoord, CCVector2& imageCoord, bool withLensError = true) const;
	
	//! Computes the global coordinates of a 3D points from its 3D coordinates (pixel position in the image)
	/** \param imageCoord image coordinates of the pixel (input) --> !! Note that the first index is (0,0) and the last (width-1,height-1) !!
		\param globalCoord global coordinates of the corresponding 3D point (output)
		\param z0 altitude of the output pixel
		\param withLensCorrection if we want to correct the initial pixel coordinates with the lens correction formula
		\return if operation has succeded (typically, errors occur when the initial pixel coordinates are not into the image boundaries)
	**/
	bool fromImageCoordToGlobalCoord(const CCVector2& imageCoord, CCVector3& globalCoord, PointCoordinateType z0, bool withLensCorrection = true) const;

	//! Apply the Brown's lens correction to the real projection (through a lens) of a 3D point in the image
	/**	\warning Only works with Brown's distortion model for now (see BrownDistortionParameters).
		\param real real 2D coordinates of a pixel (asumming that this pixel coordinate is obtained after projection through a lens) (input) !! Note that the first index is (0,0) and the last (width-1,height-1) !!
		\param ideal after applying lens correction (output) --> !! Note that the first index is (0,0) and the last (width-1,height-1) !!
	**/
	bool fromRealImCoordToIdealImCoord(const CCVector2& real, CCVector2& ideal) const;

	//! Knowing the ideal projection of a 3D point, computes what would be the real projection (through a lens)
	/** \warning The first pixel is (0,0) and the last (width-1,height-1)
		\param[in] ideal 2D coordinates of the ideal projection
		\param[out] real what would be the real 2D coordinates of the projection trough a lens
	**/
	//TODO
	//bool fromIdealImCoordToRealImCoord(const CCVector2& ideal, CCVector2& real) const;

public: //orthorectification tools

	//! Key point (i.e. mapping between a point in a 3D cloud and a pixel in an image)
	struct KeyPoint
	{
		//! 2D 'x' coordinate (in pixels)
		float x;
		//! 2D 'y' coordinate (in pixels)
		float y;
		//! Index in associated point cloud
		unsigned index;

		//! Default constructor
		KeyPoint()
			: x(0)
			, y(0)
			, index(0)
		{}

		//! Constructor from a pixel and its index in associated cloud
		KeyPoint(float Px, float Py, unsigned indexInCloud)
			: x(Px)
			, y(Py)
			, index(indexInCloud)
		{}
	};

	//! Projective ortho-rectification of an image (as cloud)
	/** Requires at least 4 key points!
		\param image input image
		\param keypoints3D keypoints in 3D
		\param keypointsImage corresponding keypoints in image
		\return ortho-rectified image as a point cloud
	**/
	ccPointCloud* orthoRectifyAsCloud(	const ccImage* image,
										CCLib::GenericIndexedCloud* keypoints3D,
										std::vector<KeyPoint>& keypointsImage) const;

	//! Projective ortho-rectification of an image (as image)
	/** Requires at least 4 key points!
		\param image input image
		\param keypoints3D keypoints in 3D
		\param keypointsImage corresponding keypoints in image
		\param pixelSize pixel size (auto if -1)
		\param minCorner (optional) outputs 3D min corner (2 values)
		\param maxCorner (optional) outputs 3D max corner (2 values)
		\param realCorners (optional) image real 3D corners (4*2 values)
		\return ortho-rectified image
	**/
	ccImage* orthoRectifyAsImage(	const ccImage* image,
									CCLib::GenericIndexedCloud* keypoints3D,
									std::vector<KeyPoint>& keypointsImage,
									double& pixelSize,
									double* minCorner = 0,
									double* maxCorner = 0,
									double* realCorners = 0) const;

	//! Direct ortho-rectification of an image (as image)
	/** No keypoint is required. The user must specify however the
		orthorectification 'altitude'.
		\param image input image
		\param altitude orthorectification altitude
		\param keypointsImage corresponding keypoints in image
		\param pixelSize pixel size (auto if -1)
		\param undistortImages whether images should be undistorted or not
		\param minCorner (optional) outputs 3D min corner (2 values)
		\param maxCorner (optional) outputs 3D max corner (2 values)
		\param realCorners (optional) image real 3D corners (4*2 values)
		\return ortho-rectified image
	**/
	ccImage* orthoRectifyAsImageDirect(	const ccImage* image,
										PointCoordinateType altitude,
										double& pixelSize,
										bool undistortImages = true,
										double* minCorner = 0,
										double* maxCorner = 0,
										double* realCorners = 0) const;

	//! Projective ortho-rectification of multiple images (as image files)
	/** \param images set of N calibrated images (i.e. images with their associated sensor)
		\param a {a0, a1, a2} triplets for all images (size: 3*N)
		\param b {b0, b1, b2} triplets for all images (size: 3*N)
		\param c {c0(=1), c1, c2} triplets for all images (size: 3*N)
		\param maxSize output image(s) max dimension
		\param outputDir output directory for resulting images (is successful)
		\param[out] orthoRectifiedImages resulting images (is successful)
		\param[out] relativePos relative positions (relatively to first image)
		\return true if successful
	**/
	static bool OrthoRectifyAsImages(std::vector<ccImage*> images,
									double a[], double b[], double c[],
									unsigned maxSize,
									QDir* outputDir = 0,
									std::vector<ccImage*>* orthoRectifiedImages = 0,
									std::vector<std::pair<double,double> >* relativePos = 0);

	//! Computes ortho-rectification parameters for a given image
	/** Requires at least 4 key points!
		Collinearity equation:
		* x'i = (a0+a1.xi+a2.yi)/(1+c1.xi+c2.yi)
		* y'i = (b0+b1.xi+b2.yi)/(1+c1.xi+c2.yi)
		\param keypoints3D keypoints in 3D
		\param keypointsImage corresponding keypoints in image
		\param a a0, a1 & a2 parameters
		\param b b0, b1 & b2 parameters
		\param c c0(=1), c1 & c2 parameters
		\return success
	**/
	bool computeOrthoRectificationParams(	const ccImage* image,
											CCLib::GenericIndexedCloud* keypoints3D,
											std::vector<KeyPoint>& keypointsImage,
											double a[3],
											double b[3],
											double c[3]) const;

public: //misc

	//! Computes the uncertainty of a point knowing its depth (from the sensor view point) and pixel projection coordinates
	/**	\warning Only works with Brown's distortion model for now (see BrownDistortionParameters).
		\param pixel coordinates of the pixel where the 3D points is projected --> !! Note that the first index is (0,0) and the last (width-1,height-1) !!
		\param depth depth from sensor center to 3D point (must be positive)
		\param sigma uncertainty vector (along X, Y and Z) 
		\return operation has succeded (typically, errors occur when the initial pixel coordinates are not into the image boundaries, or when the depth of the 3D point is negative)
	**/
	bool computeUncertainty(const CCVector2& pixel, const float depth, Vector3Tpl<ScalarType>& sigma) const;
	
	//! Computes the coordinates of a 3D point in the sensor coordinate system knowing its coordinates in the global coordinate system.
	/**	\warning Only works with Brown's distortion model for now (see BrownDistortionParameters).
		\param points the points we want to compute the uncertainty
		\param accuracy to get back the uncertainty
		//TODO lensDistortion if we want to take the lens distortion into consideration
		\return success
	**/ 
	bool computeUncertainty(CCLib::ReferenceCloud* points, std::vector< Vector3Tpl<ScalarType> >& accuracy/*, bool lensDistortion*/);

	//! Undistorts an image based on the sensor distortion parameters
	/** \warning Only works with the simple radial distortion model for now (see RadialDistortionParameters).
		\param image input image
		\return undistorted image (or a null one if an error occurred)
	**/
	QImage undistort(const QImage& image) const;

	//! Undistorts an image based on the sensor distortion parameters
	/** \warning Only works with the simple radial distortion model for now (see RadialDistortionParameters).
		\param image input image
		\param inplace whether the undistortion should be applied in place or not
		\return undistorted image (maybe the same as the input image if inplace is true, or even a null pointer if an error occurred)
	**/
	ccImage* undistort(ccImage* image, bool inplace = true) const;
	
	//! Tests if a 3D point is in the field of view of the camera.
	/** \param globalCoord global coordinates of the 3D point
		//TODO withLensCorrection if we want to take the lens distortion into consideration
		\return if operation has succeded
	**/ 
	bool isGlobalCoordInFrustrum(const CCVector3& globalCoord/*, bool withLensCorrection*/);

	//! Filters an octree : all the box visible in the frustum will be drawn in red.
	/** \param octree Octree
		\param inCameraFrustrum indices of points in the frustrum
	**/
	void filterOctree(ccOctree* octree, std::vector<unsigned>& inCameraFrustrum);
	
	//! Compute the coefficients of the 6 planes frustrum in the global coordinates system (normal vector are headed the frustrum inside), the edges direction vectors and the frustrum center
	/** \param planeCoefficients coefficients of the six planes
		\param edges direction vectors of the frustrum edges (there are 12 edges but some of them are colinear)
		\param ptsFrustrum the 8 frustrum corners in the global coordinates system
		\param center center of the the frustrum circumscribed sphere 
		\return success
	**/
	bool computeGlobalPlaneCoefficients(float planeCoefficients[6][4], CCVector3 ptsFrustrum[8], CCVector3 edges[6], CCVector3& center);

public: //helpers

	//! Helper: converts camera focal from pixels to mm
	static float ConvertFocalPixToMM(float focal_pix, float ccdPixelHeight_mm);

	//! Helper: converts camera focal from mm to pixels
	static float ConvertFocalMMToPix(float focal_mm, float ccdPixelHeight_mm);

	//! Helper: deduces camera f.o.v. (in radians) from focal (in pixels)
	static float ComputeFovRadFromFocalPix(float focal_pix, int imageHeight_pix);

	//! Helper: deduces camera f.o.v. (in radians) from focal (in mm)
	static float ComputeFovRadFromFocalMm(float focal_mm, float ccdHeight_mm);
	
protected:

	//! Used internally for display
	CCVector3 computeUpperLeftPoint() const;

	//! Compute the projection matrix (from intrinsic parameters)
	void computeProjectionMatrix();

	//! Computes the eight corners of the frustrum
	/** \return success
	**/
	bool computeFrustumCorners();

	//Inherited from ccHObject
	virtual bool toFile_MeOnly(QFile& out) const;
	virtual bool fromFile_MeOnly(QFile& in, short dataVersion, int flags);
	virtual void drawMeOnly(CC_DRAW_CONTEXT& context);

	//! Camera intrinsic parameters
	IntrinsicParameters m_intrinsicParams;

	//! Lens distortion parameters 
	LensDistortionParameters::Shared m_distortionParams;

	//! Frustrum information structure
	/** Used to draw it properly.
	**/
	FrustumInformation m_frustrumInfos;

	//! Intrinsic parameters matrix
	ccGLMatrix m_projectionMatrix;
	//! Whether the intrinsic matrix is valid or not
	bool m_projectionMatrixIsValid;
};

class ccOctreeFrustrumIntersector
{
public:
	//! Definition of the state of a cell compared to a frustrum
	/** OUTSIDE : the celle is completely outside the frustrum (no intersection, no inclusion)
		INSIDE : the cell is completely inside the frustrum
		INTERSECT : other cases --> the frustrum is completely inside the cell OR the frustrum and the cell have an intersection
	**/
	enum OctreeCellVisibility
	{
		CELL_OUTSIDE_FRUSTRUM	= 0,
		CELL_INSIDE_FRUSTRUM	= 1,
		CELL_INTERSECT_FRUSTRUM	= 2,
	};

	//! Default constructor
	ccOctreeFrustrumIntersector()
		: m_associatedOctree(0)
	{
	}

	//! Prepares structure for frustrum filtering
	bool build(CCLib::DgmOctree* octree);

	//! Returns the cell visibility
	OctreeCellVisibility positionFromFrustum(CCLib::DgmOctree::OctreeCellCodeType truncatedCode, unsigned char level) const
	{
		assert(m_associatedOctree);

		std::set<CCLib::DgmOctree::OctreeCellCodeType>::const_iterator got = m_cellsInFrustum[level].find(truncatedCode);
		if (got != m_cellsInFrustum[level].end())
			return CELL_INSIDE_FRUSTRUM;
		got = m_cellsIntersectFrustum[level].find(truncatedCode);
		if (got != m_cellsIntersectFrustum[level].end())
			return CELL_INTERSECT_FRUSTRUM;
		return CELL_OUTSIDE_FRUSTRUM;
	}

	//! Compute intersection betwen the octree and a frustrum and send back the indices of 3D points inside the frustrum or in cells interescting it. 
	/** Every cells of each level of the octree will be classified as INSIDE, OUTSIDE or INTERSECTING the frustrum. 
		Their truncated code are then stored in m_cellsInFrustum (for cells INSIDE) or m_cellsIntersectFrustum (for 
		cells INTERSECTING).
		\param pointsToTest contains the indice and 3D position (global coordinates system) of every 3D points stored in an INTERSECTING cell
		\param inCameraFrustrum contains the indice of every 3D points stored in an INSIDE cell
		\param planesCoefficients coefficients (a, b, c and d) of the six frustrum planes (0:right, 1:bottom, 2:left, 3:top, 4:near, 5:far)
		\param ptsFrustrum 3D coordinates of the eight corners of the frustrum (global coordinates sytem)
		\param edges 3D coordinates (global coordinates sytem) of the six director vector of the frustrum edges
		\param center 3D coordinates of the frustrum center (global coordinates sytem) ; this is the center of the circumscribed sphere
	**/
	void computeFrustumIntersectionWithOctree(	std::vector< std::pair<unsigned, CCVector3> >& pointsToTest,
												std::vector<unsigned>& inCameraFrustrum,
												const float planesCoefficients[6][4],
												const CCVector3 ptsFrustrum[8],
												const CCVector3 edges[6],
												const CCVector3& center);
	
	//! Compute intersection betwen the octree and the height children cells of a parent cell. 
	/** \param level current level
		\param parentTruncatedCode truncated code of the parent cell (at level-1)
		\param parentResult contains in which class the parent cell has been classified (OUTSIDE, INTERSECTING, INSIDE)
		\param planesCoefficients coefficients (a, b, c and d) of the six frustrum planes (0:right, 1:bottom, 2:left, 3:top, 4:near, 5:far)
		\param ptsFrustrum 3D coordinates of the eight corners of the frustrum (global coordinates sytem)
		\param edges 3D coordinates (global coordinates sytem) of the six director vector of the frustrum edges
		\param center 3D coordinates of the frustrum center (global coordinates sytem) ; this is the center of the circumscribed sphere
	**/
	void computeFrustumIntersectionByLevel(	unsigned char level,
											CCLib::DgmOctree::OctreeCellCodeType parentTruncatedCode,
											OctreeCellVisibility parentResult,
											const float planesCoefficients[6][4],
											const CCVector3 ptsFrustrum[8],
											const CCVector3 edges[6],
											const CCVector3& center);
	
	//! Separating Axis Test
	/** See "Detecting intersection of a rectangular solid and a convex polyhedron" of Ned Greene 
		See	"OBBTree: A Hierarchical Structure for Rapid Interference Detection" of S. Gottschalk, M. C. Lin and D. Manocha
		\param bbMin minimum coordinates of the cell
		\param bbMax maximum coordinates of the cell
		\param planesCoefficients coefficients (a, b, c and d) of the six frustrum planes (0:right, 1:bottom, 2:left, 3:top, 4:near, 5:far)
		\param frustrumCorners 3D coordinates of the eight corners of the frustrum (global coordinates sytem)
		\param frustrumEdges 3D coordinates (global coordinates sytem) of the six director vector of the frustrum edges
		\param frustrumCenter 3D coordinates of the frustrum center (global coordinates sytem) ; this is the center of the circumscribed sphere
	**/
	OctreeCellVisibility separatingAxisTest(const CCVector3& bbMin,
											const CCVector3& bbMax,
											const float planesCoefficients[6][4],
											const CCVector3 frustrumCorners[8],
											const CCVector3 frustrumEdges[6],
											const CCVector3& frustrumCenter);

protected:

	CCLib::DgmOctree* m_associatedOctree;

	// contains the truncated code of the cells built in the octree
	std::set<CCLib::DgmOctree::OctreeCellCodeType> m_cellsBuilt[CCLib::DgmOctree::MAX_OCTREE_LEVEL+1];
	// contains the truncated code of the cells INSIDE the frustrum
	std::set<CCLib::DgmOctree::OctreeCellCodeType> m_cellsInFrustum[CCLib::DgmOctree::MAX_OCTREE_LEVEL+1];
	// contains the truncated code of the cells INTERSECTING the frustrum
	std::set<CCLib::DgmOctree::OctreeCellCodeType> m_cellsIntersectFrustum[CCLib::DgmOctree::MAX_OCTREE_LEVEL+1];
};


#endif //CC_CAMERA_SENSOR_HEADER
