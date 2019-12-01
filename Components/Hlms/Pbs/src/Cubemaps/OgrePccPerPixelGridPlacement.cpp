/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-present Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "Cubemaps/OgrePccPerPixelGridPlacement.h"

#include "Cubemaps/OgreParallaxCorrectedCubemapAuto.h"

#include "OgreAsyncTextureTicket.h"
#include "OgreTextureGpuManager.h"

namespace Ogre
{
    PccPerPixelGridPlacement::PccPerPixelGridPlacement() :
        mFullRegion( Vector3::ZERO, Vector3::UNIT_SCALE ),
        mOverlap( Vector3( 1.5f ) ),
        mSnapDeviationError( Vector3( 0.05f ) ),
        mSnapSidesDeviationErrorMin( Vector3( 0.25f ) ),
        mSnapSidesDeviationErrorMax( Vector3( 0.25f ) ),
        mPcc( 0 ),
        mDownloadedImageFallback( 0 ),
        mFallbackDpmMipmap( 0 )
    {
        mNumProbes[0] = 2u;
        mNumProbes[1] = 2u;
        mNumProbes[2] = 2u;
    }
    //-------------------------------------------------------------------------
    PccPerPixelGridPlacement::~PccPerPixelGridPlacement() {}
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::setParallaxCorrectedCubemapAuto( ParallaxCorrectedCubemapAuto *pcc )
    {
        OGRE_ASSERT_LOW( mAsyncTicket.empty() && "Cannot change in the middle of buildBegin/buildEnd" );
        mPcc = pcc;
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::setNumProbes( uint32 numProbes[3] )
    {
        OGRE_ASSERT_LOW( mAsyncTicket.empty() && "Cannot change in the middle of buildBegin/buildEnd" );
        for( size_t i = 0u; i < 3u; ++i )
            mNumProbes[i] = numProbes[i];
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::setFullRegion( const Aabb &fullRegion )
    {
        OGRE_ASSERT_LOW( mAsyncTicket.empty() && "Cannot change in the middle of buildBegin/buildEnd" );
        mFullRegion = fullRegion;
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::setOverlap( const Vector3 &overlap )
    {
        OGRE_ASSERT_LOW( mAsyncTicket.empty() && "Cannot change in the middle of buildBegin/buildEnd" );
        mOverlap = overlap;
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::setSnapDeviationError( const Vector3 &relativeError )
    {
        OGRE_ASSERT_LOW( mAsyncTicket.empty() && "Cannot change in the middle of buildBegin/buildEnd" );
        mSnapDeviationError = relativeError;
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::setSnapSides( const Vector3 &snapSidesDeviationErrorMin,
                                                 const Vector3 &snapSidesDeviationErrorMax )
    {
        OGRE_ASSERT_LOW( mAsyncTicket.empty() && "Cannot change in the middle of buildBegin/buildEnd" );
        mSnapSidesDeviationErrorMin = snapSidesDeviationErrorMin;
        mSnapSidesDeviationErrorMax = snapSidesDeviationErrorMax;
    }
    //-------------------------------------------------------------------------
    const Vector3 &PccPerPixelGridPlacement::getSnapSidesDeviationErrorMin( void ) const
    {
        return mSnapSidesDeviationErrorMin;
    }
    //-------------------------------------------------------------------------
    const Vector3 &PccPerPixelGridPlacement::getSnapSidesDeviationErrorMax( void ) const
    {
        return mSnapSidesDeviationErrorMax;
    }
    //-------------------------------------------------------------------------
    uint32 PccPerPixelGridPlacement::getMaxNumProbes() const
    {
        return mNumProbes[0] * mNumProbes[1] * mNumProbes[2];
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::allocateFallback( void )
    {
        deallocateFallback();
        const size_t sizeBytes = PixelFormatGpuUtils::getSizeBytes(
            1u, 1u, 1u, getMaxNumProbes() * 6u, mPcc->getBindTexture()->getPixelFormat(), 4u );
        mDownloadedImageFallback =
            reinterpret_cast<uint8 *>( OGRE_MALLOC_SIMD( sizeBytes, MEMCATEGORY_GENERAL ) );
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::deallocateFallback( void )
    {
        if( mDownloadedImageFallback )
        {
            OGRE_FREE_SIMD( mDownloadedImageFallback, MEMCATEGORY_GENERAL );
            mDownloadedImageFallback = 0;
        }
    }
    //-------------------------------------------------------------------------
    TextureBox PccPerPixelGridPlacement::getFallbackBox( void ) const
    {
        OGRE_ASSERT_LOW( mDownloadedImageFallback );
        const PixelFormatGpu pixelFormat = mPcc->getBindTexture()->getPixelFormat();
        const uint32 bytesPerPixel = (uint32)PixelFormatGpuUtils::getBytesPerPixel( pixelFormat );
        TextureBox retVal( 1u, 1u, 1u, getMaxNumProbes() * 6u, bytesPerPixel, bytesPerPixel,
                           bytesPerPixel );
        retVal.data = mDownloadedImageFallback;
        return retVal;
    }
    //-------------------------------------------------------------------------
    bool PccPerPixelGridPlacement::needsDpmFallback( void ) const
    {
        return mPcc->getUseDpm2DArray() && getMaxNumProbes() > 1u;
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::snapToFullRegion( Vector3 &inOutNewProbeAreaMin,
                                                     Vector3 &inOutNewProbeAreaMax )
    {
        Vector3 newProbeBounds[2] = { inOutNewProbeAreaMin, inOutNewProbeAreaMax };
        Vector3 fullRegionBounds[2] = { mFullRegion.getMinimum(), mFullRegion.getMaximum() };

        for( int i = 0; i < 2; ++i )
        {
            Vector3 deviation = fullRegionBounds[i] - newProbeBounds[i];
            deviation.makeAbs();

            for( size_t j = 0u; j < 3u; ++j )
            {
                const Real componentRelativeDev = ( deviation[j] / ( mFullRegion.mHalfSize[j] * 2.0f ) );
                if( componentRelativeDev <= mSnapDeviationError[j] )
                    newProbeBounds[i][j] = fullRegionBounds[i][j];
            }
        }

        inOutNewProbeAreaMin = newProbeBounds[0];
        inOutNewProbeAreaMax = newProbeBounds[1];
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::snapToSides( size_t probeIdx, Vector3 &inOutNewProbeAreaMin,
                                                Vector3 &inOutNewProbeAreaMax )
    {
        const uint32 xPos = probeIdx % mNumProbes[0];
        const uint32 yPos = ( probeIdx / mNumProbes[0] ) % mNumProbes[1];
        const uint32 zPos = static_cast<uint32>( probeIdx / ( mNumProbes[0] * mNumProbes[1] ) );

        const uint32 intPos[3] = { xPos, yPos, zPos };

        for( size_t i = 0u; i < 3u; ++i )
        {
            if( intPos[i] == 0u )
            {
                // Snap to min
                const Vector3 fullRegionMin = mFullRegion.getMinimum();
                Real deviation = inOutNewProbeAreaMin[i] - fullRegionMin[i];
                deviation = Math::Abs( deviation );

                const Real relativeDeviation = deviation / ( mFullRegion.mHalfSize[i] * 2.0f );

                if( relativeDeviation <= mSnapSidesDeviationErrorMin[i] )
                    inOutNewProbeAreaMin[i] = fullRegionMin[i];
            }
            if( intPos[i] == mNumProbes[i] - 1u )
            {
                // Snap to max
                const Vector3 fullRegionMax = mFullRegion.getMaximum();
                Real deviation = inOutNewProbeAreaMax[i] - fullRegionMax[i];
                deviation = Math::Abs( deviation );

                const Real relativeDeviation = deviation / ( mFullRegion.mHalfSize[i] * 2.0f );

                if( relativeDeviation <= mSnapSidesDeviationErrorMax[i] )
                    inOutNewProbeAreaMax[i] = fullRegionMax[i];
            }
        }
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::processProbeDepth( TextureBox box, size_t probeIdx, size_t sliceIdx )
    {
        CubemapProbe *probe = mPcc->getProbes()[probeIdx];
        const PixelFormatGpu pixelFormat = mPcc->getBindTexture()->getPixelFormat();

        ColourValue colourVal[6];
        for( size_t i = 0u; i < 6u; ++i )
            colourVal[i] = box.getColourAt( 0u, 0u, sliceIdx + i, pixelFormat );

        const Vector3 probeAreaHalfSize( mOverlap * mFullRegion.mHalfSize /
                                         Vector3( mNumProbes[0], mNumProbes[1], mNumProbes[2] ) );

        const Vector3 probeShapeCenter = probe->getProbeShape().mCenter;

        const Vector3 camCenter( probe->getProbeCameraPos() );
        const Vector3 camCenterLS( probe->getInvOrientation() * ( camCenter - probeShapeCenter ) );
        Vector3 probeAreaMax;
        Vector3 probeAreaMin;
        probeAreaMax.x = camCenterLS.x + ( mFullRegion.mHalfSize.x - camCenterLS.x ) *
                                             colourVal[CubemapSide::PX].a * 2.0f;
        probeAreaMin.x = camCenterLS.x + ( -mFullRegion.mHalfSize.x - camCenterLS.x ) *
                                             colourVal[CubemapSide::NX].a * 2.0f;
        probeAreaMax.y = camCenterLS.y + ( mFullRegion.mHalfSize.y - camCenterLS.y ) *
                                             colourVal[CubemapSide::PY].a * 2.0f;
        probeAreaMin.y = camCenterLS.y + ( -mFullRegion.mHalfSize.y - camCenterLS.y ) *
                                             colourVal[CubemapSide::NY].a * 2.0f;
        probeAreaMax.z = camCenterLS.z + ( mFullRegion.mHalfSize.z - camCenterLS.z ) *
                                             colourVal[CubemapSide::PZ].a * 2.0f;
        probeAreaMin.z = camCenterLS.z + ( -mFullRegion.mHalfSize.z - camCenterLS.z ) *
                                             colourVal[CubemapSide::NZ].a * 2.0f;
        probeAreaMin += probeShapeCenter;
        probeAreaMax += probeShapeCenter;

        snapToFullRegion( probeAreaMin, probeAreaMax );
        snapToSides( probeIdx, probeAreaMin, probeAreaMax );

        Aabb probeShape;
        probeShape.setExtents( probeAreaMin, probeAreaMax );
        probeShape.mHalfSize *= 1.05f;
        Aabb area( camCenter, probeAreaHalfSize );
        probe->set( camCenter, area, Vector3( 0.5f ), Matrix3::IDENTITY, probeShape );
    }
    //-------------------------------------------------------------------------
    Vector3 PccPerPixelGridPlacement::getProbeNormalizedCenter( size_t probeIdx ) const
    {
        const uint32 xPos = probeIdx % mNumProbes[0];
        const uint32 yPos = ( probeIdx / mNumProbes[0] ) % mNumProbes[1];
        const uint32 zPos = static_cast<uint32>( probeIdx / ( mNumProbes[0] * mNumProbes[1] ) );

        const Vector3 normalizedCenter( ( xPos + 0.5f ) / static_cast<float>( mNumProbes[0] ),
                                        ( yPos + 0.5f ) / static_cast<float>( mNumProbes[1] ),
                                        ( zPos + 0.5f ) / static_cast<float>( mNumProbes[2] ) );
        return normalizedCenter;
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::buildStart( uint32 resolution, Camera *camera,
                                               PixelFormatGpu pixelFormat, float camNear, float camFar )
    {
        OGRE_ASSERT_LOW( mPcc && "Call setParallaxCorrectedCubemapAuto first!" );

        const uint32 maxNumProbes = getMaxNumProbes();

        mPcc->setEnabled( false, resolution, resolution, maxNumProbes, pixelFormat );
        mPcc->setEnabled( true, resolution, resolution, maxNumProbes, pixelFormat );
        mPcc->setUpdatedTrackedDataFromCamera( camera );

        const Vector3 probeAreaHalfSize( mOverlap * mFullRegion.mHalfSize /
                                         Vector3( mNumProbes[0], mNumProbes[1], mNumProbes[2] ) );
        const Vector3 regionOrigin( mFullRegion.getMinimum() );

        for( size_t i = 0u; i < maxNumProbes; ++i )
        {
            CubemapProbe *probe = mPcc->createProbe();
            const Vector3 normalizedCenter( getProbeNormalizedCenter( i ) );

            const Vector3 camCenter( normalizedCenter * mFullRegion.mHalfSize * 2.0f + regionOrigin );
            Aabb area( camCenter, probeAreaHalfSize );
            probe->setTextureParams( resolution, resolution );
            probe->initWorkspace( camNear, camFar );
            probe->set( camCenter, area, Vector3( 0.05f ), Matrix3::IDENTITY, mFullRegion );
        }

        const bool bNeedsDpmFallback = needsDpmFallback();
        TextureGpu *cubemapTex = mPcc->getBindTexture();
        TextureGpuManager *textureManager = cubemapTex->getTextureManager();

        if( bNeedsDpmFallback )
        {
            mFallbackDpmMipmap = textureManager->createOrRetrieveTexture(
                "PccPerPixelGridPlacement/TmpDpm", GpuPageOutStrategy::Discard,
                TextureFlags::RenderToTexture | TextureFlags::AllowAutomipmaps, TextureTypes::TypeCube );
            mFallbackDpmMipmap->setResolution( resolution, resolution, 6u );
            mFallbackDpmMipmap->setPixelFormat( pixelFormat );
            mFallbackDpmMipmap->setNumMipmaps(
                PixelFormatGpuUtils::getMaxMipmapCount( resolution, resolution ) );
            mFallbackDpmMipmap->scheduleTransitionTo( GpuResidency::Resident );
            mPcc->setListener( this );
        }

        mPcc->updateAllDirtyProbes();

        if( mFallbackDpmMipmap )
        {
            mPcc->setListener( 0 );
            textureManager->destroyTexture( mFallbackDpmMipmap );
            mFallbackDpmMipmap = 0;
        }

        if( !bNeedsDpmFallback )
        {
            AsyncTextureTicket *asyncTicket = textureManager->createAsyncTextureTicket(
                1u, 1u, cubemapTex->getNumSlices(), TextureTypes::TypeCubeArray,
                cubemapTex->getPixelFormat() );
            asyncTicket->download( cubemapTex, cubemapTex->getNumMipmaps() - 1u, false );
            mAsyncTicket.push_back( asyncTicket );
        }
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::buildEnd( void )
    {
        OGRE_ASSERT_LOW( mPcc && "Call setParallaxCorrectedCubemapAuto first!" );
        OGRE_ASSERT_LOW( !mAsyncTicket.empty() && "Call buildStart first!" );

        const uint32 maxNumProbes = getMaxNumProbes();

        TextureGpu *cubemapTex = mPcc->getBindTexture();
        TextureGpuManager *textureManager = cubemapTex->getTextureManager();

        if( mAsyncTicket.size() == 1u )
        {
            // We're not using DPM or have just 1 probe
            AsyncTextureTicket *asyncTicket = mAsyncTicket.back();
            if( asyncTicket->canMapMoreThanOneSlice() )
            {
                const TextureBox box = asyncTicket->map( 0u );
                for( size_t i = 0u; i < maxNumProbes; ++i )
                    processProbeDepth( box, i, i * 6u );
                asyncTicket->unmap();
            }
            else
            {
                // We're not using DPM (or we are but we have just 1 probe),
                // but we still need the fallback for D3D11
                allocateFallback();
                TextureBox fallbackBox = getFallbackBox();

                for( size_t i = 0; i < asyncTicket->getNumSlices(); ++i )
                {
                    const TextureBox box = asyncTicket->map( (uint32)i );
                    ++fallbackBox.sliceStart;
                    fallbackBox.copyFrom( box );
                    asyncTicket->unmap();
                }
            }

            textureManager->destroyAsyncTextureTicket( asyncTicket );
        }
        else if( mAsyncTicket.size() > 1u )
        {
            // We're using DPM and have more than 1 probe, we'll need the fallback
            allocateFallback();
            TextureBox fallbackBox = getFallbackBox();

            FastArray<AsyncTextureTicket *>::const_iterator itor = mAsyncTicket.begin();
            FastArray<AsyncTextureTicket *>::const_iterator endt = mAsyncTicket.end();

            while( itor != endt )
            {
                AsyncTextureTicket *asyncTicket = *itor;

                if( asyncTicket->canMapMoreThanOneSlice() )
                {
                    const TextureBox box = asyncTicket->map( 0u );
                    fallbackBox.copyFrom( box );
                    fallbackBox.sliceStart += box.numSlices;
                    asyncTicket->unmap();
                }
                else
                {
                    for( size_t i = 0; i < asyncTicket->getNumSlices(); ++i )
                    {
                        const TextureBox box = asyncTicket->map( (uint32)i );
                        ++fallbackBox.sliceStart;
                        fallbackBox.copyFrom( box );
                        asyncTicket->unmap();
                    }
                }

                textureManager->destroyAsyncTextureTicket( asyncTicket );

                ++itor;
            }
        }

        if( mDownloadedImageFallback )
        {
            TextureBox fallbackBox = getFallbackBox();
            for( size_t i = 0u; i < maxNumProbes; ++i )
                processProbeDepth( fallbackBox, i, i * 6u );
            deallocateFallback();
        }

        mAsyncTicket.clear();
        mPcc->updateAllDirtyProbes();
    }
    //-------------------------------------------------------------------------
    void PccPerPixelGridPlacement::preCopyRenderTargetToCubemap( TextureGpu *renderTarget,
                                                                 uint32 cubemapArrayIdx )
    {
        TextureBox box = mFallbackDpmMipmap->getEmptyBox( 0 );
        renderTarget->copyTo( mFallbackDpmMipmap, box, 0u, box, 0u );
        mFallbackDpmMipmap->_autogenerateMipmaps();

        TextureGpuManager *textureManager = mFallbackDpmMipmap->getTextureManager();
        AsyncTextureTicket *asyncTicket = textureManager->createAsyncTextureTicket(
            1u, 1u, 6u, TextureTypes::TypeCube, mFallbackDpmMipmap->getPixelFormat() );
        asyncTicket->download( mFallbackDpmMipmap, mFallbackDpmMipmap->getNumMipmaps() - 1u, false );
        mAsyncTicket.push_back( asyncTicket );
    }
}  // namespace Ogre