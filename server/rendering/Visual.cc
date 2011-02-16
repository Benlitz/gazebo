/*
 * Copyright 2011 Nate Koenig & Andrew Howard
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
*/
/* Desc: Ogre Visual Class
 * Author: Nate Koenig
 * Date: 14 Dec 2007
 */

#include <Ogre.h>

#include "Messages.hh"
#include "World.hh"
#include "Events.hh"
#include "OgreDynamicLines.hh"
#include "Scene.hh"
#include "SelectionObj.hh"
#include "RTShaderSystem.hh"
#include "MeshManager.hh"
#include "Simulator.hh"
#include "Entity.hh"
#include "GazeboMessage.hh"
#include "GazeboError.hh"
#include "XMLConfig.hh"
#include "OgreAdaptor.hh"
#include "Global.hh"
#include "Mesh.hh"
#include "Material.hh"
#include "Visual.hh"

using namespace gazebo;

SelectionObj *Visual::selectionObj = 0;
unsigned int Visual::visualCounter = 0;

////////////////////////////////////////////////////////////////////////////////
// Constructor
Visual::Visual(const std::string &name, Visual *parent)
  : Common(parent)
{
  this->SetName(name);
  this->AddType(VISUAL);
  this->sceneNode = NULL;

  Ogre::SceneNode *pnode = NULL;
  if (parent)
    pnode = parent->GetSceneNode();
  else
    gzerr(0) << "Create a visual, invalid parent!!!\n";

    this->sceneNode = pnode->createChildSceneNode( this->GetName() );

  this->Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor
Visual::Visual (const std::string &name, Ogre::SceneNode *parent)
  : Common(NULL)
{
  this->SetName(name);
  this->AddType(VISUAL);
  this->sceneNode = NULL;

  this->sceneNode = parent->createChildSceneNode( this->GetName() );

  this->Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Constructor
Visual::Visual (const std::string &name, Scene *scene)
  : Common(NULL)
{
  this->SetName(name);
  this->AddType(VISUAL);
  this->sceneNode = NULL;

  this->sceneNode = scene->GetManager()->getRootSceneNode()->createChildSceneNode(this->GetName());

  this->Init();
}

////////////////////////////////////////////////////////////////////////////////
/// Destructor
Visual::~Visual()
{
  delete this->xyzP;
  delete this->rpyP;
  delete this->meshNameP;
  delete this->meshTileP;
  delete this->materialNameP;
  delete this->castShadowsP;

  // delete instance from lines vector
  for (std::list<OgreDynamicLines*>::iterator iter=this->lines.begin();
       iter!=this->lines.end();iter++)
    delete *iter;
  this->lines.clear();

  RTShaderSystem::Instance()->DetachEntity(this);

  if (this->sceneNode != NULL)
  {
    this->sceneNode->removeAndDestroyAllChildren();
    this->sceneNode->detachAllObjects();

    this->sceneNode->getParentSceneNode()->removeAndDestroyChild( this->sceneNode->getName() );
    this->sceneNode = NULL;
  }
}

////////////////////////////////////////////////////////////////////////////////
// Helper for the contructor
void Visual::Init()
{
  this->transparency = 0.0;
  this->isStatic = false;
  this->useRTShader = true;
  this->visible = true;
  this->ribbonTrail = NULL;
  this->boundingBoxNode = NULL;
  this->staticGeom = NULL;

  Param::Begin(&this->parameters);
  this->xyzP = new ParamT<Vector3>("xyz", Vector3(0,0,0), 0);
  this->xyzP->Callback( &Visual::SetPosition, this );

  this->rpyP = new ParamT<Quatern>("rpy", Quatern(1,0,0,0), 0);
  this->rpyP->Callback( &Visual::SetRotation, this );

  this->meshNameP = new ParamT<std::string>("mesh","",1);
  this->meshTileP = new ParamT< Vector2<double> >("uvTile", 
                                Vector2<double>(1.0, 1.0), 0 );
 
  //default to Gazebo/White
  this->materialNameP = new ParamT<std::string>("material",
                                                std::string("none"),0);
  this->materialNameP->Callback( &Visual::SetMaterial, this );

  this->castShadowsP = new ParamT<bool>("castShadows",true,0);
  this->castShadowsP->Callback( &Visual::SetCastShadows, this );

  this->scaleP = new ParamT<Vector3>("scale", Vector3(1,1,1), 0);

  this->normalMapNameP = new ParamT<std::string>("normalMap",
                                                std::string("none"),0);
  this->normalMapNameP->Callback( &Visual::SetNormalMap, this );

  this->shaderP = new ParamT<std::string>("shader", std::string("pixel"),0);
  this->shaderP->Callback( &Visual::SetShader, this );
  Param::End();

  RTShaderSystem::Instance()->AttachEntity(this);
}

////////////////////////////////////////////////////////////////////////////////
void Visual::LoadFromMsg(const VisualMsg *msg)
{
  std::string mesh = msg->mesh;

  if (msg->plane.normal != Vector3(0,0,0))
  {
    MeshManager::Instance()->CreatePlane(msg->id, msg->plane,
        Vector2<double>(2,2), Vector2<double>(msg->uvTile_x, msg->uvTile_y) );
    mesh = msg->id;
  }

  this->meshNameP->SetValue(mesh);
  this->xyzP->SetValue(msg->pose.pos);
  this->rpyP->SetValue(msg->pose.rot);
  this->meshTileP->Load(NULL);
  this->materialNameP->SetValue(msg->material);
  this->castShadowsP->SetValue(msg->castShadows);
  this->scaleP->SetValue(msg->scale);

  this->Load(NULL);
  this->UpdateFromMsg(msg);
}

////////////////////////////////////////////////////////////////////////////////
// Load the visual
void Visual::Load(XMLConfigNode *node)
{
  std::ostringstream stream;
  Pose3d pose;
  Vector3 size(0,0,0);
  Ogre::Vector3 meshSize(1,1,1);
  Ogre::MovableObject *obj = NULL;

  if (node)
  {
    this->xyzP->Load(node);
    this->rpyP->Load(node);
    this->meshNameP->Load(node);
    this->meshTileP->Load(node);
    this->materialNameP->Load(node);
    this->castShadowsP->Load(node);
    this->shaderP->Load(node);
    this->normalMapNameP->Load(node);
    this->scaleP->Load(node);
  }

  // Read the desired position and rotation of the mesh
  pose.pos = this->xyzP->GetValue();
  pose.rot = this->rpyP->GetValue();

  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  try
  {
    // Create the visual
    stream << "VISUAL_" << this->sceneNode->getName();
    std::string meshName = (**this->meshNameP);
    if (!meshName.empty())
    {
      if ( meshName == "unit_box")
      {
        meshName += "_U" + 
          boost::lexical_cast<std::string>(this->meshTileP->GetValue().x) + "V" +
          boost::lexical_cast<std::string>(this->meshTileP->GetValue().y);

        if (!MeshManager::Instance()->HasMesh(meshName));
        {
          MeshManager::Instance()->CreateBox(meshName, Vector3(1,1,1), 
              **this->meshTileP);
        }
      }

      if (!MeshManager::Instance()->HasMesh(meshName))
      {
        MeshManager::Instance()->Load(meshName);
      }

      // Add the mesh into OGRE
      this->InsertMesh( MeshManager::Instance()->GetMesh(meshName) );

      Ogre::SceneManager *mgr = this->sceneNode->getCreator();
      if (mgr->hasEntity(stream.str()))
        obj = (Ogre::MovableObject*)mgr->getEntity(stream.str());
      else
        obj = (Ogre::MovableObject*)mgr->createEntity( stream.str(), meshName);
    }
  }
  catch (Ogre::Exception e)
  {
    std::cerr << "Ogre Error:" << e.getFullDescription() << "\n";
    gzthrow("Unable to create a mesh from " + this->meshNameP->GetValue());
  }

  // Attach the entity to the node
  if (obj)
  {
    this->AttachObject(obj);
    obj->setVisibilityFlags(GZ_ALL_CAMERA);
  }

  // Set the pose of the scene node
  this->SetPose(pose);

  // Get the size of the mesh
  if (obj)
  {
    meshSize = obj->getBoundingBox().getSize();
  }

  this->sceneNode->setScale((**this->scaleP).x,(**this->scaleP).y, (**this->scaleP).z );

  // Set the material of the mesh
  if (**this->materialNameP != "none")
    this->SetMaterial(this->materialNameP->GetValue());

  // Allow the mesh to cast shadows
  this->SetCastShadows((**this->castShadowsP));
}

////////////////////////////////////////////////////////////////////////////////
/// Update the visual.
void Visual::Update()
{
  if (!this->visible)
    return;

  std::list<OgreDynamicLines*>::iterator iter;

  // Update the lines
  for (iter = this->lines.begin(); iter != this->lines.end(); iter++)
    (*iter)->Update();
}

////////////////////////////////////////////////////////////////////////////////
/// Set the owner
void Visual::SetOwner(Common *common)
{
  this->owner = common;
}

////////////////////////////////////////////////////////////////////////////////
// Get the owner
Common *Visual::GetOwner() const
{
  return this->owner;
}

////////////////////////////////////////////////////////////////////////////////
// Save the visual in XML format
void Visual::Save(std::string &prefix, std::ostream &stream)
{
  stream << prefix << "<visual>\n";
  stream << prefix << "  " << *(this->xyzP) << "\n";
  stream << prefix << "  " << *(this->rpyP) << "\n";
  stream << prefix << "  " << *(this->meshNameP) << "\n";
  stream << prefix << "  " << *(this->materialNameP) << "\n";
  stream << prefix << "  " << *(this->castShadowsP) << "\n";
  stream << prefix << "  " << *(this->scaleP) << "\n";
  stream << prefix << "</visual>\n";
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a visual
void Visual::AttachVisual(Visual *vis)
{
  vis->GetSceneNode()->getParentSceneNode()->removeChild(vis->GetSceneNode());
  this->sceneNode->addChild( vis->GetSceneNode() );
  vis->SetParent(this);
}

////////////////////////////////////////////////////////////////////////////////
/// Detach a visual 
void Visual::DetachVisual(Visual *vis)
{
  this->sceneNode->removeChild(vis->GetSceneNode());
  vis->SetParent(NULL);
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a renerable object to the visual
void Visual::AttachObject( Ogre::MovableObject *obj)
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  this->sceneNode->attachObject(obj);
  RTShaderSystem::Instance()->UpdateShaders();

  obj->setUserAny( Ogre::Any(this) );
}

////////////////////////////////////////////////////////////////////////////////
/// Detach all objects
void Visual::DetachObjects()
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  this->sceneNode->detachAllObjects();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the number of attached objects
unsigned short Visual::GetNumAttached()
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return 0;

  return this->sceneNode->numAttachedObjects();
}

////////////////////////////////////////////////////////////////////////////////
/// Get an attached object
Ogre::MovableObject *Visual::GetAttached(unsigned short num)
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return NULL;

  return this->sceneNode->getAttachedObject(num);
}

////////////////////////////////////////////////////////////////////////////////
// Attach a static object
void Visual::MakeStatic()
{
  /*boost::recursive_mutex::scoped_lock lock(*this->mutex);

  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  if (!this->staticGeom)
    this->staticGeom = this->scene->GetManager()->createStaticGeometry(this->sceneNode->getName() + "_Static");

  // Add the scene node to the static geometry
  this->staticGeom->addSceneNode(this->sceneNode);

  // Build the static geometry
  this->staticGeom->build();

  // Prevent double rendering
  this->sceneNode->setVisible(false);
  */
}

////////////////////////////////////////////////////////////////////////////////
/// Attach a mesh to this visual by name
void Visual::AttachMesh( const std::string &meshName )
{

  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  std::ostringstream stream;
  Ogre::MovableObject *obj;
  stream << this->sceneNode->getName() << "_ENTITY_" << meshName;

  // Add the mesh into OGRE
  if (!this->sceneNode->getCreator()->hasEntity(meshName) &&
      MeshManager::Instance()->HasMesh(meshName))
  {
    const Mesh *mesh = MeshManager::Instance()->GetMesh(meshName);

    this->InsertMesh( mesh );
  }

  obj = (Ogre::MovableObject*)(this->sceneNode->getCreator()->createEntity(stream.str(), meshName));

  this->AttachObject( obj );
}

////////////////////////////////////////////////////////////////////////////////
///  Set the scale
void Visual::SetScale(const Vector3 &scale )
{
  this->scaleP->SetValue(scale);
  this->sceneNode->setScale(Ogre::Vector3(scale.x, scale.y, scale.z));
}

////////////////////////////////////////////////////////////////////////////////
/// Get the scale
Vector3 Visual::GetScale()
{
  Ogre::Vector3 vscale;
  vscale=this->sceneNode->getScale();
  return Vector3(vscale.x, vscale.y, vscale.z);
}


////////////////////////////////////////////////////////////////////////////////
// Set the material
void Visual::SetMaterial(const std::string &materialName)
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  if (materialName.empty())
    return;

  Ogre::MaterialPtr origMaterial;

  try
  {
    this->origMaterialName = materialName;
    // Get the original material
    origMaterial= Ogre::MaterialManager::getSingleton().getByName (materialName);;
  }
  catch (Ogre::Exception e)
  {
    gzmsg(0) << "Unable to get Material[" << materialName << "] for Geometry["
    << this->sceneNode->getName() << ". Object will appear white.\n";
    return;
  }

  if (origMaterial.isNull())
  {
    gzmsg(0) << "Unable to get Material[" << materialName << "] for Geometry["
    << this->sceneNode->getName() << ". Object will appear white\n";
    return;
  }


  // Create a custom material name
  this->myMaterialName = this->sceneNode->getName() + "_MATERIAL_" + materialName;

  Ogre::MaterialPtr myMaterial;

  // Clone the material. This will allow us to change the look of each geom
  // individually.
  if (Ogre::MaterialManager::getSingleton().resourceExists(this->myMaterialName))
    myMaterial = (Ogre::MaterialPtr)(Ogre::MaterialManager::getSingleton().getByName(this->myMaterialName));
  else
    myMaterial = origMaterial->clone(this->myMaterialName);

  Ogre::Material::TechniqueIterator techniqueIt = myMaterial->getTechniqueIterator ();

  /*while (techniqueIt.hasMoreElements ())
  {
    Ogre::Technique *t = techniqueIt.getNext ();
    Ogre::Technique::PassIterator passIt = t->getPassIterator ();
    while (passIt.hasMoreElements ())
    {
      passIt.peekNext ()->setDepthWriteEnabled (true);
      passIt.peekNext ()->setSceneBlending (this->sceneBlendType);
      passIt.moveNext ();
    }
  }*/

  try
  {
    for (int i=0; i < this->sceneNode->numAttachedObjects(); i++)
    {
      Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);

      if (dynamic_cast<Ogre::Entity*>(obj))
        ((Ogre::Entity*)obj)->setMaterialName(this->myMaterialName);
      else
        ((Ogre::SimpleRenderable*)obj)->setMaterial(this->myMaterialName);
    }

  }
  catch (Ogre::Exception e)
  {
    gzmsg(0) << "Unable to set Material[" << myMaterialName << "] to Geometry["
    << this->sceneNode->getName() << ". Object will appear white.\n";
  }
}


void Visual::AttachAxes()
{
  std::ostringstream nodeName;

  nodeName << this->sceneNode->getName()<<"_AXES_NODE";
 
  if (!this->sceneNode->getCreator()->hasEntity("axis_cylinder"))
    this->InsertMesh(MeshManager::Instance()->GetMesh("axis_cylinder"));

  Ogre::SceneNode *node = this->sceneNode->createChildSceneNode(nodeName.str());
  Ogre::SceneNode *x, *y, *z;

  x = node->createChildSceneNode(nodeName.str() + "_axisX");
  x->setInheritScale(true);
  x->translate(.25,0,0);
  x->yaw(Ogre::Radian(M_PI/2.0));

  y = node->createChildSceneNode(nodeName.str() + "_axisY");
  y->setInheritScale(true);
  y->translate(0,.25,0);
  y->pitch(Ogre::Radian(M_PI/2.0));

  z = node->createChildSceneNode(nodeName.str() + "_axisZ");
  z->translate(0,0,.25);
  z->setInheritScale(true);
  
  Ogre::MovableObject *xobj, *yobj, *zobj;

  xobj = (Ogre::MovableObject*)(node->getCreator()->createEntity(nodeName.str()+"X_AXIS", "axis_cylinder"));
  xobj->setCastShadows(false);
  ((Ogre::Entity*)xobj)->setMaterialName("Gazebo/Red");

  yobj = (Ogre::MovableObject*)(node->getCreator()->createEntity(nodeName.str()+"Y_AXIS", "axis_cylinder"));
  yobj->setCastShadows(false);
  ((Ogre::Entity*)yobj)->setMaterialName("Gazebo/Green");

  zobj = (Ogre::MovableObject*)(node->getCreator()->createEntity(nodeName.str()+"Z_AXIS", "axis_cylinder"));
  zobj->setCastShadows(false);
  ((Ogre::Entity*)zobj)->setMaterialName("Gazebo/Blue");

  x->attachObject(xobj);
  y->attachObject(yobj);
  z->attachObject(zobj);
}


////////////////////////////////////////////////////////////////////////////////
/// Set the transparency
void Visual::SetTransparency( float trans )
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  this->transparency = std::min(std::max(trans, (float)0.0), (float)1.0);
  for (unsigned int i=0; i < this->sceneNode->numAttachedObjects(); i++)
  {
    Ogre::Entity *entity = NULL;
    Ogre::SimpleRenderable *simple = NULL;
    Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);

    entity = dynamic_cast<Ogre::Entity*>(obj);

    if (!entity)
      continue;

    // For each ogre::entity
    for (unsigned int j=0; j < entity->getNumSubEntities(); j++)
    {
      Ogre::SubEntity *subEntity = entity->getSubEntity(j);
      Ogre::MaterialPtr material = subEntity->getMaterial();
      Ogre::Material::TechniqueIterator techniqueIt = material->getTechniqueIterator();

      unsigned int techniqueCount, passCount;
      Ogre::Technique *technique;
      Ogre::Pass *pass;
      Ogre::ColourValue dc;

      for (techniqueCount = 0; techniqueCount < material->getNumTechniques(); 
           techniqueCount++)
      {
        technique = material->getTechnique(techniqueCount);

        for (passCount=0; passCount < technique->getNumPasses(); passCount++)
        {
          pass = technique->getPass(passCount);
          // Need to fix transparency
          /*if (pass->getPolygonMode() == Ogre::PM_SOLID)
            pass->setSceneBlending(Ogre::SBT_TRANSPARENT_ALPHA);
            */

          if (this->transparency > 0.0)
            pass->setDepthWriteEnabled(false);
          else
            pass->setDepthWriteEnabled(true);

          dc = pass->getDiffuse();
          dc.a = (1.0f - this->transparency);
          pass->setDiffuse(dc);
        }
      }
    }
  }

}

////////////////////////////////////////////////////////////////////////////////
/// Get the transparency
float Visual::GetTransparency()
{
  return this->transparency;
}

////////////////////////////////////////////////////////////////////////////////
void Visual::SetHighlight(bool highlight)
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;
}


////////////////////////////////////////////////////////////////////////////////
/// Set whether the visual should cast shadows
void Visual::SetCastShadows(const bool &shadows)
{

  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  for (int i=0; i < this->sceneNode->numAttachedObjects(); i++)
  {
    Ogre::MovableObject *obj = this->sceneNode->getAttachedObject(i);
    obj->setCastShadows(shadows);
  }

  if (this->IsStatic() && this->staticGeom)
    this->staticGeom->setCastShadows(shadows);
}

////////////////////////////////////////////////////////////////////////////////
/// Set whether the visual is visible
void Visual::SetVisible(bool visible, bool cascade)
{
  this->sceneNode->setVisible( visible, cascade );
  this->visible = visible;
}

////////////////////////////////////////////////////////////////////////////////
/// Toggle whether this visual is visible
void Visual::ToggleVisible()
{
  this->SetVisible( !this->GetVisible() );
}

////////////////////////////////////////////////////////////////////////////////
/// Get whether the visual is visible
bool Visual::GetVisible() const
{
  return this->visible;
}

////////////////////////////////////////////////////////////////////////////////
// Set the position of the visual
void Visual::SetPosition( const Vector3 &pos)
{
  /*if (this->IsStatic() && this->staticGeom)
  {
    this->staticGeom->reset();
    delete this->staticGeom;
    this->staticGeom = NULL;
    //this->staticGeom->setOrigin( Ogre::Vector3(pos.x, pos.y, pos.z) );
  }*/

  this->sceneNode->setPosition(pos.x, pos.y, pos.z);

  /*if (this->IsStatic())
    this->MakeStatic();
    */
}

////////////////////////////////////////////////////////////////////////////////
// Set the rotation of the visual
void Visual::SetRotation( const Quatern &rot)
{

  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  this->sceneNode->setOrientation(rot.u, rot.x, rot.y, rot.z);
}

////////////////////////////////////////////////////////////////////////////////
// Set the pose of the visual
void Visual::SetPose( const Pose3d &pose)
{

  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  this->SetPosition( pose.pos );
  this->SetRotation( pose.rot);
}

////////////////////////////////////////////////////////////////////////////////
// Set the position of the visual
Vector3 Visual::GetPosition() const
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return Vector3();

  Ogre::Vector3 vpos;
  Vector3 pos;
  vpos=this->sceneNode->getPosition();
  pos.x=vpos.x;
  pos.y=vpos.y;
  pos.z=vpos.z;
  return pos;
}

////////////////////////////////////////////////////////////////////////////////
// Get the rotation of the visual
Quatern Visual::GetRotation( ) const
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return Quatern();

  Ogre::Quaternion vquatern;
  Quatern quatern;
  vquatern=this->sceneNode->getOrientation();
  quatern.u =vquatern.w;
  quatern.x=vquatern.x;
  quatern.y=vquatern.y;
  quatern.z=vquatern.z;
  return quatern;
}

////////////////////////////////////////////////////////////////////////////////
// Get the pose of the visual
Pose3d Visual::GetPose() const
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return Pose3d();

  Pose3d pos;
  pos.pos=this->GetPosition();
  pos.rot=this->GetRotation();
  return pos;
}

////////////////////////////////////////////////////////////////////////////////
/// Get the global pose of the node
Pose3d Visual::GetWorldPose() const
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return Pose3d();

  Pose3d pose;

  Ogre::Vector3 vpos;
  Ogre::Quaternion vquatern;

  vpos=this->sceneNode->_getDerivedPosition();
  pose.pos.x=vpos.x;
  pose.pos.y=vpos.y;
  pose.pos.z=vpos.z;

  vquatern=this->sceneNode->getOrientation();
  pose.rot.u =vquatern.w;
  pose.rot.x=vquatern.x;
  pose.rot.y=vquatern.y;
  pose.rot.z=vquatern.z;


  return pose;
}


////////////////////////////////////////////////////////////////////////////////
// Get this visual Ogre node
Ogre::SceneNode * Visual::GetSceneNode() const
{
  return this->sceneNode;
}


////////////////////////////////////////////////////////////////////////////////
///  Create a bounding box for this visual
void Visual::AttachBoundingBox(const Vector3 &min, const Vector3 &max)
{
  std::ostringstream nodeName;

  nodeName << this->sceneNode->getName()<<"_AABB_NODE";
  std::cout << "BB Node name:" << nodeName.str() <<"|" << min << ":" << max << "\n";

  int i=0;
  while (this->sceneNode->getCreator()->hasSceneNode(nodeName.str()))
  {
    nodeName << "_" << i;
    i++;
  }

  this->boundingBoxNode = this->sceneNode->createChildSceneNode(nodeName.str());
  this->boundingBoxNode->setInheritScale(false);

  if (!this->sceneNode->getCreator()->hasEntity("unit_box_U1V1"))
  {
    // Add the mesh into OGRE
    this->InsertMesh(MeshManager::Instance()->GetMesh("unit_box_U1V1"));
  }

  Ogre::MovableObject *odeObj = (Ogre::MovableObject*)(this->sceneNode->getCreator()->createEntity(nodeName.str()+"_OBJ", "unit_box_U1V1"));
  odeObj->setQueryFlags(0);

  this->boundingBoxNode->attachObject(odeObj);
  Vector3 diff = max-min;

  this->boundingBoxNode->setScale(diff.x, diff.y, diff.z);

  Ogre::Entity *ent = NULL;
  Ogre::SimpleRenderable *simple = NULL;

  ent = dynamic_cast<Ogre::Entity*>(odeObj);
  simple = dynamic_cast<Ogre::SimpleRenderable*>(odeObj);

  if (ent)
    ent->setMaterialName("Gazebo/GreenTransparent");
  else if (simple)
    simple->setMaterial("Gazebo/GreenTransparent");

  this->boundingBoxNode->setVisible(false);

}

////////////////////////////////////////////////////////////////////////////////
// Set the material of the bounding box
void Visual::SetBoundingBoxMaterial(const std::string &materialName )
{
  // Stop here if the rendering engine has been disabled
  if (!Simulator::Instance()->GetRenderEngineEnabled())
    return;

  if (materialName.empty())
    return;

  try
  {
    for (int i=0; i < this->boundingBoxNode->numAttachedObjects(); i++)
    {
      Ogre::MovableObject *obj = this->boundingBoxNode->getAttachedObject(i);

      if (dynamic_cast<Ogre::Entity*>(obj))
        ((Ogre::Entity*)obj)->setMaterialName(materialName);
      else
        ((Ogre::SimpleRenderable*)obj)->setMaterial(materialName);
    }
  }
  catch (Ogre::Exception e)
  {
    gzmsg(0) << "Unable to set BoundingBoxMaterial[" << materialName << "][" << e.getFullDescription() << "]\n";
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Set to true to show a white bounding box, used to indicate user selection
void Visual::ShowSelectionBox( bool value )
{
  std::cout << "Show Selection for[" << this->GetName() << "]\n";

  if (!selectionObj)
  {
    selectionObj = new SelectionObj();
    selectionObj->Load();
  }

  if (value)
    selectionObj->Attach(this);
  else
    selectionObj->Attach(NULL);
    
}


////////////////////////////////////////////////////////////////////////////////
/// Return true if the  visual is a static geometry
bool Visual::IsStatic() const
{
  return this->isStatic;
}


////////////////////////////////////////////////////////////////////////////////
/// Set one visual to track/follow another
void Visual::EnableTrackVisual( Visual *vis )
{
  this->sceneNode->setAutoTracking(true, vis->GetSceneNode() );
}

////////////////////////////////////////////////////////////////////////////////
/// Disable tracking of a visual
void Visual::DisableTrackVisual()
{
  this->sceneNode->setAutoTracking(false);
}

////////////////////////////////////////////////////////////////////////////////
/// Get the normal map
std::string Visual::GetNormalMap() const
{
  return (**this->normalMapNameP);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the normal map
void Visual::SetNormalMap(const std::string &nmap)
{
  this->normalMapNameP->SetValue(nmap);
  RTShaderSystem::Instance()->UpdateShaders();
}

////////////////////////////////////////////////////////////////////////////////
/// Get the shader
std::string Visual::GetShader() const
{
  return (**this->shaderP);
}

////////////////////////////////////////////////////////////////////////////////
/// Set the shader
void Visual::SetShader(const std::string &shader)
{
  this->shaderP->SetValue(shader);
  RTShaderSystem::Instance()->UpdateShaders();
}

////////////////////////////////////////////////////////////////////////////////
void Visual::SetRibbonTrail(bool value)
{
  if (this->ribbonTrail == NULL)
  {
    this->ribbonTrail = (Ogre::RibbonTrail*)this->GetWorld()->GetScene()->GetManager()->createMovableObject("RibbonTrail");
    this->ribbonTrail->setMaterialName("Gazebo/Red");
    this->ribbonTrail->setTrailLength(200);
    this->ribbonTrail->setMaxChainElements(1000);
    this->ribbonTrail->setNumberOfChains(1);
    this->ribbonTrail->setVisible(false);
    this->ribbonTrail->setInitialWidth(0,0.05);
    this->sceneNode->attachObject(this->ribbonTrail);
    //this->scene->GetManager()->getRootSceneNode()->attachObject(this->ribbonTrail);
  }


  if (value)
  {
    try
    {
      this->ribbonTrail->addNode(this->sceneNode);
    } catch (...) { }
  }
  else
  {
    this->ribbonTrail->removeNode(this->sceneNode);
    this->ribbonTrail->clearChain(0);
  }
  this->ribbonTrail->setVisible(value);
}

////////////////////////////////////////////////////////////////////////////////
/// Get the size of the bounding box
Vector3 Visual::GetBoundingBoxSize() const
{
  this->sceneNode->_updateBounds();
  Ogre::AxisAlignedBox box = this->sceneNode->_getWorldAABB();
  Ogre::Vector3 size = box.getSize();
  return Vector3(size.x, size.y, size.z);
}

////////////////////////////////////////////////////////////////////////////////
/// Set whether to use the RT Shader system
void Visual::SetUseRTShader(bool value)
{
  this->useRTShader = value;
}

////////////////////////////////////////////////////////////////////////////////
/// Get whether to user the RT shader system
bool Visual::GetUseRTShader() const
{
  return this->useRTShader;
}

////////////////////////////////////////////////////////////////////////////////
// Add a line to the visual
OgreDynamicLines *Visual::AddDynamicLine(RenderOpType type)
{
  Events::ConnectPreRenderSignal( boost::bind(&Visual::Update, this) );

  OgreDynamicLines *line = new OgreDynamicLines(type);
  this->lines.push_back(line);
  this->AttachObject(line);
  return line;
}

////////////////////////////////////////////////////////////////////////////////
// Delete a dynamic line
void Visual::DeleteDynamicLine(OgreDynamicLines *line)
{
  // delete instance from lines vector
  for (std::list<OgreDynamicLines*>::iterator iter=this->lines.begin();iter!=this->lines.end();iter++)
  {
    if (*iter == line)
    {
      this->lines.erase(iter);
      break;
    }
  }

  if (this->lines.size() == 0)
    Events::DisconnectPreRenderSignal( boost::bind(&Visual::Update, this) );
}

////////////////////////////////////////////////////////////////////////////////
/// Get the name of the material
std::string Visual::GetMaterialName() const
{
  return this->myMaterialName;
}

////////////////////////////////////////////////////////////////////////////////
// Get the bounds
Box Visual::GetBounds() const
{
  Box box;
  this->GetBoundsHelper(this->GetSceneNode(), box);
  return box;
}

////////////////////////////////////////////////////////////////////////////////
// Get the bounding box for a scene node
void Visual::GetBoundsHelper(Ogre::SceneNode *node, Box &box) const
{
  node->_updateBounds();

  Ogre::SceneNode::ChildNodeIterator it = node->getChildIterator();

  for (int i=0; i < node->numAttachedObjects(); i++)
  {
    Ogre::MovableObject *obj = node->getAttachedObject(i);
    if (obj->isVisible() && obj->getMovableType() != "gazebo::ogredynamiclines")
    {
      Ogre::Any any = obj->getUserAny();
      if (any.getType() == typeid(std::string))
      {
        std::string str = Ogre::any_cast<std::string>(any);
        if (str.substr(0,3) == "rot" || str.substr(0,5) == "trans")
          continue;
      }

      Ogre::AxisAlignedBox bb = obj->getWorldBoundingBox();
      Ogre::Vector3 min = bb.getMinimum();
      Ogre::Vector3 max = bb.getMaximum();
      box.Merge(Box(Vector3(min.x, min.y, min.z), Vector3(max.x, max.y, max.z)));
    }
  }

  while(it.hasMoreElements())
  {
    Ogre::SceneNode *next = dynamic_cast<Ogre::SceneNode*>(it.getNext());
    this->GetBoundsHelper( next, box);
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Insert a mesh into Ogre 
void Visual::InsertMesh( const Mesh *mesh)
{
  Ogre::MeshPtr ogreMesh;

  if (mesh->GetSubMeshCount() == 0)
    return;

  try
  {
    // Create a new mesh specifically for manual definition.
    ogreMesh = Ogre::MeshManager::getSingleton().createManual(mesh->GetName(),
        Ogre::ResourceGroupManager::DEFAULT_RESOURCE_GROUP_NAME);

    for (unsigned int i=0; i < mesh->GetSubMeshCount(); i++)
    {
      Ogre::SubMesh *ogreSubMesh;
      Ogre::VertexData *vertexData;
      Ogre::VertexDeclaration* vertexDecl;
      Ogre::HardwareVertexBufferSharedPtr vBuf;
      Ogre::HardwareIndexBufferSharedPtr iBuf;
      float *vertices;
      unsigned short *indices;


      size_t currOffset = 0;

      const SubMesh *subMesh = mesh->GetSubMesh(i);

      ogreSubMesh = ogreMesh->createSubMesh();
      ogreSubMesh->useSharedVertices = false;
      ogreSubMesh->vertexData = new Ogre::VertexData();
      vertexData = ogreSubMesh->vertexData;
      vertexDecl = vertexData->vertexDeclaration;

      // The vertexDecl should contain positions, blending weights, normals,
      // diffiuse colors, specular colors, tex coords. In that order.
      vertexDecl->addElement(0, currOffset, Ogre::VET_FLOAT3, 
                             Ogre::VES_POSITION);
      currOffset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);

      // TODO: blending weights
      
      // normals
      if (subMesh->GetNormalCount() > 0 )
      {
        vertexDecl->addElement(0, currOffset, Ogre::VET_FLOAT3, 
                               Ogre::VES_NORMAL);
        currOffset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT3);
      }

      // TODO: diffuse colors

      // TODO: specular colors

      // two dimensional texture coordinates
      if (subMesh->GetTexCoordCount() > 0)
      {
        vertexDecl->addElement(0, currOffset, Ogre::VET_FLOAT2,
            Ogre::VES_TEXTURE_COORDINATES, 0);
        currOffset += Ogre::VertexElement::getTypeSize(Ogre::VET_FLOAT2);
      }

      // allocate the vertex buffer
      vertexData->vertexCount = subMesh->GetVertexCount();

      vBuf = Ogre::HardwareBufferManager::getSingleton().createVertexBuffer(
                 vertexDecl->getVertexSize(0),
                 vertexData->vertexCount,
                 Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
                 false);

      vertexData->vertexBufferBinding->setBinding(0, vBuf);
      vertices = static_cast<float*>(vBuf->lock(
                      Ogre::HardwareBuffer::HBL_DISCARD));

      // allocate index buffer
      ogreSubMesh->indexData->indexCount = subMesh->GetIndexCount();

      ogreSubMesh->indexData->indexBuffer =
        Ogre::HardwareBufferManager::getSingleton().createIndexBuffer(
            Ogre::HardwareIndexBuffer::IT_16BIT,
            ogreSubMesh->indexData->indexCount,
            Ogre::HardwareBuffer::HBU_STATIC_WRITE_ONLY,
            false);

      iBuf = ogreSubMesh->indexData->indexBuffer;
      indices = static_cast<unsigned short*>(
          iBuf->lock(Ogre::HardwareBuffer::HBL_DISCARD));

      unsigned int j;

      // Add all the vertices
      for (j =0; j < subMesh->GetVertexCount(); j++)
      {
        *vertices++ = subMesh->GetVertex(j).x;
        *vertices++ = subMesh->GetVertex(j).y;
        *vertices++ = subMesh->GetVertex(j).z;

        if (subMesh->GetNormalCount() > 0)
        {
          *vertices++ = subMesh->GetNormal(j).x;
          *vertices++ = subMesh->GetNormal(j).y;
          *vertices++ = subMesh->GetNormal(j).z;
        }

        if (subMesh->GetTexCoordCount() > 0)
        {
          *vertices++ = subMesh->GetTexCoord(j).x;
          *vertices++ = subMesh->GetTexCoord(j).y;
        }
      }

      // Add all the indices
      for (j =0; j < subMesh->GetIndexCount(); j++)
        *indices++ = subMesh->GetIndex(j);

      const Material *material;
      material = mesh->GetMaterial( subMesh->GetMaterialIndex() );
      if (material)
      {
        ogreSubMesh->setMaterialName( material->GetName() );
      }

      // Unlock
      vBuf->unlock();
      iBuf->unlock();
    }

    Vector3 max = mesh->GetMax();
    Vector3 min = mesh->GetMin();

    if (!max.IsFinite())
      gzthrow("Max bounding box is not finite[" << max << "]\n");

    if (!min.IsFinite())
      gzthrow("Min bounding box is not finite[" << min << "]\n");


    ogreMesh->_setBounds( Ogre::AxisAlignedBox(
          Ogre::Vector3(min.x, min.y, min.z),
          Ogre::Vector3(max.x, max.y, max.z)), 
          false );

    // this line makes clear the mesh is loaded (avoids memory leaks)
    ogreMesh->load();
  }
  catch (Ogre::Exception e)
  {
    gzerr(0) << "Unable to create a basic Unit cylinder object" << std::endl;
  }
}

////////////////////////////////////////////////////////////////////////////////
/// Update a visual based on a message
void Visual::UpdateFromMsg(const VisualMsg *msg)
{
  this->SetPose(msg->pose);
  this->SetTransparency(msg->transparency);
  this->SetScale(msg->scale);
  this->SetVisible(msg->visible);

  if (msg->points.size() > 0)
  {
    OgreDynamicLines *lines = this->AddDynamicLine(RENDERING_LINE_LIST);
    for (unsigned int i=0; i < msg->points.size(); i++)
      lines->AddPoint( msg->points[i] );
  }
}


