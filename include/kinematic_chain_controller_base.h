#ifndef KINEMATIC_CHAIN_CONTROLLER_BASE_H
#define KINEMATIC_CHAIN_CONTROLLER_BASE_H

#include <urdf/model.h>

#include <ros/node_handle.h>
#include <ros/ros.h>

#include <kdl/tree.hpp>
#include <kdl/kdl.hpp>
#include <kdl/chain.hpp>
#include <kdl/chainfksolver.hpp>
#include <kdl/frames.hpp>
#include <kdl/chaindynparam.hpp> //this to compute the gravity vector
#include <kdl/chainjnttojacsolver.hpp>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <kdl_parser/kdl_parser.hpp>

#include <vector>

#include "ur_modern_driver/ur_driver.h"

class KinematicChainControllerBase
{
public:
  KDL::Chain kdl_chain_;
  KinematicChainControllerBase() {}
  ~KinematicChainControllerBase() {}

  bool init(ros::NodeHandle &n);

public:
  ros::NodeHandle nh_;
  KDL::JntArrayVel joint_msr_;

  struct limits_
  {
    KDL::JntArray min;
    KDL::JntArray max;
    KDL::JntArray center;
  } joint_limits_;

  // std::vector<typename JI::ResourceHandleType> joint_handles_;
};


bool KinematicChainControllerBase::init(ros::NodeHandle &n)
{
  nh_ = n;

  // get URDF and name of root and tip from the parameter server
  std::string robot_description, root_name, tip_name;
  std::string name_space = nh_.getNamespace();
  if (!ros::param::search(name_space,"robot_description", robot_description))
  {
    ROS_ERROR_STREAM("KinematicChainControllerBase: No robot description (URDF) found on parameter server (" << n.getNamespace() <<"/robot_description)");
    return false;
  }
  if (!nh_.getParam( name_space + "/root_name", root_name))
  {
    ROS_ERROR_STREAM("KinematicChainControllerBase: No root name found on parameter server ("<<n.getNamespace()<<"/root_name)");
    return false;
  }
  if (!nh_.getParam(name_space + "/tip_name", tip_name))
  {
    ROS_ERROR_STREAM("KinematicChainControllerBase: No tip name found on parameter server ("<<n.getNamespace()<<"/tip_name)");
    return false;
  }

  // Construct an URDF model from the xml string
  std::string xml_string;

  if (nh_.hasParam(robot_description))
    nh_.getParam(robot_description.c_str(), xml_string);
  else
  {
    ROS_ERROR("Parameter %s not set, shutting down node...", robot_description.c_str());
    nh_.shutdown();
    return false;
  }

  if (xml_string.size() == 0)
  {
    ROS_ERROR("Unable to load robot model from parameter %s", robot_description.c_str());
    nh_.shutdown();
    return false;
  }

  // Get urdf model out of robot_description
  urdf::Model model;
  if (!model.initString(xml_string))
  {
    ROS_ERROR("Failed to parse urdf file");
    n.shutdown();
    return false;
  }
  ROS_INFO("Successfully parsed urdf file");

  KDL::Tree kdl_tree;
  if (!kdl_parser::treeFromUrdfModel(model, kdl_tree))
  {
    ROS_ERROR("Failed to construct kdl tree");
    n.shutdown();
    return false;
  }

  // Populate the KDL chain
  if(!kdl_tree.getChain(root_name, tip_name, kdl_chain_))
  {
    ROS_ERROR_STREAM("Failed to get KDL chain from tree: ");
    ROS_ERROR_STREAM("  "<<root_name<<" --> "<<tip_name);
    ROS_ERROR_STREAM("  Tree has "<<kdl_tree.getNrOfJoints()<<" joints");
    ROS_ERROR_STREAM("  Tree has "<<kdl_tree.getNrOfSegments()<<" segments");
    ROS_ERROR_STREAM("  The segments are:");

    KDL::SegmentMap segment_map = kdl_tree.getSegments();
    KDL::SegmentMap::iterator it;

    for( it=segment_map.begin(); it != segment_map.end(); it++ )
      ROS_ERROR_STREAM( "    "<<(*it).first);

    return false;
  }

  // KDL chain information
  ROS_INFO("tip_name:  %s",tip_name.c_str());
  ROS_INFO("root_name: %s",root_name.c_str());
  ROS_INFO("Number of segments: %d", kdl_chain_.getNrOfSegments());
  ROS_INFO("Number of joints in chain: %d", kdl_chain_.getNrOfJoints());
  for(std::size_t i = 0; i < kdl_chain_.getNrOfSegments(); i++){
    ROS_INFO_STREAM("segment("<<i<<"): " << kdl_chain_.getSegment(i).getName());
  }

  // Parsing joint limits from urdf model along kdl chain
  // Param Initialize
  boost::shared_ptr<const urdf::Link> link_ = model.getLink(tip_name);
  boost::shared_ptr<const urdf::Joint> joint_;
  joint_limits_.min.resize(kdl_chain_.getNrOfJoints());
  joint_limits_.max.resize(kdl_chain_.getNrOfJoints());
  joint_limits_.center.resize(kdl_chain_.getNrOfJoints());
  int index;

  for (std::size_t i = 0; i < kdl_chain_.getNrOfJoints() && link_; i++)
  {
    joint_ = model.getJoint(link_->parent_joint->name);
    ROS_INFO("Getting limits for joint: %s", joint_->name.c_str());
    index = kdl_chain_.getNrOfJoints() - i - 1;

    if(joint_->limits){
      joint_limits_.min(index) = joint_->limits->lower;
      joint_limits_.max(index) = joint_->limits->upper;
      joint_limits_.center(index) = (joint_limits_.min(index) +
                                     joint_limits_.max(index))/2;
    }else{
      joint_limits_.min(index) = 0;
      joint_limits_.max(index) = 0;
      joint_limits_.center(index) = 0;
      ROS_INFO("joint_->limits is NULL %s",joint_->name.c_str());
    }

    link_ = model.getLink(link_->getParent()->name);
  }

  // ROS_INFO("Getting joint handles");
  // Get joint handles for all of the joints in the chain
  // int count=0;
  // for(std::vector<KDL::Segment>::const_iterator it =
  //     kdl_chain_.segments.begin(); it != kdl_chain_.segments.end(); ++it)
  // {

  //   ROS_INFO("%s type: %s", it->getJoint().getName().c_str(),
  //            it->getJoint().getTypeName().c_str() );
  //   if(it->getJoint().getTypeName() != "None" && count < 7) {
  //     joint_handles_.push_back(robot->getHandle(it->getJoint().getName()));
  //   }
  //   count++;
  // }

  // ROS_INFO("Number of joints in handle = %lu", joint_handles_.size() );
  ROS_INFO_STREAM("kdl_chain.getNrOfJoints: " << kdl_chain_.getNrOfJoints());

  ROS_INFO("Finished Kinematic Base init");

  return true;
}

#endif
