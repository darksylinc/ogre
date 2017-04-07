
#include "Utils/MiscUtils.h"

#include "OgreHlmsCompute.h"
#include "OgreHlmsComputeJob.h"
#include "OgreLwString.h"

namespace Demo
{
	void MiscUtils::setGaussianFilterParams( Ogre::HlmsComputeJob *job, Ogre::uint8 kernelRadius,
											 float gaussianDeviationFactor)
    {
		using namespace Ogre;

		assert( !(kernelRadius & 0x01) && "kernelRadius must be even!" );

        if( job->getProperty( "kernel_radius" ) != kernelRadius )
            job->setProperty( "kernel_radius", kernelRadius );
		ShaderParams &shaderParams = job->getShaderParams( "default" );

		std::vector<float> weights( kernelRadius + 1u );

		const float fKernelRadius = kernelRadius;
		const float gaussianDeviation = fKernelRadius * gaussianDeviationFactor;

		//It's 2.0f if using the approximate filter (sampling between two pixels to
		//get the bilinear interpolated result and cut the number of samples in half)
		const float stepSize = 1.0f;

		//Calculate the weights
		float fWeightSum = 0;
		for( uint32 i=0; i<kernelRadius + 1u; ++i )
		{
			const float _X = i - fKernelRadius + ( 1.0f - 1.0f / stepSize );
			float fWeight = 1.0f / sqrt ( 2.0f * Math::PI * gaussianDeviation * gaussianDeviation );
			fWeight *= exp( - ( _X * _X ) / ( 2.0f * gaussianDeviation * gaussianDeviation ) );

			fWeightSum += fWeight;
			weights[i] = fWeight;
		}

		fWeightSum = fWeightSum * 2.0f - weights[kernelRadius];

		//Normalize the weights
		for( uint32 i=0; i<kernelRadius + 1u; ++i )
			weights[i] /= fWeightSum;

		//Remove shader constants from previous calls (needed in case we've reduced the radius size)
		ShaderParams::ParamVec::iterator itor = shaderParams.mParams.begin();
		ShaderParams::ParamVec::iterator end  = shaderParams.mParams.end();

		while( itor != end )
		{
			String::size_type pos = itor->name.find( "c_weights[" );

			if( pos != String::npos )
			{
				itor = shaderParams.mParams.erase( itor );
				end  = shaderParams.mParams.end();
			}
			else
			{
				++itor;
			}
		}

		//Set the shader constants, 16 at a time (since that's the limit of what ManualParam can hold)
		char tmp[32];
		LwString weightsString( LwString::FromEmptyPointer( tmp, sizeof(tmp) ) );
		const uint32 floatsPerParam = sizeof( ShaderParams::ManualParam().dataBytes ) / sizeof(float);
		for( uint32 i=0; i<kernelRadius + 1u; i += floatsPerParam )
		{
			weightsString.clear();
			weightsString.a( "c_weights[", i, "]" );

			ShaderParams::Param p;
			p.isAutomatic   = false;
			p.isDirty       = true;
			p.name = weightsString.c_str();
			shaderParams.mParams.push_back( p );
			ShaderParams::Param *param = &shaderParams.mParams.back();

			param->setManualValue( &weights[i], std::min<uint32>( floatsPerParam, weights.size() - i ) );
		}

		shaderParams.setDirty();
	}
}
