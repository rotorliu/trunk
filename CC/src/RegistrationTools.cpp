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

#include "RegistrationTools.h"

//local
#include "SquareMatrix.h"
#include "GenericProgressCallback.h"
#include "GenericCloud.h"
#include "GenericIndexedCloudPersist.h"
#include "ReferenceCloud.h"
#include "DgmOctree.h"
#include "DistanceComputationTools.h"
#include "CCConst.h"
#include "CloudSamplingTools.h"
#include "ScalarFieldTools.h"
#include "NormalDistribution.h"
#include "ManualSegmentationTools.h"
#include "GeometricalAnalysisTools.h"
#include "KdTree.h"
#include "SimpleCloud.h"
#include "Garbage.h"

//system
#include <time.h>
#include <algorithm>
#include <assert.h>

using namespace CCLib;

void RegistrationTools::FilterTransformation(	const ScaledTransformation& inTrans,
												int filters,
												ScaledTransformation& outTrans )
{
	outTrans = inTrans;

	//filter translation
	if (filters & SKIP_TRANSLATION)
	{
		if (filters & SKIP_TX)
			outTrans.T.x = 0;
		if (filters & SKIP_TY)
			outTrans.T.y = 0;
		if (filters & SKIP_TZ)
			outTrans.T.z = 0;
	}

	//filter rotation
	if (inTrans.R.isValid() && (filters & SKIP_ROTATION))
	{
		const CCLib::SquareMatrix R(inTrans.R); //copy it in case inTrans and outTrans are the same!
		outTrans.R.toIdentity();
		if (filters & SKIP_RYZ) //keep only the rotation component around X
		{
			//we use a specific Euler angles convention here
			if (R.getValue(0,2) < 1.0)
			{
				PointCoordinateType phi = -asin(R.getValue(0,2));
				PointCoordinateType cos_phi = cos(phi);
				PointCoordinateType theta = atan2(R.getValue(1,2)/cos_phi,R.getValue(2,2)/cos_phi);
				PointCoordinateType cos_theta = cos(theta);
				PointCoordinateType sin_theta = sin(theta);

				outTrans.R.setValue(1,1,cos_theta);
				outTrans.R.setValue(2,2,cos_theta);
				outTrans.R.setValue(2,1,sin_theta);
				outTrans.R.setValue(1,2,-sin_theta);
			}
			else
			{
				//simpler/faster to ignore this (very) specific case!
			}
		}
		else if (filters & SKIP_RXZ) //keep only the rotation component around Y
		{
			//we use a specific Euler angles convention here
			if (R.getValue(2,1) < 1.0)
			{
				PointCoordinateType theta = asin(R.getValue(2,1));
				PointCoordinateType cos_theta = cos(theta);
				PointCoordinateType phi = atan2(-R.getValue(2,0)/cos_theta,R.getValue(2,2)/cos_theta);
				PointCoordinateType cos_phi = cos(phi);
				PointCoordinateType sin_phi = sin(phi);

				outTrans.R.setValue(0,0,cos_phi);
				outTrans.R.setValue(2,2,cos_phi);
				outTrans.R.setValue(0,2,sin_phi);
				outTrans.R.setValue(2,0,-sin_phi);
			}
			else
			{
				//simpler/faster to ignore this (very) specific case!
			}
		}
		else if (filters & SKIP_RXY) //keep only the rotation component around Z
		{
			//we use a specific Euler angles convention here
			if (R.getValue(2,0) < 1.0)
			{
				PointCoordinateType theta_rad = -asin(R.getValue(2,0));
				PointCoordinateType cos_theta = cos(theta_rad);
				PointCoordinateType phi_rad = atan2(R.getValue(1,0)/cos_theta, R.getValue(0,0)/cos_theta);
				PointCoordinateType cos_phi	= cos(phi_rad);
				PointCoordinateType sin_phi	= sin(phi_rad);

				outTrans.R.setValue(0,0,cos_phi);
				outTrans.R.setValue(1,1,cos_phi);
				outTrans.R.setValue(1,0,sin_phi);
				outTrans.R.setValue(0,1,-sin_phi);
			}
			else
			{
				//simpler/faster to ignore this (very) specific case!
			}
		}
	}
}

struct Model
{
	Model() : cloud(0), weights(0) {}
	Model(const Model& m) : cloud(m.cloud), weights(m.weights) {}
	GenericIndexedCloudPersist* cloud;
	ScalarField* weights;
};

struct Data
{
	Data() : cloud(0), rotatedCloud(0), weights(0), CPSet(0) {}
	Data(const Data& d) : cloud(d.cloud), rotatedCloud(d.rotatedCloud), weights(d.weights), CPSet(d.CPSet) {}
	ReferenceCloud* cloud;
	SimpleCloud* rotatedCloud;
	ScalarField* weights;
	ReferenceCloud* CPSet;
};

ICPRegistrationTools::RESULT_TYPE ICPRegistrationTools::RegisterClouds(	GenericIndexedCloudPersist* inputModelCloud,
																		GenericIndexedCloudPersist* inputDataCloud,
																		ScaledTransformation& transform,
																		CONVERGENCE_TYPE convType,
																		double minRMSDecrease,
																		unsigned nbMaxIterations,
																		double& finalRMS,
																		unsigned& finalPointCount,
																		bool adjustScale/*=false*/,
																		GenericProgressCallback* progressCb/*=0*/,
																		bool filterOutFarthestPoints/*=false*/,
																		unsigned samplingLimit/*=20000*/,
																		double finalOverlapRatio/*=1.0*/,
																		ScalarField* inputModelWeights/*=0*/,
																		ScalarField* inputDataWeights/*=0*/,
																		int filters/*=SKIP_NONE*/)
{
	assert(inputModelCloud && inputDataCloud);

	//hopefully the user will understand it's not possible ;)
	finalRMS = -1.0;

	Garbage<GenericIndexedCloudPersist> cloudGarbage;
	Garbage<ScalarField> sfGarbage;

	//MODEL CLOUD (reference, won't move)
	Model model;
	{
		//we resample the cloud if it's too big (speed increase)
		if (inputModelCloud->size() > samplingLimit)
		{
			ReferenceCloud* subModelCloud = CloudSamplingTools::subsampleCloudRandomly(inputModelCloud,samplingLimit);
			if (!subModelCloud)
			{
				//not enough memory
				return ICP_ERROR_NOT_ENOUGH_MEMORY;
			}
			cloudGarbage.add(subModelCloud);
			
			//if we need to resample the weights as well
			if (inputModelWeights)
			{
				model.weights = new ScalarField("ResampledModelWeights");
				sfGarbage.add(model.weights);

				unsigned destCount = subModelCloud->size();
				if (model.weights->resize(destCount))
				{
					for (unsigned i = 0; i < destCount; ++i)
					{
						unsigned pointIndex = subModelCloud->getPointGlobalIndex(i);
						model.weights->setValue(i,inputModelWeights->getValue(pointIndex));
					}
					model.weights->computeMinAndMax();
				}
				else
				{
					//not enough memory
					return ICP_ERROR_NOT_ENOUGH_MEMORY;
				}
			}
			model.cloud = subModelCloud;
		}
		else
		{
			//we use the input cloud and weights
			model.cloud = inputModelCloud;
			model.weights = inputModelWeights;
		}
	}
	assert(model.cloud);

	//DATA CLOUD (will move)
	Data data;
	{
		//we also want to use the same number of points for registration as initially defined by the user!
		unsigned dataSamplingLimit = finalOverlapRatio != 1.0 ? static_cast<unsigned>(samplingLimit / finalOverlapRatio) : samplingLimit;

		//we resample the cloud if it's too big (speed increase)
		if (inputDataCloud->size() > dataSamplingLimit)
		{
			data.cloud = CloudSamplingTools::subsampleCloudRandomly(inputDataCloud,dataSamplingLimit);
			if (!data.cloud)
			{
				return ICP_ERROR_NOT_ENOUGH_MEMORY;
			}
			cloudGarbage.add(data.cloud);

			//if we need to resample the weights as well
			if (inputDataWeights)
			{
				data.weights = new ScalarField("ResampledDataWeights");
				sfGarbage.add(data.weights);
				
				unsigned destCount = data.cloud->size();
				if (data.weights->resize(destCount))
				{
					for (unsigned i = 0; i < destCount; ++i)
					{
						unsigned pointIndex = data.cloud->getPointGlobalIndex(i);
						data.weights->setValue(i,inputDataWeights->getValue(pointIndex));
					}
					data.weights->computeMinAndMax();
				}
				else
				{
					//not enough memory
					return ICP_ERROR_NOT_ENOUGH_MEMORY;
				}
			}
		}
		else //no need to resample
		{
			//we still create a 'fake' reference cloud with all the points
			data.cloud = new ReferenceCloud(inputDataCloud);
			cloudGarbage.add(data.cloud);
			if (!data.cloud->addPointIndex(0,inputDataCloud->size()))
			{
				//not enough memory
				return ICP_ERROR_NOT_ENOUGH_MEMORY;
			}
			//we use the input weights
			data.weights = inputDataWeights;
		}

		//eventually we'll need a scalar field on the data cloud
		if (!data.cloud->enableScalarField())
		{
			//not enough memory
			return ICP_ERROR_NOT_ENOUGH_MEMORY;
		}
	}
	assert(data.cloud);

	//for partial overlap
	unsigned maxOverlapCount = 0;
	std::vector<ScalarType> overlapDistances;
	if (finalOverlapRatio < 1.0)
	{
		//we pre-allocate the memory to sort distance values later
		try
		{
			overlapDistances.resize(data.cloud->size());
		}
		catch (const std::bad_alloc&)
		{
			//not enough memory
			return ICP_ERROR_NOT_ENOUGH_MEMORY;
		}
		maxOverlapCount = static_cast<unsigned>(finalOverlapRatio*data.cloud->size());
		assert(maxOverlapCount != 0);
	}

	//Closest Point Set (see ICP algorithm)
	data.CPSet = new ReferenceCloud(model.cloud);
	cloudGarbage.add(data.CPSet);

	//per-point couple weights
	ScalarField* coupleWeights = 0;
	if (model.weights || data.weights)
	{
		coupleWeights = new ScalarField("CoupleWeights");
		sfGarbage.add(coupleWeights);
	}

	//we compute the initial distance between the two clouds (and the CPSet by the way)
	{
		//data.cloud->forEach(ScalarFieldTools::SetScalarValueToNaN); //DGM: done automatically in computeCloud2CloudDistance now
		DistanceComputationTools::Cloud2CloudDistanceComputationParams c2cDistParams;
		c2cDistParams.CPSet = data.CPSet;
		if (DistanceComputationTools::computeCloud2CloudDistance(data.cloud,model.cloud,c2cDistParams,progressCb) < 0)
		{
			//an error occurred during distances computation...
			return ICP_ERROR_DIST_COMPUTATION;
		}
	}

	FILE* fTraceFile = 0;
#ifdef _DEBUG
	fTraceFile = fopen("registration_trace_log.csv","wt");
	if (fTraceFile)
		fprintf(fTraceFile,"Iteration; RMS; Point count;\n");
#endif

	double lastStepRMS = -1.0, initialDeltaRMS = -1.0;
	ScaledTransformation currentTrans;
	RESULT_TYPE result = ICP_ERROR;

	for (unsigned iteration = 0 ;; ++iteration)
	{
		if (progressCb && progressCb->isCancelRequested())
		{
			result = ICP_ERROR_CANCELED_BY_USER;
			break;
		}

		//shall we remove the farthest points?
		bool pointOrderHasBeenChanged = false;
		if (filterOutFarthestPoints)
		{
			NormalDistribution N;
			N.computeParameters(data.cloud);
			if (N.isValid())
			{
				ScalarType mu,sigma2;
				N.getParameters(mu,sigma2);
				ScalarType maxDistance = static_cast<ScalarType>(mu + 2.5*sqrt(sigma2));

				Data filteredData;
				filteredData.cloud = new ReferenceCloud(data.cloud->getAssociatedCloud());
				filteredData.CPSet = new ReferenceCloud(data.CPSet->getAssociatedCloud()); //we must also update the CPSet!
				cloudGarbage.add(filteredData.cloud);
				cloudGarbage.add(filteredData.CPSet);
				if (data.weights)
				{
					filteredData.weights = new ScalarField("ResampledDataWeights");
					sfGarbage.add(filteredData.weights);
				}

				unsigned pointCount = data.cloud->size();
				if (	!filteredData.cloud->reserve(pointCount)
					||	!filteredData.CPSet->reserve(pointCount)
					||	(filteredData.weights && !filteredData.weights->reserve(pointCount)))
				{
					//not enough memory
					result = ICP_ERROR_NOT_ENOUGH_MEMORY;
					break;
				}

				//we keep only the points with "not too high" distances
				for (unsigned i=0; i<pointCount; ++i)
				{
					if (data.cloud->getPointScalarValue(i) <= maxDistance)
					{
						filteredData.cloud->addPointIndex(data.cloud->getPointGlobalIndex(i));
						filteredData.CPSet->addPointIndex(data.CPSet->getPointGlobalIndex(i));
						if (filteredData.weights)
							filteredData.weights->addElement(data.weights->getValue(i));
					}
				}

				//resize should be ok as we have called reserve first
				filteredData.cloud->resize(filteredData.cloud->size()); //should always be ok as current size < pointCount
				filteredData.CPSet->resize(filteredData.CPSet->size());
				if (filteredData.weights)
					filteredData.weights->resize(filteredData.weights->currentSize());

				//replace old structures by new ones
				cloudGarbage.destroy(data.cloud);
				cloudGarbage.destroy(data.CPSet);
				if (data.weights)
					sfGarbage.destroy(data.weights);
				data = filteredData;

				pointOrderHasBeenChanged = true;
			}
		}

		//shall we ignore/remove some points based on their distance?
		Data trueData;
		unsigned pointCount = data.cloud->size();
		if (maxOverlapCount != 0 && pointCount > maxOverlapCount)
		{
			assert(overlapDistances.size() >= pointCount);
			for (unsigned i=0; i<pointCount; ++i)
			{
				overlapDistances[i] = data.cloud->getPointScalarValue(i);
				assert(overlapDistances[i] == overlapDistances[i]);
			}
			std::sort(overlapDistances.begin(),overlapDistances.begin()+pointCount);

			assert(maxOverlapCount != 0);
			ScalarType maxOverlapDist = overlapDistances[maxOverlapCount-1];

			Data filteredData;
			filteredData.cloud = new ReferenceCloud(data.cloud->getAssociatedCloud());
			filteredData.CPSet = new ReferenceCloud(data.CPSet->getAssociatedCloud()); //we must also update the CPSet!
			cloudGarbage.add(filteredData.cloud);
			cloudGarbage.add(filteredData.CPSet);
			if (data.weights)
			{
				filteredData.weights = new ScalarField("ResampledDataWeights");
				sfGarbage.add(filteredData.weights);
			}

			if (	!filteredData.cloud->reserve(pointCount) //should be maxOverlapCount in theory, but there may be several points with the same value as maxOverlapDist!
				||	!filteredData.CPSet->reserve(pointCount)
				||	(filteredData.weights && !filteredData.weights->reserve(pointCount)))
			{
				//not enough memory
				result = ICP_ERROR_NOT_ENOUGH_MEMORY;
				break;
			}

			//we keep only the points with "not too high" distances
			for (unsigned i=0; i<pointCount; ++i)
			{
				if (data.cloud->getPointScalarValue(i) <= maxOverlapDist)
				{
					filteredData.cloud->addPointIndex(data.cloud->getPointGlobalIndex(i));
					filteredData.CPSet->addPointIndex(data.CPSet->getPointGlobalIndex(i));
					if (filteredData.weights)
						filteredData.weights->addElement(data.weights->getValue(i));
				}
			}
			assert(filteredData.cloud->size() >= maxOverlapCount);

			//resize should be ok as we have called reserve first
			filteredData.cloud->resize(filteredData.cloud->size()); //should always be ok as current size < pointCount
			filteredData.CPSet->resize(filteredData.CPSet->size());
			if (filteredData.weights)
				filteredData.weights->resize(filteredData.weights->currentSize());

			//(temporarily) replace old structures by new ones
			trueData = data;
			data = filteredData;
		}

		//update couple weights (if any)
		if (coupleWeights)
		{
			assert(model.weights || data.weights);
			unsigned count = data.cloud->size();
			assert(data.CPSet->size() == count);

			if (coupleWeights->currentSize() != count && !coupleWeights->resize(count))
			{
				//not enough memory to store weights
				result = ICP_ERROR_NOT_ENOUGH_MEMORY;
				break;
			}
			for (unsigned i = 0; i<count; ++i)
			{
				ScalarType wd = (data.weights ? data.weights->getValue(i) : static_cast<ScalarType>(1.0));
				ScalarType wm = (model.weights ? model.weights->getValue(data.CPSet->getPointGlobalIndex(i)) : static_cast<ScalarType>(1.0));
				coupleWeights->setValue(i, wd*wm);
			}
			coupleWeights->computeMinAndMax();
		}

		//we can now compute the best registration transformation for this step
		//(now that we have selected the points that will be used for registration!)
		{
			//if we use weights, we have to compute weighted RMS!!!
			double meanSquareValue = 0.0;
			double wiSum = 0.0; //we normalize the weights by their sum

			for (unsigned i = 0; i < data.cloud->size(); ++i)
			{
				ScalarType V = data.cloud->getPointScalarValue(i);
				if (ScalarField::ValidValue(V))
				{
					double wi = 1.0;
					if (coupleWeights)
					{
						ScalarType w = coupleWeights->getValue(i);
						if (!ScalarField::ValidValue(w))
							continue;
						wi = fabs(w);
					}
					double Vd = wi * static_cast<double>(V);
					wiSum += wi*wi;
					meanSquareValue += Vd*Vd;
				}
			}

			//12/11/2008 - A.BEY: ICP guarantees only the decrease of the squared distances sum (not the distances sum)
			double meanSquareError = (wiSum != 0 ? static_cast<ScalarType>(meanSquareValue / wiSum) : 0);

			double rms = sqrt(meanSquareError);

#ifdef _DEBUG
			if (fTraceFile)
				fprintf(fTraceFile,"%u; %f; %u;\n",iteration,rms,data.cloud->size());
#endif
			if (iteration == 0)
			{
				//progress notification
				if (progressCb)
				{
					//on the first iteration, we init/show the dialog
					progressCb->reset();
					progressCb->setMethodTitle("Clouds registration");
					char buffer[256];
					sprintf(buffer,"Initial RMS = %f\n",rms);
					progressCb->setInfo(buffer);
					progressCb->start();
				}

				finalRMS = rms;
				finalPointCount = data.cloud->size();

				if (rms < ZERO_TOLERANCE)
				{
					//nothing to do
					result = ICP_NOTHING_TO_DO;
					break;
				}
			}
			else
			{
				assert(lastStepRMS >= 0.0); 
				
				if (rms > lastStepRMS) //error increase!
				{
					result = iteration == 1 ? ICP_NOTHING_TO_DO : ICP_APPLY_TRANSFO;
					break;
				}

				//error update (RMS)
				double deltaRMS = lastStepRMS - rms;
				//should be better!
				assert(deltaRMS >= 0.0);

				//we update the global transformation matrix
				if (currentTrans.R.isValid())
				{
					if (transform.R.isValid())
						transform.R = currentTrans.R * transform.R;
					else
						transform.R = currentTrans.R;

					transform.T = currentTrans.R * transform.T;
				}

				if (adjustScale)
				{
					transform.s *= currentTrans.s;
					transform.T *= currentTrans.s;
				}

				transform.T += currentTrans.T;

				finalRMS = rms;
				finalPointCount = data.cloud->size();

				//stop criterion
				if (	(convType == MAX_ERROR_CONVERGENCE && deltaRMS < minRMSDecrease) //convergence reached
					||	(convType == MAX_ITER_CONVERGENCE && iteration >= nbMaxIterations) //max iteration reached
					)
				{
					result = ICP_APPLY_TRANSFO;
					break;
				}

				//progress notification
				if (progressCb)
				{
					char buffer[256];

					sprintf(buffer,"RMS = %f [-%f]\n",rms,deltaRMS);
					progressCb->setInfo(buffer);
					if (iteration == 1)
					{
						initialDeltaRMS = deltaRMS;
						progressCb->update(0);
					}
					else
					{
						assert(initialDeltaRMS >= 0.0);
						float progressPercent = static_cast<float>((initialDeltaRMS-deltaRMS)/(initialDeltaRMS-minRMSDecrease)*100.0);
						progressCb->update(progressPercent);
					}
				}
			}

			lastStepRMS = rms;
		}

		//single iteration of the registration procedure
		currentTrans = ScaledTransformation();
		if (!RegistrationTools::RegistrationProcedure(data.cloud, data.CPSet, currentTrans, adjustScale, coupleWeights))
		{
			result = ICP_ERROR_REGISTRATION_STEP;
			break;
		}

		//restore original data sets (if any were stored)
		if (trueData.cloud)
		{
			cloudGarbage.destroy(data.cloud);
			cloudGarbage.destroy(data.CPSet);
			if (data.weights)
				sfGarbage.destroy(data.weights);
			data = trueData;
		}

		//shall we filter some components of the resulting transformation?
		if (filters != SKIP_NONE)
		{
			//filter translation (in place)
			FilterTransformation(currentTrans,filters,currentTrans);
		}

		//get rotated data cloud
		if (!data.rotatedCloud || pointOrderHasBeenChanged)
		{
			//we create a new structure, with rotated points
			SimpleCloud* rotatedDataCloud = PointProjectionTools::applyTransformation(data.cloud, currentTrans);
			if (!rotatedDataCloud)
			{
				//not enough memory
				result = ICP_ERROR_NOT_ENOUGH_MEMORY;
				break;
			}
			//replace data.rotatedCloud
			if (data.rotatedCloud)
				cloudGarbage.destroy(data.rotatedCloud);
			data.rotatedCloud = rotatedDataCloud;
			cloudGarbage.add(data.rotatedCloud);

			//update data.cloud
			data.cloud->clear(false);
			data.cloud->setAssociatedCloud(data.rotatedCloud);
			if (!data.cloud->addPointIndex(0,data.rotatedCloud->size()))
			{
				//not enough memory
				result = ICP_ERROR_NOT_ENOUGH_MEMORY;
				break;
			}
		}
		else
		{
			//we simply have to rotate the existing temporary cloud
			data.rotatedCloud->applyTransformation(currentTrans);
			//DGM: warning, we must manually invalidate the ReferenceCloud bbox after rotation!
			data.cloud->invalidateBoundingBox();
		}

		//compute (new) distances to model
		{
			DistanceComputationTools::Cloud2CloudDistanceComputationParams c2cDistParams;
			c2cDistParams.CPSet = data.CPSet;
			if (DistanceComputationTools::computeCloud2CloudDistance(data.cloud,model.cloud,c2cDistParams) < 0)
			{
				//an error occurred during distances computation...
				result = ICP_ERROR_REGISTRATION_STEP;
				break;
			}
		}
	}

	//end of tracefile
	if (fTraceFile)
	{
		fclose(fTraceFile);
		fTraceFile = 0;
	}

	//end of progress notification
	if (progressCb)
	{
		progressCb->stop();
	}

	return result;
}

bool HornRegistrationTools::FindAbsoluteOrientation(GenericCloud* lCloud,
													GenericCloud* rCloud,
													ScaledTransformation& trans,
													bool fixedScale/*=false*/)
{
	return RegistrationProcedure(lCloud,rCloud,trans,!fixedScale);
}

double HornRegistrationTools::ComputeRMS(GenericCloud* lCloud,
										 GenericCloud* rCloud,
										 const ScaledTransformation& trans)
{
	assert(rCloud && lCloud);
	if (!rCloud || !lCloud || rCloud->size() != lCloud->size() || rCloud->size()<3)
		return false;

	double rms = 0.0;

	rCloud->placeIteratorAtBegining();
	lCloud->placeIteratorAtBegining();
	unsigned count = rCloud->size();
			
	for (unsigned i=0; i<count; i++)
	{
		const CCVector3* Ri = rCloud->getNextPoint();
		const CCVector3* Li = lCloud->getNextPoint();
		CCVector3 Lit = (trans.R.isValid() ? trans.R * (*Li) : (*Li))*trans.s + trans.T;

//#ifdef _DEBUG
//		double dist = (*Ri-Lit).norm();
//#endif

		rms += (*Ri-Lit).norm2();
	}

	return sqrt(rms/(double)count);
}

bool RegistrationTools::RegistrationProcedure(	GenericCloud* P, //data
												GenericCloud* X, //model
												ScaledTransformation& trans,
												bool adjustScale/*=false*/,
												ScalarField* coupleWeights/*=0*/,
												PointCoordinateType aPrioriScale/*=1.0f*/)
{
	//resulting transformation (R is invalid on initialization, T is (0,0,0) and s==1)
	trans.R.invalidate();
	trans.T = CCVector3(0,0,0);
	trans.s = PC_ONE;

	if (P == 0 || X == 0 || P->size() != X->size() || P->size() < 3)
		return false;

	//centers of mass
	CCVector3 Gp = coupleWeights ? GeometricalAnalysisTools::computeWeightedGravityCenter(P, coupleWeights) : GeometricalAnalysisTools::computeGravityCenter(P);
	CCVector3 Gx = coupleWeights ? GeometricalAnalysisTools::computeWeightedGravityCenter(X, coupleWeights) : GeometricalAnalysisTools::computeGravityCenter(X);

	//specific case: 3 points only
	//See section 5.A in Horn's paper
	if (P->size() == 3)
	{
		//compute the first set normal
		P->placeIteratorAtBegining();
		const CCVector3* Ap = P->getNextPoint();
		const CCVector3* Bp = P->getNextPoint();
		const CCVector3* Cp = P->getNextPoint();
		CCVector3 Np(0,0,1);
		{
			Np = (*Bp-*Ap).cross(*Cp-*Ap);
			double norm = Np.normd();
			if (norm < ZERO_TOLERANCE)
				return false;
			Np /= static_cast<PointCoordinateType>(norm);
		}
		//compute the second set normal
		X->placeIteratorAtBegining();
		const CCVector3* Ax = X->getNextPoint();
		const CCVector3* Bx = X->getNextPoint();
		const CCVector3* Cx = X->getNextPoint();
		CCVector3 Nx(0,0,1);
		{
			Nx = (*Bx-*Ax).cross(*Cx-*Ax);
			double norm = Nx.normd();
			if (norm < ZERO_TOLERANCE)
				return false;
			Nx /= static_cast<PointCoordinateType>(norm);
		}
		//now the rotation is simply the rotation from Nx to Np, centered on Gx
		CCVector3 a = Np.cross(Nx);
		if (a.norm() < ZERO_TOLERANCE)
		{
			trans.R = CCLib::SquareMatrix(3);
			trans.R.toIdentity();
			if (Np.dot(Nx) < 0)
			{
				trans.R.scale(-1);
			}
		}
		else
		{
			double cos_t = Np.dot(Nx);
			assert(cos_t > -1.0 && cos_t < 1.0); //see above
			double s = sqrt((1+cos_t)*2);
			double q[4] = { s/2, a.x/s, a.y/s, a.z/s };
			//don't forget to normalize the quaternion
			double qnorm = q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3];
			assert(qnorm >= ZERO_TOLERANCE);
			qnorm = sqrt(qnorm);
			q[0] /= qnorm;
			q[1] /= qnorm;
			q[2] /= qnorm;
			q[3] /= qnorm;
			trans.R.initFromQuaternion(q);
		}

		if (adjustScale)
		{
			double sumNormP = (*Bp-*Ap).norm() + (*Cp-*Bp).norm() + (*Ap-*Cp).norm();
			sumNormP *= aPrioriScale;
			if (sumNormP < ZERO_TOLERANCE)
				return false;
			double sumNormX = (*Bx-*Ax).norm() + (*Cx-*Bx).norm() + (*Ax-*Cx).norm();
			trans.s = static_cast<PointCoordinateType>(sumNormX / sumNormP); //sumNormX / (sumNormP * Sa) in fact
		}

		//we deduce the first translation
		trans.T = Gx - (trans.R*Gp) * (aPrioriScale*trans.s); //#26 in besl paper, modified with the scale as in jschmidt

		//we need to find the rotation in the (X) plane now
		{
			CCVector3 App = trans.apply(*Ap);
			CCVector3 Bpp = trans.apply(*Bp);
			CCVector3 Cpp = trans.apply(*Cp);

			double C = 0;
			double S = 0;
			CCVector3 Ssum(0,0,0);
			CCVector3 rx,rp;
			
			rx = *Ax - Gx;
			rp = App - Gx;
			C = rx.dot(rp);
			Ssum = rx.cross(rp);

			rx = *Bx - Gx;
			rp = Bpp - Gx;
			C += rx.dot(rp);
			Ssum += rx.cross(rp);

			rx = *Cx - Gx;
			rp = Cpp - Gx;
			C += rx.dot(rp);
			Ssum += rx.cross(rp);

			S = Ssum.dot(Nx);
			double Q = sqrt(S*S + C*C);
			if (Q < ZERO_TOLERANCE)
				return false;
			
			PointCoordinateType sin_t = static_cast<PointCoordinateType>(S / Q);
			PointCoordinateType cos_t = static_cast<PointCoordinateType>(C / Q);
			PointCoordinateType inv_cos_t = 1 - cos_t;

			const PointCoordinateType& l1 = Nx.x;
			const PointCoordinateType& l2 = Nx.y;
			const PointCoordinateType& l3 = Nx.z;

			PointCoordinateType l1_inv_cos_t = l1*inv_cos_t;
			PointCoordinateType l3_inv_cos_t = l3*inv_cos_t;

			SquareMatrix R(3);
			//1st column
			R.m_values[0][0] = cos_t + l1*l1_inv_cos_t;
			R.m_values[0][1] = l2*l1_inv_cos_t+l3*sin_t;
			R.m_values[0][2] = l3*l1_inv_cos_t-l2*sin_t;

			//2nd column
			R.m_values[1][0] = l2*l1_inv_cos_t-l3*sin_t;
			R.m_values[1][1] = cos_t+l2*l2*inv_cos_t;
			R.m_values[1][2] = l2*l3_inv_cos_t+l1*sin_t;

			//3rd column
			R.m_values[2][0] = l3*l1_inv_cos_t+l2*sin_t;
			R.m_values[2][1] = l2*l3_inv_cos_t-l1*sin_t;
			R.m_values[2][2] = cos_t+l3*l3_inv_cos_t;

			trans.R = R * trans.R;
			trans.T = Gx - (trans.R*Gp) * (aPrioriScale*trans.s); //update T as well
		}
	}
	else
	{
		CCVector3 bbMin,bbMax;
		X->getBoundingBox(bbMin,bbMax);

		//if the data cloud is equivalent to a single point (for instance
		//it's the case when the two clouds are very far away from
		//each other in the ICP process) we try to get the two clouds closer
		CCVector3 diag = bbMax-bbMin;
		if (fabs(diag.x) + fabs(diag.y) + fabs(diag.z) < ZERO_TOLERANCE)
		{
			trans.T = Gx - Gp*aPrioriScale;
			return true;
		}

		//Cross covariance matrix, eq #24 in Besl92 (but with weights, if any)
		SquareMatrixd Sigma_px = (coupleWeights ? GeometricalAnalysisTools::computeWeightedCrossCovarianceMatrix(P, X, Gp, Gx, coupleWeights)
												: GeometricalAnalysisTools::computeCrossCovarianceMatrix(P,X,Gp,Gx) );
		if (!Sigma_px.isValid())
			return false;

		//transpose sigma_px
		SquareMatrixd Sigma_px_t = Sigma_px.transposed();

		SquareMatrixd Aij = Sigma_px - Sigma_px_t;

		double trace = Sigma_px.trace(); //that is the sum of diagonal elements of sigma_px

		SquareMatrixd traceI3(3); //create the I matrix with eigvals equal to trace
		traceI3.m_values[0][0] = trace;
		traceI3.m_values[1][1] = trace;
		traceI3.m_values[2][2] = trace;

		SquareMatrixd bottomMat = Sigma_px + Sigma_px_t - traceI3;

		//we build up the registration matrix (see ICP algorithm)
		SquareMatrixd QSigma(4); //#25 in the paper (besl)

		QSigma.m_values[0][0] = trace;

		QSigma.m_values[0][1] = QSigma.m_values[1][0] = Aij.m_values[1][2];
		QSigma.m_values[0][2] = QSigma.m_values[2][0] = Aij.m_values[2][0];
		QSigma.m_values[0][3] = QSigma.m_values[3][0] = Aij.m_values[0][1];

		QSigma.m_values[1][1] = bottomMat.m_values[0][0];
		QSigma.m_values[1][2] = bottomMat.m_values[0][1];
		QSigma.m_values[1][3] = bottomMat.m_values[0][2];

		QSigma.m_values[2][1] = bottomMat.m_values[1][0];
		QSigma.m_values[2][2] = bottomMat.m_values[1][1];
		QSigma.m_values[2][3] = bottomMat.m_values[1][2];

		QSigma.m_values[3][1] = bottomMat.m_values[2][0];
		QSigma.m_values[3][2] = bottomMat.m_values[2][1];
		QSigma.m_values[3][3] = bottomMat.m_values[2][2];

		//we compute its eigenvalues and eigenvectors
		SquareMatrixd eig = QSigma.computeJacobianEigenValuesAndVectors();

		if (!eig.isValid())
			return false;

		//as Besl says, the best rotation corresponds to the eigenvector associated to the biggest eigenvalue
		double qR[4];
		eig.getMaxEigenValueAndVector(qR);

		//these eigenvalue and eigenvector correspond to a quaternion --> we get the corresponding matrix
		trans.R.initFromQuaternion(qR);

		if (adjustScale)
		{
			//two accumulators
			double acc_num = 0.0;
			double acc_denom = 0.0;

			//now deduce the scale (refer to "Point Set Registration with Integrated Scale Estimation", Zinsser et. al, PRIP 2005)
			X->placeIteratorAtBegining();
			P->placeIteratorAtBegining();

			unsigned count = X->size();
			assert(P->size() == count);
			for (unsigned i=0; i<count; ++i)
			{
				//'a' refers to the data 'A' (moving) = P
				//'b' refers to the model 'B' (not moving) = X
				CCVector3 a_tilde = trans.R * (*(P->getNextPoint()) - Gp);	// a_tilde_i = R * (a_i - a_mean)
				CCVector3 b_tilde = (*(X->getNextPoint()) - Gx);			// b_tilde_j =     (b_j - b_mean)

				acc_num += b_tilde.dot(a_tilde);
				acc_denom += a_tilde.dot(a_tilde);
			}

			//DGM: acc_2 can't be 0 because we already have checked that the bbox is not a single point!
			assert(acc_denom > 0.0);
			trans.s = static_cast<PointCoordinateType>(fabs(acc_num / acc_denom));
		}

		//and we deduce the translation
		trans.T = Gx - (trans.R*Gp) * (aPrioriScale*trans.s); //#26 in besl paper, modified with the scale as in jschmidt
	}

	return true;
}

bool FPCSRegistrationTools::RegisterClouds(	GenericIndexedCloud* modelCloud,
											GenericIndexedCloud* dataCloud,
											ScaledTransformation& transform,
											ScalarType delta,
											ScalarType beta,
											PointCoordinateType overlap,
											unsigned nbBases,
											unsigned nbTries,
											GenericProgressCallback* progressCb,
											unsigned nbMaxCandidates)
{
	/*DGM: KDTree::buildFromCloud will call reset right away!
	if (progressCb)
	{
		progressCb->reset();
		progressCb->setMethodTitle("Clouds registration");
		progressCb->setInfo("Starting 4PCS");
		progressCb->start();
	}
	//*/

	//Initialize random seed with current time
	srand(static_cast<unsigned>(time(0)));

	unsigned bestScore = 0, score = 0;
	transform.R.invalidate();
	transform.T = CCVector3(0,0,0);

	//Adapt overlap to the model cloud size
	{
		CCVector3 bbMin, bbMax;
		modelCloud->getBoundingBox(bbMin, bbMax);
		CCVector3 diff = bbMax - bbMin;
		overlap *= diff.norm() / 2;
	}

	//Build the associated KDtrees
	KDTree* dataTree = new KDTree();
	if (!dataTree->buildFromCloud(dataCloud, progressCb))
	{
		delete dataTree;
		return false;
	}
	KDTree* modelTree = new KDTree();
	if (!modelTree->buildFromCloud(modelCloud, progressCb))
	{
		delete dataTree;
		delete modelTree;
		return false;
	}

	//if (progressCb)
	//    progressCb->stop();

	for (unsigned i=0; i<nbBases; i++)
	{
		//Randomly find the current reference base
		Base reference;
		if (!FindBase(modelCloud, overlap, nbTries, reference))
			continue;

		//Search for all the congruent bases in the second cloud
		std::vector<Base> candidates;
		unsigned count = dataCloud->size();
		candidates.reserve(count);
		if (candidates.capacity() < count) //not enough memory
		{
			delete dataTree;
			delete modelTree;
			transform.R = SquareMatrix();
			return false;
		}
		const CCVector3* referenceBasePoints[4];
		{
			for(unsigned j=0; j<4; j++)
				referenceBasePoints[j] = modelCloud->getPoint(reference.getIndex(j));
		}
		int result = FindCongruentBases(dataTree, beta, referenceBasePoints, candidates);
		if (result == 0)
			continue;
		else if (result < 0) //something bad happened!
		{
			delete dataTree;
			delete modelTree;
			transform.R = SquareMatrix();
			return false;
		}

		//Compute rigid transforms and filter bases if necessary
		{
			std::vector<ScaledTransformation> transforms;
			if (!FilterCandidates(modelCloud, dataCloud, reference, candidates, nbMaxCandidates, transforms))
			{
				delete dataTree;
				delete modelTree;
				transform.R = SquareMatrix();
				return false;
			}

			for(unsigned j=0; j<candidates.size(); j++)
			{
				//Register the current candidate base with the reference base
				const ScaledTransformation& RT = transforms[j];
				//Apply the rigid transform to the data cloud and compute the registration score
				if (RT.R.isValid())
				{
					score = ComputeRegistrationScore(modelTree, dataCloud, delta, RT);

					//Keep parameters that lead to the best result
					if (score > bestScore)
					{
						transform.R = RT.R;
						transform.T = RT.T;
						bestScore = score;
					}
				}
			}
		}

		if (progressCb)
		{
			char buffer[256];
			sprintf(buffer,"Trial %u/%u [best score = %u]\n",i+1,nbBases,bestScore);
			progressCb->setInfo(buffer);
			progressCb->update(((float)(i+1)*100.0f)/(float)nbBases);

			if (progressCb->isCancelRequested())
			{
				delete dataTree;
				delete modelTree;
				transform.R = SquareMatrix();
				return false;
			}
		}
	}

	delete dataTree;
	delete modelTree;

	if (progressCb)
		progressCb->stop();

	return (bestScore > 0);
}


 unsigned FPCSRegistrationTools::ComputeRegistrationScore(	KDTree *modelTree,
															GenericIndexedCloud *dataCloud,
															ScalarType delta,
															const ScaledTransformation& dataToModel)
{
	CCVector3 Q;

	unsigned score = 0;

	unsigned count = dataCloud->size();
	for (unsigned i=0; i<count; ++i)
	{
		dataCloud->getPoint(i,Q);
		//Apply rigid transform to each point
		Q = dataToModel.R * Q + dataToModel.T;
		//Check if there is a point in the model cloud that is close enough to q
		if (modelTree->findPointBelowDistance(Q.u, delta))
			score++;
	}

	return score;
 }

bool FPCSRegistrationTools::FindBase(	GenericIndexedCloud* cloud,
										PointCoordinateType overlap,
										unsigned nbTries,
										Base &base)
{
	unsigned a, b, c, d;
	unsigned i, size;
	PointCoordinateType f, best, d0, d1, d2, x, y, z, w;
	const CCVector3 *p0, *p1, *p2, *p3;
	CCVector3 normal, u, v;

	overlap *= overlap;
	size = cloud->size();
	best = 0.;
	b = c = 0;
	a = rand()%size;
	p0 = cloud->getPoint(a);
	//Randomly pick 3 points as sparsed as possible
	for(i=0; i<nbTries; i++)
	{
		unsigned t1 = (rand() % size);
		unsigned t2 = (rand() % size);
		if (t1 == a || t2 == a || t1 == t2)
			continue;

		p1 = cloud->getPoint(t1);
		p2 = cloud->getPoint(t2);
		//Checked that the selected points are not more than overlap-distant from p0
		u = *p1-*p0;
		if (u.norm2() > overlap)
			continue;
		u = *p2-*p0;
		if (u.norm2() > overlap)
			continue;

		//compute [p0, p1, p2] area thanks to cross product
		x = ((p1->y-p0->y)*(p2->z-p0->z))-((p1->z-p0->z)*(p2->y-p0->y));
		y = ((p1->z-p0->z)*(p2->x-p0->x))-((p1->x-p0->x)*(p2->z-p0->z));
		z = ((p1->x-p0->x)*(p2->y-p0->y))-((p1->y-p0->y)*(p2->x-p0->x));
		//don't need to compute the true area : f=(area²)*2 is sufficient for comparison
		f = x*x + y*y + z*z;
		if (f > best)
		{
			b = t1;
			c = t2;
			best = f;
			normal.x = x;
			normal.y = y;
			normal.z = z;
		}
	}

	if (b == c)
		return false;

	//Once we found the points, we have to search for a fourth coplanar point
	f = normal.norm();
	if (f <= 0)
		return false;
	normal *= 1.0f/f;
	//plane equation : p lies in the plane if x*p[0] + y*p[1] + z*p[2] + w = 0
	x = normal.x;
	y = normal.y;
	z = normal.z;
	w = -(x*p0->x)-(y*p0->y)-(z*p0->z);
	d = a;
	best = -1.;
	p1 = cloud->getPoint(b);
	p2 = cloud->getPoint(c);
	for(i=0; i<nbTries; i++)
	{
		unsigned t1 = (rand() % size);
		if (t1 == a || t1 == b || t1 == c)
			continue;
		p3 = cloud->getPoint(t1);
		//p3 must be close enough to at least two other points (considering overlap)
		d0 = (*p3 - *p0).norm2();
		d1 = (*p3 - *p1).norm2();
		d2 = (*p3 - *p2).norm2();
		if ((d0>=overlap && d1>=overlap) || (d0>=overlap && d2>=overlap) || (d1>=overlap && d2>=overlap))
			continue;
		//Compute distance to the plane (cloud[a], cloud[b], cloud[c])
		f = fabs((x*p3->x)+(y*p3->y)+(z*p3->z)+w);
		//keep the point which is the closest to the plane, while being as far as possible from the other three points
		f=(f+1.0f)/(sqrt(d0)+sqrt(d1)+sqrt(d2));
		if ((best < 0.) || (f < best))
		{
			d = t1;
			best = f;
		}
	}

	//Store the result in the base parameter
	if (d != a)
	{
		//Find the points order in the quadrilateral
		p0 = cloud->getPoint(a);
		p1 = cloud->getPoint(b);
		p2 = cloud->getPoint(c);
		p3 = cloud->getPoint(d);
		//Search for the diagonnals of the convexe hull (3 tests max)
		//Note : if the convexe hull is made of 3 points, the points order has no importance
		u = (*p1-*p0)*(*p2-*p0);
		v = (*p1-*p0)*(*p3-*p0);
		if (u.dot(v) <= 0)
		{
			//p2 and p3 lie on both sides of [p0, p1]
			base.init(a, b, c, d);
			return true;
		}
		u = (*p2-*p1)*(*p0-*p1);
		v = (*p2-*p1)*(*p3-*p1);
		if (u.dot(v) <= 0)
		{
			//p0 and p3 lie on both sides of [p2, p1]
			base.init(b, c, d, a);
			return true;
		}
		base.init(a, c, b, d);
		return true;
	}

	return false;
}

//pair of indexes
typedef std::pair<unsigned,unsigned> IndexPair;

int FPCSRegistrationTools::FindCongruentBases(KDTree* tree,
												ScalarType delta,
												const CCVector3* base[4],
												std::vector<Base>& results)
{
	//Compute reference base invariants (r1, r2)
	PointCoordinateType r1, r2, d1, d2;
	{
		const CCVector3* p0 = base[0];
		const CCVector3* p1 = base[1];
		const CCVector3* p2 = base[2];
		const CCVector3* p3 = base[3];

		d1 = (*p1-*p0).norm();
		d2 = (*p3-*p2).norm();

		CCVector3 inter;
		if (!LinesIntersections(*p0, *p1, *p2, *p3, inter, r1, r2))
			return 0;
	}

	GenericIndexedCloud* cloud = tree->getAssociatedCloud();

	//Find all pairs which are d1-appart and d2-appart
	std::vector<IndexPair> pairs1, pairs2;
	{
		unsigned count = (unsigned)cloud->size();
		std::vector<unsigned> pointsIndexes;
		try
		{
			pointsIndexes.reserve(count);
		}
		catch(...)
		{
			//not enough memory
			return -1;
		}

		for (unsigned i=0; i<count; i++)
		{
			const CCVector3 *q0 = cloud->getPoint(i);
			IndexPair idxPair;
			idxPair.first = i;
			//Extract all points from the cloud which are d1-appart (up to delta) from q0
			pointsIndexes.clear();
			tree->findPointsLyingToDistance(q0->u, static_cast<ScalarType>(d1), delta, pointsIndexes);
			{
				for(size_t j=0; j<pointsIndexes.size(); j++)
				{
					//As ||pi-pj|| = ||pj-pi||, we only take care of pairs that verify i<j
					if (pointsIndexes[j]>i)
					{
						idxPair.second = pointsIndexes[j];
						pairs1.push_back(idxPair);
					}
				}
			}
			//Extract all points from the cloud which are d2-appart (up to delta) from q0
			pointsIndexes.clear();
			tree->findPointsLyingToDistance(q0->u, static_cast<ScalarType>(d2), delta, pointsIndexes);
			{
				for(size_t j=0; j<pointsIndexes.size(); j++)
				{
					if (pointsIndexes[j]>i)
					{
						idxPair.second = pointsIndexes[j];
						pairs2.push_back(idxPair);
					}
				}
			}
		}
	}

	//Select among the pairs the ones that can be congruent to the base "base"
	std::vector<IndexPair> match;
	{
		SimpleCloud tmpCloud1,tmpCloud2;
		{
			unsigned count = (unsigned)pairs1.size();
			if (!tmpCloud1.reserve(count*2)) //not enough memory
				return -2;
			for(unsigned i=0; i<count; i++)
			{
				//generate the two intermediate points from r1 in pairs1[i]
				const CCVector3 *q0 = cloud->getPoint(pairs1[i].first);
				const CCVector3 *q1 = cloud->getPoint(pairs1[i].second);
				CCVector3 P1 = *q0 + r1*(*q1-*q0);
				tmpCloud1.addPoint(P1);
				CCVector3 P2 = *q1 + r1*(*q0-*q1);
				tmpCloud1.addPoint(P2);
			}
		}
	
		{
			unsigned count = (unsigned)pairs2.size();
			if (!tmpCloud2.reserve(count*2)) //not enough memory
				return -3;
			for(unsigned i=0; i<count; i++)
			{
				//generate the two intermediate points from r2 in pairs2[i]
				const CCVector3 *q0 = cloud->getPoint(pairs2[i].first);
				const CCVector3 *q1 = cloud->getPoint(pairs2[i].second);
				CCVector3 P1 = *q0 + r2*(*q1-*q0);
				tmpCloud2.addPoint(P1);
				CCVector3 P2 = *q1 + r2*(*q0-*q1);
				tmpCloud2.addPoint(P2);
			}
		}

		//build kdtree for nearest neighbour fast research
		KDTree intermediateTree;
		if (!intermediateTree.buildFromCloud(&tmpCloud1))
			return -4;

		//Find matching (up to delta) intermediate points in tmpCloud1 and tmpCloud2
		{
			unsigned count = (unsigned)tmpCloud2.size();
			match.reserve(count);
			if (match.capacity() < count)	//not enough memory
				return -5;
		
			for(unsigned i=0; i<count; i++)
			{
				const CCVector3 *q0 = tmpCloud2.getPoint(i);
				unsigned a;
				if (intermediateTree.findNearestNeighbour(q0->u, a, delta))
				{
					IndexPair idxPair;
					idxPair.first = i;
					idxPair.second = a;
					match.push_back(idxPair);
				}
			}
		}
	}

	//Find bases from matching intermediate points indexes
	{
		results.clear();
		size_t count = match.size();
		if (count>0)
		{
			results.reserve(count);
			if (results.capacity() < count)		//not enough memory
				return -6;
			for(size_t i=0; i<count; i++)
			{
				Base quad;
				unsigned b = match[i].second / 2;
				if ((match[i].second % 2) == 0)
				{
					quad.a = pairs1[b].first;
					quad.b = pairs1[b].second;
				}
				else
				{
					quad.a = pairs1[b].second;
					quad.b = pairs1[b].first;
				}

				unsigned a = match[i].first / 2;
				if ((match[i].first % 2) == 0)
				{
					quad.c = pairs2[a].first;
					quad.d = pairs2[a].second;
				}
				else
				{
					quad.c = pairs2[a].second;
					quad.d = pairs2[a].first;
				}
				results.push_back(quad);
			}
		}
	}

	return (int)results.size();
}


bool FPCSRegistrationTools::LinesIntersections(	const CCVector3 &p0,
												const CCVector3 &p1,
												const CCVector3 &p2,
												const CCVector3 &p3,
												CCVector3 &inter,
												PointCoordinateType& lambda,
												PointCoordinateType& mu)
{
	CCVector3 p02, p32, p10, A, B;
	PointCoordinateType num, denom;

	//Find lambda and mu such that :
	//A = p0+lambda(p1-p0)
	//B = p2+mu(p3-p2)
	//(lambda, mu) = argmin(||A-B||²)
	p02 = p0-p2;
	p32 = p3-p2;
	p10 = p1-p0;
	num = (p02.dot(p32) * p32.dot(p10)) - (p02.dot(p10) * p32.dot(p32));
	denom = (p10.dot(p10) * p32.dot(p32)) - (p32.dot(p10) * p32.dot(p10));
	if (fabs(denom) < 0.00001)
		return false;
	lambda = num / denom;
	num = p02.dot(p32) + (lambda*p32.dot(p10));
	denom = p32.dot(p32);
	if (fabs(denom) < 0.00001)
		return false;
	mu = num / denom;
	A.x = p0.x+(lambda*p10.x);
	A.y = p0.y+(lambda*p10.y);
	A.z = p0.z+(lambda*p10.z);
	B.x = p2.x+(mu*p32.x);
	B.y = p2.y+(mu*p32.y);
	B.z = p2.z+(mu*p32.z);
	inter.x = (A.x+B.x)/2.0f;
	inter.y = (A.y+B.y)/2.0f;
	inter.z = (A.z+B.z)/2.0f;

	return true;
}

bool FPCSRegistrationTools::FilterCandidates(	GenericIndexedCloud *modelCloud,
												GenericIndexedCloud *dataCloud,
												Base& reference,
												std::vector<Base>& candidates,
												unsigned nbMaxCandidates,
												std::vector<ScaledTransformation>& transforms)
{
	std::vector<Base> table;
	std::vector<float> scores, sortedscores;
	const CCVector3* p[4];
	ScaledTransformation t;
	std::vector<ScaledTransformation> tarray;
	SimpleCloud referenceBaseCloud, dataBaseCloud;

	unsigned candidatesCount = static_cast<unsigned>(candidates.size());
	if (candidatesCount == 0)
		return false;

	bool filter = (nbMaxCandidates>0 && candidatesCount > nbMaxCandidates);
	{
		try
		{
			table.resize(candidatesCount);
		}
		catch (.../*const std::bad_alloc&*/) //out of memory
		{
			return false;
		}
		for (unsigned i=0; i<candidatesCount; i++)
			table[i].copy(candidates[i]);
	}

	if (!referenceBaseCloud.reserve(4)) //we never know ;)
		return false;

	{
		for (unsigned j=0; j<4; j++)
		{
			p[j] = modelCloud->getPoint(reference.getIndex(j));
			referenceBaseCloud.addPoint(*p[j]);
		}
	}

	try
	{
		scores.reserve(candidatesCount);
		sortedscores.reserve(candidatesCount);
		tarray.reserve(candidatesCount);
		transforms.reserve(candidatesCount);
	}
	catch (.../*const std::bad_alloc&*/) //out of memory
	{
		return false;
	}

	//enough memory?
	if (	scores.capacity() < candidatesCount 
		||	sortedscores.capacity() < candidatesCount 
		||	tarray.capacity() < candidatesCount 
		||	transforms.capacity() < candidatesCount)
	{
		return false;
	}

	{
		for (unsigned i=0; i<table.size(); i++)
		{
			dataBaseCloud.clear();
			if (!dataBaseCloud.reserve(4)) //we never know ;)
				return false;
			for (unsigned j=0; j<4; j++)
				dataBaseCloud.addPoint(*dataCloud->getPoint(table[i].getIndex(j)));

			if (!RegistrationTools::RegistrationProcedure(&dataBaseCloud, &referenceBaseCloud, t, false))
				return false;

			tarray.push_back(t);
			if (filter)
			{
				float score = 0;
				GenericIndexedCloud* b = PointProjectionTools::applyTransformation(&dataBaseCloud, t);
				if (!b)
					return false; //not enough memory
				for (unsigned j=0; j<4; j++)
				{
					const CCVector3* q = b->getPoint(j);
					score += static_cast<float>((*q - *(p[j])).norm());
				}
				delete b;
				scores.push_back(score);
				sortedscores.push_back(score);
			}
		}
	}

	if (filter)
	{
		transforms.clear();
		candidates.clear();
		try
		{
			candidates.resize(nbMaxCandidates);
		}
		catch (.../*const std::bad_alloc&*/) //out of memory
		{
			return false;
		}

		//Sort the scores in ascending order and only keep the nbMaxCandidates smallest scores
		sort(sortedscores.begin(), sortedscores.end());
		float score = sortedscores[nbMaxCandidates-1];
		unsigned j = 0;
		for (unsigned i=0; i<scores.size(); i++)
		{
			if (scores[i] <= score && j < nbMaxCandidates)
			{
				candidates[i].copy(table[i]);
				transforms.push_back(tarray[i]);
				j++;
			}
		}
	}
	else
	{
		transforms = tarray;
	}

	return true;
}

