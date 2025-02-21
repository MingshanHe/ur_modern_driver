#include <pluginlib/class_list_macros.h>
#include "kinematic_chain_controller_base.h"
#include "cartesian_velocity_controller.h"
#include "kdl_conversions/kdl_msg.h"
#include "trajectory_msgs/JointTrajectory.h"

namespace controller_interface
{
/** \brief Initialize the kinematic chain for kinematics-based computation.
 *
 */
template<typename T>
bool CartesianVelocityControllerBase<T>::init(T *robot, ros::NodeHandle &n) {
  // KDL
  KinematicChainControllerBase<T>::init(robot, n);
  ik_vel_solver_.reset(new KDL::ChainIkSolverVel_pinv_givens(this->kdl_chain_));
  fk_vel_solver_.reset(new KDL::ChainFkSolverVel_recursive(this->kdl_chain_));
  fk_pos_solver_.reset(new KDL::ChainFkSolverPos_recursive(this->kdl_chain_));

  // get publishing period
  if (!n.getParam("publish_rate", publish_rate_)){
      ROS_ERROR("Parameter 'publish_rate' not set");
      return false;
  }

  // Publisher and Subscriber
  realtime_pub_.reset(new realtime_tools::RealtimePublisher<cartesian_state_msgs::PoseTwist>(n, "ee_state", 4));
  sub_command_ = n.subscribe("command_cart_vel", 5, &CartesianVelocityControllerBase<T>::command_cart_vel, this,ros::TransportHints().reliable().tcpNoDelay());
  vel_command_ = n.advertise<trajectory_msgs::JointTrajectory>("/ur5/ur_driver/joint_speed", 1);

  // Variable init
  this->joint_msr_.resize(this->kdl_chain_.getNrOfJoints());
  q_dt_cmd_.resize(this->kdl_chain_.getNrOfJoints());
  x_dt_des_ = KDL::Twist::Zero();
  x_.p.Zero();
  x_.M.Identity();
  x_dot_.p.Zero();
  x_dot_.M.Identity();

  ROS_INFO("Finished Cartesian Velocity Controller Base init");
  return true;
}

/** \brief This is called from within the realtime thread just before the
 * first call to \ref update
 *
 * \param time The current time
 */
template<typename T>
void CartesianVelocityControllerBase<T>::starting(const ros::Time& time){
  for(std::size_t i=0; i < this->joint_handles_.size(); i++) {
    q_dt_cmd_(i) = 0.0;
  }
  x_dt_des_ = KDL::Twist::Zero();
  last_publish_time_ = time;
  ROS_INFO("Finished Cartesian Velocity Controller Base Starting init");
}

/*!
 * \brief Issues commands to the joint. Should be called at regular intervals
 */
template<typename T>
void CartesianVelocityControllerBase<T>::update(const ros::Time& time, const ros::Duration& period) {

  // Get joint positions
  for(std::size_t i=0; i < this->joint_handles_.size(); i++)
  {
    this->joint_msr_.q(i)         = this->joint_handles_[i].getPosition();
    this->joint_msr_.qdot(i)      = this->joint_handles_[i].getVelocity();
  }

  // Compute inverse kinematics velocity solver
  ik_vel_solver_->CartToJnt(this->joint_msr_.q, x_dt_des_, q_dt_cmd_);
  writeVelocityCommands(period);

  // Forward kinematics
  fk_vel_solver_->JntToCart(this->joint_msr_, x_dot_);
  fk_pos_solver_->JntToCart(this->joint_msr_.q, x_);

  // Limit rate of publishing
  if (publish_rate_ > 0.0 && last_publish_time_+ ros::Duration(1.0/publish_rate_) < time)
  {
    // try to publish
    if (realtime_pub_->trylock()) {
      // we're actually publishing, so increment time
      last_publish_time_ = last_publish_time_ + ros::Duration(1.0/publish_rate_);

      // populate message
      realtime_pub_->msg_.header.stamp = time;
      tf::poseKDLToMsg(x_, realtime_pub_->msg_.pose);
      tf::twistKDLToMsg(x_dot_.GetTwist(), realtime_pub_->msg_.twist);

      realtime_pub_->unlockAndPublish();
    }
  }
}

/*!
 * \brief Subscriber's callback: copies twist commands
 */
template<typename T>
void CartesianVelocityControllerBase<T>::command_cart_vel( const geometry_msgs::TwistConstPtr &msg) {
    // std::cout<<"In command_cart_vel callback."<<std::endl;
    x_dt_des_.vel(0) = msg->linear.x;
    x_dt_des_.vel(1) = msg->linear.y;
    x_dt_des_.vel(2) = msg->linear.z;
    x_dt_des_.rot(0) = msg->angular.x;
    x_dt_des_.rot(1) = msg->angular.y;
    x_dt_des_.rot(2) = msg->angular.z;
    ROS_INFO("Finished Command Cart Vel Subscribe");
}


/********************************************/
/**FUNCTIONS OF INSTANCES OF THE BASE CLASS**/
/********************************************/

/** \brief write the desired velocity command in the hardware interface input
 * for a VelocityJointInterface
 * \param period The duration of an update cycle
 */
void CartesianVelocityController::writeVelocityCommands(
                                    const ros::Duration& period) {
    // std::cout<<"In writeVelocityCommands sim:=false."<<std::endl;
    for(std::size_t i=0; i < this->joint_handles_.size(); i++) {
      this->joint_handles_[i].setCommand(q_dt_cmd_(i));
    }
    //TODO: Add Joint Speed Control.
    trajectory_msgs::JointTrajectory traj;

    traj.header.frame_id = "base_link";
    traj.joint_names.resize(6);
    traj.points.resize(1);

    traj.points[0].positions.resize(6);
    traj.points[0].velocities.resize(6);

    traj.joint_names[0] = "shoulder_pan_joint";
    traj.joint_names[1] = "shoulder_lift_joint";
    traj.joint_names[2] = "elbow_joint";
    traj.joint_names[3] = "wrist_1_joint";
    traj.joint_names[4] = "wrist_2_joint";
    traj.joint_names[5] = "wrist_3_joint";

    traj.points[0].velocities[0] = q_dt_cmd_(0);
    traj.points[0].velocities[1] = q_dt_cmd_(1);
    traj.points[0].velocities[2] = q_dt_cmd_(2);
    traj.points[0].velocities[3] = q_dt_cmd_(3);
    traj.points[0].velocities[4] = q_dt_cmd_(4);
    traj.points[0].velocities[5] = q_dt_cmd_(5);

    traj.points[0].time_from_start = ros::Duration(1);

    vel_command_.publish(traj);
}

/** \brief write the desired velocity command in the hardware interface input
 * for a PosititionJointInterface
 * \param period The duration of an update cycle
 */
void CartesianVelocityControllerSim::writeVelocityCommands(
                                    const ros::Duration& period) {
  // std::cout<<"In writeVelocityCommands sim:=true."<<std::endl;
  for(std::size_t i=0; i < this->joint_handles_.size(); i++) {
    this->joint_handles_[i].setCommand(this->joint_msr_.q(i)
                                    + q_dt_cmd_(i)*period.toSec());
  }
}

} // controller_interface namespace

// Register controllers with the PLUGINLIB_EXPORT_CLASS macro to enable dynamic
// loading with the controller manager
PLUGINLIB_EXPORT_CLASS(controller_interface::CartesianVelocityController,
                       controller_interface::ControllerBase)
PLUGINLIB_EXPORT_CLASS(controller_interface::CartesianVelocityControllerSim,
                       controller_interface::ControllerBase)
