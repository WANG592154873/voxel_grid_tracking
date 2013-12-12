/*
 *  Copyright 2013 Néstor Morales Hernández <nestor@isaatc.ull.es>
 * 
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *      http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#include "voxelgridtracking.h"

#include <tf/transform_datatypes.h>
#include <tf/transform_listener.h>
#include <ros/ros.h>
#include <std_msgs/Float64.h>
#include <sensor_msgs/PointCloud2.h>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl_ros/point_cloud.h>

#include <opencv2/opencv.hpp>

#include <iostream>

using namespace std;

namespace voxel_grid_tracking {
    
VoxelGridTracking::VoxelGridTracking()
{
    m_pointCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    
    ros::NodeHandle nh("~");
    m_deltaTimeSub = nh.subscribe<std_msgs::Float64>("deltaTime", 1, boost::bind(&VoxelGridTracking::deltaTimeCallback, this, _1));    
    m_pointCloudSub = nh.subscribe<sensor_msgs::PointCloud2>("pointCloud", 1, boost::bind(&VoxelGridTracking::pointCloudCallback, this, _1));
    
}

void VoxelGridTracking::start()
{

    tf::StampedTransform lastMapOdomTransform;
    lastMapOdomTransform.stamp_ = ros::Time(-1);
    
    tf::TransformListener listener;
    tf::StampedTransform transform;
    ros::Rate rate(10.0);
    while (ros::ok()) {
        try{
            while (! listener.waitForTransform ("/map", "/odom", ros::Time(0), ros::Duration(10.0), ros::Duration(0.01)));
            
            listener.lookupTransform("/map", "/odom", ros::Time(0), transform);
            if (lastMapOdomTransform.stamp_ != ros::Time(-1)) {
                if (lastMapOdomTransform.stamp_ != transform.stamp_) {
                    double yaw, yaw1, yaw2, pitch, roll;
                    tf::Matrix3x3(transform.getRotation()).getRPY(roll, pitch, yaw2);
                    tf::Matrix3x3(lastMapOdomTransform.getRotation()).getRPY(roll, pitch, yaw1);
                    yaw = yaw2 - yaw1;
                    
                    double deltaX = lastMapOdomTransform.getOrigin().getX() - transform.getOrigin().getX();
                    double deltaY = lastMapOdomTransform.getOrigin().getY() - transform.getOrigin().getY();
                    
                    double deltaS = sqrt(deltaX * deltaX + deltaY * deltaY);
                    double speed = 0.0;
                    if (deltaS != 0.0) {
                        speed = deltaS / m_deltaTime;
                    }
                    
                    cout << "Transformation Received!!!!" << endl;
                    setDeltaYawSpeedAndTime(yaw, speed, m_deltaTime);
                    
                    pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
                    pcl::copyPointCloud(*m_pointCloud, *pointCloud);
                    
                    compute(pointCloud);
                }
            }
            lastMapOdomTransform = transform;
            ros::spinOnce();
        } catch (tf::TransformException ex){
            ROS_ERROR("%s",ex.what());
        }
        
        rate.sleep();
    }
    
    ros::spin();
}
void VoxelGridTracking::deltaTimeCallback(const std_msgs::Float64::ConstPtr& msg)
{
    m_deltaTime = msg.get()->data;
}

void VoxelGridTracking::pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& msg) 
{
    pcl::fromROSMsg<pcl::PointXYZRGB>(*msg, *m_pointCloud);
}

void VoxelGridTracking::setDeltaYawSpeedAndTime(const double& deltaYaw, const double& deltaSpeed, const double& deltaTime)
{
    m_deltaYaw = deltaYaw;
    m_deltaSpeed = deltaSpeed;
    m_deltaTime = deltaTime;
}

void VoxelGridTracking::compute(const pcl::PointCloud< pcl::PointXYZRGB >::Ptr& pointCloud)
{
    cout << __FUNCTION__ << endl;
//     getBinaryMapFromPointCloud(pointCloud);
//     
//     getMeasurementModel();
//     
//     if (m_initialized) {
//         prediction();
//         measurementBasedUpdate();
//         reconstructObjects(pointCloud);
//     } 
//     publishParticles(m_oldParticlesPub, 2.0);
//     
//     initialization();
//     
//     publishAll(pointCloud);
}

}