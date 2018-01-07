/*
-----------------------------------------------------------------------------
This source file is part of OGRE
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-2017 Torus Knot Software Ltd

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

#include "OgreStableHeaders.h"

#include "OgreSceneFormatImporter.h"
#include "OgreSceneManager.h"
#include "OgreRoot.h"

#include "OgreLwString.h"

#include "OgreItem.h"
#include "OgreMesh2.h"
#include "OgreEntity.h"
#include "OgreHlms.h"

#include "OgreMeshSerializer.h"
#include "OgreMesh2Serializer.h"
#include "OgreFileSystemLayer.h"

#include "OgreLogManager.h"

#include "rapidjson/document.h"

namespace Ogre
{
    SceneFormatImporter::SceneFormatImporter( Root *root, SceneManager *sceneManager ) :
        SceneFormatBase( root, sceneManager )
    {
    }
    //-----------------------------------------------------------------------------------
    SceneFormatImporter::~SceneFormatImporter()
    {
    }
    //-----------------------------------------------------------------------------------
    inline float SceneFormatImporter::decodeFloat( const rapidjson::Value &jsonValue )
    {
        union MyUnion
        {
            float   f32;
            uint32  u32;
        };

        MyUnion myUnion;
        myUnion.u32 = jsonValue.GetUint();
        return myUnion.f32;
    }
    //-----------------------------------------------------------------------------------
    inline Vector3 SceneFormatImporter::decodeVector3Array( const rapidjson::Value &jsonArray )
    {
        Vector3 retVal( Vector3::ZERO );

        const rapidjson::SizeType arraySize = std::min( 3u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Vector4 SceneFormatImporter::decodeVector4Array( const rapidjson::Value &jsonArray )
    {
        Vector4 retVal( Vector4::ZERO );

        const rapidjson::SizeType arraySize = std::min( 4u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Quaternion SceneFormatImporter::decodeQuaternionArray( const rapidjson::Value &jsonArray )
    {
        Quaternion retVal( Quaternion::IDENTITY );

        const rapidjson::SizeType arraySize = std::min( 4u, jsonArray.Size() );
        for( rapidjson::SizeType i=0; i<arraySize; ++i )
        {
            if( jsonArray[i].IsUint() )
                retVal[i] = decodeFloat( jsonArray[i] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    inline Aabb SceneFormatImporter::decodeAabbArray( const rapidjson::Value &jsonArray,
                                                      const Aabb &defaultValue )
    {
        Aabb retVal( defaultValue );

        if( jsonArray.Size() == 2u )
        {
            retVal.mCenter = decodeVector3Array( jsonArray[0] );
            retVal.mHalfSize = decodeVector3Array( jsonArray[1] );
        }

        return retVal;
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importNode( const rapidjson::Value &nodeValue, Node *node )
    {
        rapidjson::Value::ConstMemberIterator  itor;

        itor = nodeValue.FindMember( "position" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsArray() )
            node->setPosition( decodeVector3Array( itor->value ) );

        itor = nodeValue.FindMember( "rotation" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsArray() )
            node->setOrientation( decodeQuaternionArray( itor->value ) );

        itor = nodeValue.FindMember( "scale" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsArray() )
            node->setScale( decodeVector3Array( itor->value ) );

        itor = nodeValue.FindMember( "inherit_orientation" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsBool() )
            node->setInheritOrientation( itor->value.GetBool() );

        itor = nodeValue.FindMember( "inherit_scale" );
        if( itor != nodeValue.MemberEnd() && itor->value.IsBool() )
            node->setInheritScale( itor->value.GetBool() );
    }
    //-----------------------------------------------------------------------------------
    SceneNode* SceneFormatImporter::importSceneNode( const rapidjson::Value &sceneNodeValue,
                                                     uint32 nodeIdx,
                                                     const rapidjson::Value &sceneNodesJson )
    {
        SceneNode *sceneNode = 0;

        rapidjson::Value::ConstMemberIterator itTmp = sceneNodeValue.FindMember( "node" );
        if( itTmp != sceneNodeValue.MemberEnd() && itTmp->value.IsObject() )
        {
            const rapidjson::Value &nodeValue = itTmp->value;

            bool isStatic = false;
            uint32 parentIdx = nodeIdx;

            itTmp = nodeValue.FindMember( "parent_id" );
            if( itTmp != nodeValue.MemberEnd() && itTmp->value.IsUint() )
                parentIdx = itTmp->value.GetUint();

            itTmp = nodeValue.FindMember( "is_static" );
            if( itTmp != nodeValue.MemberEnd() && itTmp->value.IsBool() )
                isStatic = itTmp->value.GetBool();

            const SceneMemoryMgrTypes sceneNodeType = isStatic ? SCENE_STATIC : SCENE_DYNAMIC;

            if( parentIdx != nodeIdx )
            {
                SceneNode *parentNode = 0;
                IndexToSceneNodeMap::const_iterator parentNodeIt = mCreatedSceneNodes.find( parentIdx );
                if( parentNodeIt == mCreatedSceneNodes.end() )
                {
                    //Our parent node will be created after us. Initialize it now.
                    if( parentIdx < sceneNodesJson.Size() &&
                        sceneNodesJson[parentIdx].IsObject() )
                    {
                        parentNode = importSceneNode( sceneNodesJson[parentIdx], parentIdx,
                                                      sceneNodesJson );
                    }

                    if( !parentNode )
                    {
                        OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND,
                                     "Node " + StringConverter::toString( nodeIdx ) + " is child of " +
                                     StringConverter::toString( parentIdx ) +
                                     " but we could not find it or create it. This file is malformed.",
                                     "SceneFormatImporter::importSceneNode" );
                    }
                }
                else
                {
                    //Parent was already created
                    parentNode = parentNodeIt->second;
                }

                sceneNode = parentNode->createChildSceneNode( sceneNodeType );
            }
            else
            {
                //Has no parent. Could be root scene node,
                //or a loose node whose parent wasn't exported.
                bool isRootNode = false;
                itTmp = sceneNodeValue.FindMember( "is_root_node" );
                if( itTmp != sceneNodeValue.MemberEnd() && itTmp->value.IsBool() )
                    isRootNode = itTmp->value.GetBool();

                if( isRootNode )
                    sceneNode = mSceneManager->getRootSceneNode( sceneNodeType );
                else
                    sceneNode = mSceneManager->createSceneNode( sceneNodeType );
            }

            importNode( nodeValue, sceneNode );

            mCreatedSceneNodes[nodeIdx] = sceneNode;
        }
        else
        {
            OGRE_EXCEPT( Exception::ERR_ITEM_NOT_FOUND,
                         "Object 'node' must be present in a scene_node. SceneNode: " +
                         StringConverter::toString( nodeIdx ) + " File: " + mFilename,
                         "SceneFormatImporter::importSceneNodes" );
        }

        return sceneNode;
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSceneNodes( const rapidjson::Value &json )
    {
        rapidjson::Value::ConstMemberIterator begin = json.MemberBegin();
        rapidjson::Value::ConstMemberIterator itor = begin;
        rapidjson::Value::ConstMemberIterator end  = json.MemberEnd();

        while( itor != end )
        {
            const size_t nodeIdx = itor - begin;
            if( itor->value.IsObject() &&
                mCreatedSceneNodes.find( nodeIdx ) == mCreatedSceneNodes.end() )
            {
                importSceneNode( itor->value, nodeIdx, json );
            }

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importMovableObject( const rapidjson::Value &movableObjectValue,
                                                   MovableObject *movableObject )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;

        tmpIt = movableObjectValue.FindMember( "name" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsString() )
            movableObject->setName( tmpIt->value.GetString() );

        tmpIt = movableObjectValue.FindMember( "parent_node_id" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
        {
            uint32 nodeId = tmpIt->value.GetUint();
            IndexToSceneNodeMap::const_iterator itNode = mCreatedSceneNodes.find( nodeId );
            if( itNode != mCreatedSceneNodes.end() )
                itNode->second->attachObject( movableObject );
            else
            {
                LogManager::getSingleton().logMessage( "WARNING: MovableObject references SceneNode " +
                                                       StringConverter::toString( nodeId ) +
                                                       " which does not exist or couldn't be created" );
            }
        }

        tmpIt = movableObjectValue.FindMember( "render_queue" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
        {
            uint32 rqId = tmpIt->value.GetUint();
            movableObject->setRenderQueueGroup( rqId );
        }

        tmpIt = movableObjectValue.FindMember( "local_aabb" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsArray() )
        {
            movableObject->setLocalAabb( decodeAabbArray( tmpIt->value,
                                                          movableObject->getLocalAabb() ) );
        }

        ObjectData &objData = movableObject->_getObjectData();

        tmpIt = movableObjectValue.FindMember( "local_radius" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mLocalRadius[objData.mIndex] = decodeFloat( tmpIt->value );

        tmpIt = movableObjectValue.FindMember( "rendering_distance" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            movableObject->setRenderingDistance( decodeFloat( tmpIt->value ) );

        //Decode raw flag values
        tmpIt = movableObjectValue.FindMember( "visibility_flags" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mVisibilityFlags[objData.mIndex] = tmpIt->value.GetUint();
        tmpIt = movableObjectValue.FindMember( "query_flags" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mQueryFlags[objData.mIndex] = tmpIt->value.GetUint();
        tmpIt = movableObjectValue.FindMember( "light_mask" );
        if( tmpIt != movableObjectValue.MemberEnd() && tmpIt->value.IsUint() )
            objData.mLightMask[objData.mIndex] = tmpIt->value.GetUint();
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importRenderable( const rapidjson::Value &renderableValue,
                                                Renderable *renderable )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;

        tmpIt = renderableValue.FindMember( "custom_parameters" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsObject() )
        {
            rapidjson::Value::ConstMemberIterator itor = tmpIt->value.MemberBegin();
            rapidjson::Value::ConstMemberIterator end  = tmpIt->value.MemberEnd();

            while( itor != end )
            {
                if( itor->name.IsUint() && itor->value.IsArray() )
                {
                    const uint32 idxCustomParam = itor->name.GetUint();
                    renderable->setCustomParameter( idxCustomParam, decodeVector4Array( itor->value ) );
                }

                ++itor;
            }
        }

        bool isV1Material = false;
        tmpIt = renderableValue.FindMember( "is_v1_material" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            isV1Material = tmpIt->value.GetBool();

        tmpIt = renderableValue.FindMember( "datablock" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsString() )
        {
            if( !isV1Material )
                renderable->setDatablock( tmpIt->value.GetString() );
            else
            {
                renderable->setDatablockOrMaterialName(
                            tmpIt->value.GetString(),
                            ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME );
            }
        }

        tmpIt = renderableValue.FindMember( "custom_parameter" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsUint() )
            renderable->mCustomParameter = static_cast<uint8>( tmpIt->value.GetUint() );

        tmpIt = renderableValue.FindMember( "render_queue_sub_group" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsUint() )
            renderable->setRenderQueueSubGroup( static_cast<uint8>( tmpIt->value.GetUint() ) );

        tmpIt = renderableValue.FindMember( "polygon_mode_overrideable" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            renderable->setPolygonModeOverrideable( tmpIt->value.GetBool() );

        tmpIt = renderableValue.FindMember( "use_identity_view" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            renderable->setUseIdentityView( tmpIt->value.GetBool() );

        tmpIt = renderableValue.FindMember( "use_identity_projection" );
        if( tmpIt != renderableValue.MemberEnd() && tmpIt->value.IsBool() )
            renderable->setUseIdentityProjection( tmpIt->value.GetBool() );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importSubItem( const rapidjson::Value &subItemValue, SubItem *subItem )
    {
        rapidjson::Value::ConstMemberIterator tmpIt;
        tmpIt = subItemValue.FindMember( "renderable" );
        if( tmpIt != subItemValue.MemberEnd() && tmpIt->value.IsObject() )
            importRenderable( tmpIt->value, subItem );
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importItems( const rapidjson::Value &json )
    {
        rapidjson::Value::ConstMemberIterator begin = json.MemberBegin();
        rapidjson::Value::ConstMemberIterator itor = begin;
        rapidjson::Value::ConstMemberIterator end  = json.MemberEnd();

        while( itor != end )
        {
            if( itor->value.IsObject() )
            {
                const rapidjson::Value &itemValue = itor->value;

                String meshName, resourceGroup;

                rapidjson::Value::ConstMemberIterator tmpIt;

                tmpIt = itemValue.FindMember( "mesh" );
                if( tmpIt != itemValue.MemberEnd() && tmpIt->value.IsString() )
                    meshName = tmpIt->value.GetString();

                tmpIt = itemValue.FindMember( "mesh_resource_group" );
                if( tmpIt != itemValue.MemberEnd() && tmpIt->value.IsString() )
                    resourceGroup = tmpIt->value.GetString();

                if( resourceGroup.empty() )
                    resourceGroup = ResourceGroupManager::AUTODETECT_RESOURCE_GROUP_NAME;

                bool isStatic = false;
                rapidjson::Value const *movableObjectValue = 0;

                tmpIt = itemValue.FindMember( "movable_object" );
                if( tmpIt != itemValue.MemberEnd() && tmpIt->value.IsObject() )
                {
                    movableObjectValue = &tmpIt->value;

                    tmpIt = movableObjectValue->FindMember( "is_static" );
                    if( tmpIt != movableObjectValue->MemberEnd() && tmpIt->value.IsBool() )
                        isStatic = tmpIt->value.GetBool();
                }

                const SceneMemoryMgrTypes sceneNodeType = isStatic ? SCENE_STATIC : SCENE_DYNAMIC;

                Item *item = mSceneManager->createItem( meshName, resourceGroup, sceneNodeType );

                if( movableObjectValue )
                    importMovableObject( *movableObjectValue, item );

                tmpIt = itemValue.FindMember( "sub_items" );
                if( tmpIt != itemValue.MemberEnd() && tmpIt->value.IsArray() )
                {
                    const rapidjson::Value &subItemsArray = tmpIt->value;
                    const size_t numSubItems = std::min<size_t>( item->getNumSubItems(),
                                                                 subItemsArray.Size() );
                    for( size_t i=0; i<numSubItems; ++i )
                    {
                        const rapidjson::Value &subItemValue = subItemsArray[i];

                        if( subItemValue.IsObject() )
                            importSubItem( subItemValue, item->getSubItem( i ) );
                    }
                }
            }

            ++itor;
        }
    }
    //-----------------------------------------------------------------------------------
    void SceneFormatImporter::importScene( const String &filename, const char *jsonString )
    {
        mFilename = filename;

        rapidjson::Document d;
        d.Parse( jsonString );

        if( d.HasParseError() )
        {
            OGRE_EXCEPT( Exception::ERR_INVALIDPARAMS,
                         "SceneFormatImporter::importScene",
                         "Invalid JSON string in file " + filename );
        }

        rapidjson::Value::ConstMemberIterator itor = d.FindMember( "scene_nodes" );
        if( itor != d.MemberEnd() && itor->value.IsArray() )
            importSceneNodes( itor->value );

        itor = d.FindMember( "items" );
        if( itor != d.MemberEnd() && itor->value.IsArray() )
            importItems( itor->value );
    }
}
