
#include "Utils/ScreenSpaceReflections.h"

#include "OgreMaterialManager.h"
#include "OgreMaterial.h"
#include "OgreTechnique.h"

#include "OgreCamera.h"

namespace Demo
{
    const Ogre::Matrix4 PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE(
        0.5,    0,    0,  0.5,
        0,   -0.5,    0,  0.5,
        0,      0,    1,    0,
        0,      0,    0,    1);

    ScreenSpaceReflections::ScreenSpaceReflections()
    {
        Ogre::MaterialPtr material = Ogre::MaterialManager::getSingleton().load(
                    "SSR/ScreenSpaceReflectionsVectors",
                    Ogre::ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME ).
                staticCast<Ogre::Material>();

        Ogre::Pass *pass = material->getTechnique(0)->getPass(0);

        mPsParams = pass->getFragmentProgramParameters();
    }
    //-----------------------------------------------------------------------------------
    void ScreenSpaceReflections::update( Ogre::Camera *camera )
    {
        Ogre::Real projectionA = camera->getFarClipDistance() /
                                    (camera->getFarClipDistance() - camera->getNearClipDistance());
        Ogre::Real projectionB = (-camera->getFarClipDistance() * camera->getNearClipDistance()) /
                                    (camera->getFarClipDistance() - camera->getNearClipDistance());
        //The division will keep "linearDepth" in the shader in the [0; 1] range.
        //projectionB /= camera->getFarClipDistance();
        mPsParams->setNamedConstant( "p_projectionParams",
                                     Ogre::Vector4( projectionA, projectionB, 0, 0 ) );

        Ogre::Matrix4 viewToTextureSpaceMatrix = camera->getProjectionMatrix();
        // Convert depth range from [-1,+1] to [0,1]
        viewToTextureSpaceMatrix[2][0] = (viewToTextureSpaceMatrix[2][0] + viewToTextureSpaceMatrix[3][0]) / 2;
        viewToTextureSpaceMatrix[2][1] = (viewToTextureSpaceMatrix[2][1] + viewToTextureSpaceMatrix[3][1]) / 2;
        viewToTextureSpaceMatrix[2][2] = (viewToTextureSpaceMatrix[2][2] + viewToTextureSpaceMatrix[3][2]) / 2;
        viewToTextureSpaceMatrix[2][3] = (viewToTextureSpaceMatrix[2][3] + viewToTextureSpaceMatrix[3][3]) / 2;

        // Convert right-handed to left-handed
        viewToTextureSpaceMatrix[0][2] = -viewToTextureSpaceMatrix[0][2];
        viewToTextureSpaceMatrix[1][2] = -viewToTextureSpaceMatrix[1][2];
        viewToTextureSpaceMatrix[2][2] = -viewToTextureSpaceMatrix[2][2];
        viewToTextureSpaceMatrix[3][2] = -viewToTextureSpaceMatrix[3][2];

        viewToTextureSpaceMatrix = PROJECTIONCLIPSPACE2DTOIMAGESPACE_PERSPECTIVE * viewToTextureSpaceMatrix;

        mPsParams->setNamedConstant( "p_viewToTextureSpaceMatrix", viewToTextureSpaceMatrix );
    }
}
