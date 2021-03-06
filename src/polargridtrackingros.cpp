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


#include "polargridtrackingros.h"

#include <pcl-1.7/pcl/point_cloud.h>
#include<pcl_conversions/pcl_conversions.h>
#include "sensor_msgs/PointCloud2.h"
#include "nav_msgs/GridCells.h"
#include "nav_msgs/OccupancyGrid.h"
#include <visualization_msgs/Marker.h>
#include "geometry_msgs/PoseArray.h"
#include <visualization_msgs/MarkerArray.h>
#include <tf/transform_datatypes.h>
#include "tf/transform_listener.h"
// #include "tf2_ros/buffer.h"

#include "utilspolargridtracking.h"
#include "voxel_grid_tracking/roiArray.h"
#include "voxel_grid_tracking/voxel_tracker_time_stats.h"

// #include "utils.h"

#define FRAME_ID "left_cam"

#include <boost/foreach.hpp>

namespace polar_grid_tracking {
    
polar_grid_trackingROS::polar_grid_trackingROS(const uint32_t& rows, const uint32_t& cols, const double& cellSizeX, 
                                           const double& cellSizeZ, const double& maxVelX, const double& maxVelZ, 
                                           const t_Camera_params& cameraParams, const double& particlesPerCell, 
                                           const double& threshProbForCreation, 
                                           const double & gridDepthFactor, const uint32_t &  gridColumnFactor, const double & yawInterval,
                                           const double & threshYaw, const double & threshMagnitude): 
                                            polar_grid_tracking(rows, cols, cellSizeX, cellSizeZ, maxVelX, maxVelZ, cameraParams, 
                                                              particlesPerCell, threshProbForCreation, 
                                                              gridDepthFactor, gridColumnFactor, yawInterval,
                                                              threshYaw, threshMagnitude)
{
    // TODO: Get from params
//     m_cameraParams.minX = 0.0;
//     m_cameraParams.minY = 0.0;
//     m_cameraParams.width = 1244;
//     m_cameraParams.height = 370;
//     m_cameraParams.u0 = 604.081;
//     m_cameraParams.v0 = 180.507;
//     m_cameraParams.ku = 707.049;
//     m_cameraParams.kv = 707.049;
//     m_cameraParams.distortion = 0;
//     m_cameraParams.baseline = 0.472539;
//     m_cameraParams.R = Eigen::MatrixXd(3, 3);
//     m_cameraParams.R << 0.999984, -0.00501274, -0.00271074,
//     0.00500201, 0.99998, -0.00395038,
//     0.00273049, 0.00393676, 0.999988;
//     m_cameraParams.t = Eigen::MatrixXd(3, 1);
//     m_cameraParams.t << 0.0598969, -1.00137, 0.00463762;
   
//     m_pointCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    
    ros::NodeHandle nh("~");
    
    // Reading params
    nh.param<string>("map_frame", m_mapFrame, "/map");
    nh.param<string>("pose_frame", m_poseFrame, "/base_footprint");
    nh.param<string>("camera_frame", m_cameraFrame, "/base_left_cam");
    
    // Publish / Subscribe
    // Topics
    std::string left_info_topic = "left/camera_info";
    std::string right_info_topic = "right/camera_info";
    
    m_leftCameraInfoSub.subscribe(nh, left_info_topic, 1);
    m_rightCameraInfoSub.subscribe(nh, right_info_topic, 1);
    m_pointCloudSub.subscribe(nh, "inputPointCloud", 10);
    
    bool approx;
    int queue_size;
    nh.param("approximate_sync", approx, true);
    nh.param("queue_size", queue_size, 10);
    
    if (approx)
    {
        m_approxSynchronizer.reset( new ApproximateSync(ApproximatePolicy(queue_size),
                                                        m_pointCloudSub, 
                                                        m_leftCameraInfoSub, 
                                                        m_rightCameraInfoSub) );
        m_approxSynchronizer->registerCallback(boost::bind(&polar_grid_trackingROS::pointCloudCallback, 
                                                           this, _1, _2, _3));
    }
    else
    {
        m_exactSynchronizer.reset( new ExactSync(ExactPolicy(queue_size),
                                                 m_pointCloudSub,
                                                 m_leftCameraInfoSub, 
                                                 m_rightCameraInfoSub) );
        m_exactSynchronizer->registerCallback(boost::bind(&polar_grid_trackingROS::pointCloudCallback, 
                                                          this, _1, _2, _3));
    }
//     m_pointCloudSub = nh.subscribe<sensor_msgs::PointCloud2>("pointCloudStereo", 0, boost::bind(&polar_grid_trackingROS::pointCloudCallback, this, _1));

    m_pointCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
    m_pointCloudPub = nh.advertise<sensor_msgs::PointCloud2> ("pointCloud", 1);
    m_extendedPointCloudPub = nh.advertise<sensor_msgs::PointCloud2> ("extendedPointCloud", 1);
    m_extendedPointCloudOrientationPub = nh.advertise<geometry_msgs::PoseArray> ("pointCloudOrientation", 1);
    m_binaryMapPub = nh.advertise<nav_msgs::GridCells> ("binaryMap", 1);
    m_particlesPub = nh.advertise<geometry_msgs::PoseArray> ("particles", 1);
    m_oldParticlesPub = nh.advertise<geometry_msgs::PoseArray> ("oldParticles", 1);
    m_colAvgPub = nh.advertise<geometry_msgs::PoseArray> ("colAvg", 1);
    m_polarGridPub = nh.advertise<visualization_msgs::Marker>("polarGrid", 10);
    m_polarCellYawPub = nh.advertise<geometry_msgs::PoseArray> ("polarCellYaw", 1);
    m_obstaclesPub = nh.advertise<visualization_msgs::MarkerArray>("obstacles", 10);
    m_roiPub = nh.advertise<visualization_msgs::MarkerArray>("obstaclesROI", 10);
    m_pointCloudInObstaclePub = nh.advertise<sensor_msgs::PointCloud2> ("pointCloudInObstacle", 1);
    m_ROIPub = nh.advertise<voxel_grid_tracking::roiArray>("result_rois", 1);
    m_timeStatsPub = nh.advertise<voxel_grid_tracking::voxel_tracker_time_stats>("time_stats", 1);

    m_lastMapOdomTransform.stamp_ = ros::Time(-1);
}

void polar_grid_trackingROS::start()
{
    tf::StampedTransform lastMapOdomTransform;
    lastMapOdomTransform.stamp_ = ros::Time(-1);
    
    tf::TransformListener listener;
    tf::StampedTransform transform;
    while (ros::ok()) {
//        ros::spinOnce();
        try{
            while ((! listener.waitForTransform ("/map", "/old_left_cam", ros::Time(0), ros::Duration(10.0), ros::Duration(0.000001))) && (ros::ok()));
            
            listener.lookupTransform("/map", "/old_left_cam", ros::Time(0), transform);
            if (lastMapOdomTransform.stamp_ != ros::Time(-1)) {
                if (lastMapOdomTransform.stamp_ != transform.stamp_) {
                    double yaw, yaw1, yaw2, pitch, pitch1, pitch2, roll;
                    tf::Matrix3x3(transform.getRotation()).getRPY(roll, pitch2, yaw2);
                    tf::Matrix3x3(lastMapOdomTransform.getRotation()).getRPY(roll, pitch1, yaw1);
                    yaw = yaw2 - yaw1;
                    pitch = pitch2 - pitch1;
                    
                    double deltaX = lastMapOdomTransform.getOrigin().getX() - transform.getOrigin().getX();
                    double deltaY = lastMapOdomTransform.getOrigin().getY() - transform.getOrigin().getY();
                    double deltaZ = lastMapOdomTransform.getOrigin().getZ() - transform.getOrigin().getZ();
                    
//                     double deltaX = 0, deltaY = 0, deltaZ = 0;
                    
                    m_deltaTime = transform.stamp_.toSec() - lastMapOdomTransform.stamp_.toSec();
                    
                    double deltaS = sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
                    double speed = 0.0;
                    if (deltaS != 0.0) {
                        speed = deltaS / m_deltaTime;
                    }
                    
                    cout << "Transformation Received!!!!" << endl;
                    cout << "yaw " << yaw << endl;
                    cout << "yaw1 " << yaw1 << endl;
                    cout << "yaw2 " << yaw2 << endl;
                    cout << "pitch " << pitch << endl;
                    cout << "deltaX " << deltaX << endl;
                    cout << "deltaY " << deltaY << endl;
                    cout << "deltaS " << deltaS << endl;
                    cout << "lastMapOdomTransform " << cv::Point3d(lastMapOdomTransform.getOrigin().getX(), 
                                                                   lastMapOdomTransform.getOrigin().getY(),
                                                                   lastMapOdomTransform.getOrigin().getZ()) << endl;
                    cout << "transform " << cv::Point3d(transform.getOrigin().getX(), 
                                                        transform.getOrigin().getY(),
                                                        transform.getOrigin().getZ()) << endl;
                    cout << "speed " << speed << endl;
                    cout << "m_deltaTime " << m_deltaTime << endl;

                    setDeltaYawSpeedAndTime(yaw, speed, m_deltaTime);
                    
                    pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
                    pcl::copyPointCloud(*m_pointCloud, *pointCloud);
                    
                    compute(pointCloud);
                }
            }
            lastMapOdomTransform = transform;
//             ros::spinOnce();
        } catch (tf::TransformException ex){
            ROS_ERROR("%s",ex.what());
        }
        
    }
    
    
    //     m_pointCloud.reset(new pcl::PointCloud<pcl::PointXYZRGB>);
//     tf::StampedTransform lastMapOdomTransform;
//     lastMapOdomTransform.stamp_ = ros::Time(-1);
//     
//     tf::TransformListener listener;
//     tf::StampedTransform transform;
//     ros::Rate rate(10.0);
//     while (ros::ok()) {
//         try{
//             while (! listener.waitForTransform ("/map", "/odom", ros::Time(0), ros::Duration(10.0), ros::Duration(0.01)));
//             
//             listener.lookupTransform("/map", "/odom", ros::Time(0), transform);
//             if (lastMapOdomTransform.stamp_ != ros::Time(-1)) {
//                 if (lastMapOdomTransform.stamp_ != transform.stamp_) {
//                     // FIXME: Get the time increment from the timestamp, not from the Z
//                     const double deltaTime = transform.getOrigin().getZ() - lastMapOdomTransform.getOrigin().getZ();
//                     
//                     double yaw, yaw1, yaw2, pitch, roll;
//                     tf::Matrix3x3(transform.getRotation()).getRPY(roll, pitch, yaw2);
//                     tf::Matrix3x3(lastMapOdomTransform.getRotation()).getRPY(roll, pitch, yaw1);
//                     yaw = yaw2 - yaw1;
//                     
//                     double deltaX = transform.getOrigin().getX() - lastMapOdomTransform.getOrigin().getX();
//                     double deltaY = transform.getOrigin().getY() - lastMapOdomTransform.getOrigin().getY();
//                     
//                     double deltaS = sqrt(deltaX * deltaX + deltaY * deltaY);
//                     double speed = 0.0;
//                     if (deltaS != 0.0) {
//                         speed = deltaS / deltaTime;
//                     }
//                     
//                     setDeltaYawSpeedAndTime(yaw, speed, deltaTime);
//                     
//                     pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
//                     pcl::copyPointCloud(*m_pointCloud, *pointCloud);
//                     
//                     compute(pointCloud);
//                 }
//             }
//             lastMapOdomTransform = transform;
//         } catch (tf::TransformException ex){
//             ROS_ERROR("%s",ex.what());
//         }
//         
//         
//         
//         rate.sleep();
//     }
//     
//     ros::spin();
}

/**
 * DEPRECATED
 */
void polar_grid_trackingROS::pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& msg) 
{
    m_currentId = msg->header.seq;
//     pcl::fromROSMsg<pcl::PointXYZRGB>(*msg, *m_pointCloud);
//     cout << "Received point cloud " << msg->header.seq << " with size = " << m_pointCloud->size() << endl;
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmpPointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    pcl::fromROSMsg<pcl::PointXYZRGB>(*msg, *tmpPointCloud);
    cout << "Received point cloud " << msg->header.seq << " with size = " << tmpPointCloud->size() << endl;
    
    m_pointCloud->clear();
    
    for (pcl::PointCloud<pcl::PointXYZRGB>::const_iterator it = tmpPointCloud->begin(); 
         it != tmpPointCloud->end(); it++) {
        
        const pcl::PointXYZRGB & point = *it;
        pcl::PointXYZRGB newPoint;
        
        newPoint.x = point.x - m_cellSizeX / 2.0;
        newPoint.y = (point.z - m_cellSizeZ / 2.0);
        newPoint.z = point.y;
        newPoint.r = point.r;
        newPoint.g = point.g;
        newPoint.b = point.b;
        
        m_pointCloud->push_back(newPoint);
    }
}

void polar_grid_trackingROS::pointCloudCallback(const sensor_msgs::PointCloud2::ConstPtr& msgPointCloud,
                                           const sensor_msgs::CameraInfoConstPtr& leftCameraInfo, 
                                           const sensor_msgs::CameraInfoConstPtr& rightCameraInfo) 
{
//     m_cameraFrame = msgPointCloud->header.frame_id;
//     try {
//         m_tfListener.lookupTransform(m_mapFrame, m_poseFrame, ros::Time(0), m_pose2MapTransform);
//         m_tfListener.lookupTransform(m_cameraFrame, m_mapFrame, ros::Time(0), m_map2CamTransform);
//     } catch (tf::TransformException ex){
//         ROS_ERROR("%s",ex.what());
//     }
//     
    pcl::fromROSMsg<pcl::PointXYZRGB>(*msgPointCloud, *m_pointCloud);
//     
//     if (m_pointCloud->size() != 0) {
//         
//         m_deltaTime = (msgPointCloud->header.stamp - m_lastPointCloudTime).toSec();
//         
//         m_lastPointCloudTime = msgPointCloud->header.stamp;
//         
//         m_stereoCameraModel.fromCameraInfo(*leftCameraInfo, *rightCameraInfo);
//         m_currentId = leftCameraInfo->header.seq;
//         
//         compute(m_pointCloud);
//     }
    
    tf::StampedTransform transform;
    try{
        m_cameraFrame = msgPointCloud->header.frame_id;
//         while ((! m_tfListener.waitForTransform (m_mapFrame, m_cameraFrame, ros::Time(0), ros::Duration(10.0), ros::Duration(0.000001))) && (ros::ok()));
        
        m_tfListener.lookupTransform(m_mapFrame, m_cameraFrame, ros::Time(0), transform);
        if (m_lastMapOdomTransform.stamp_ != ros::Time(-1)) {
            if (m_lastMapOdomTransform.stamp_ != transform.stamp_) {
                double yaw, yaw1, yaw2, pitch, pitch1, pitch2, roll;
                tf::Matrix3x3(transform.getRotation()).getRPY(roll, pitch2, yaw2);
                tf::Matrix3x3(m_lastMapOdomTransform.getRotation()).getRPY(roll, pitch1, yaw1);
                yaw = yaw2 - yaw1;
                pitch = pitch2 - pitch1;
                
                double deltaX = m_lastMapOdomTransform.getOrigin().getX() - transform.getOrigin().getX();
                double deltaY = m_lastMapOdomTransform.getOrigin().getY() - transform.getOrigin().getY();
                double deltaZ = m_lastMapOdomTransform.getOrigin().getZ() - transform.getOrigin().getZ();
                
                m_deltaTime = transform.stamp_.toSec() - m_lastMapOdomTransform.stamp_.toSec();
                
                double deltaS = sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
                double speed = 0.0;
                if (deltaS != 0.0) {
                    speed = deltaS / m_deltaTime;
                }
                cout << "Transformation Received!!!!" << endl;
                cout << "yaw " << yaw << endl;
                cout << "yaw1 " << yaw1 << endl;
                cout << "yaw2 " << yaw2 << endl;
                cout << "pitch " << pitch << endl;
                cout << "deltaX " << deltaX << endl;
                cout << "deltaY " << deltaY << endl;
                cout << "deltaS " << deltaS << endl;
                cout << "lastMapOdomTransform " << cv::Point3d(m_lastMapOdomTransform.getOrigin().getX(), 
                                                               m_lastMapOdomTransform.getOrigin().getY(),
                                                               m_lastMapOdomTransform.getOrigin().getZ()) << endl;
                                                        
                cout << "transform " << cv::Point3d(transform.getOrigin().getX(), 
                                                    transform.getOrigin().getY(),
                                                    transform.getOrigin().getZ()) << endl;
                cout << "speed " << speed << endl;
                cout << "m_deltaTime " << m_deltaTime << endl;
                
                setDeltaYawSpeedAndTime(yaw, speed, m_deltaTime);
                
                pcl::PointCloud<pcl::PointXYZRGB>::Ptr pointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
                pcl::copyPointCloud(*m_pointCloud, *pointCloud);
                
                m_stereoCameraModel.fromCameraInfo(*leftCameraInfo, *rightCameraInfo);
                m_currentId = leftCameraInfo->header.seq;
                
                compute(pointCloud);
            }
        }
        m_lastMapOdomTransform = transform;
        //             ros::spinOnce();
    } catch (tf::TransformException ex){
        ROS_ERROR("%s",ex.what());
    }
}

void polar_grid_trackingROS::compute(const pcl::PointCloud< pcl::PointXYZRGB >::Ptr& pointCloud)
{
    voxel_grid_tracking::voxel_tracker_time_stats timeStatsMsg;
    timeStatsMsg.header.seq = m_currentId;
    timeStatsMsg.header.stamp = ros::Time::now();
    
    INIT_CLOCK(startCompute)
    
//     polar_grid_tracking::compute(pointCloud);
    getBinaryMapFromPointCloud(pointCloud);
//     drawBinaryMap(map);
//     drawTopDownMap(pointCloud);
    
    getMeasurementModel();
// 
    if (m_initialized) {
        prediction();
        measurementBasedUpdate();
        reconstructObjects(pointCloud);
        
//         publishPolarGrid();
//         publishPointCloud(extendedPointCloud);
//         publishPointCloudOrientation(extendedPointCloud);
//         publishPolarCellYaw(1.0);
//         clearObstaclesAndROIs();
//         publishObstacles();
//         publishROIs();
//         publishPointCloudInObstacles(extendedPointCloud);
//     
// //         visualizeROI2d();
    } 
//     publishParticles(m_oldParticlesPub, 2.0);
// 
    initialization();
    END_CLOCK(totalCompute, startCompute)
    
    ROS_INFO("[%s] Total time: %f seconds", __FUNCTION__, totalCompute);
    timeStatsMsg.totalCompute = totalCompute;
    m_timeStatsPub.publish(timeStatsMsg);
    
    if (m_initialized)
        publishParticles(m_oldParticlesPub, 2.0);
//     publishAll(pointCloud);
    publishRoiArrays();
}

void polar_grid_trackingROS::publishAll(const pcl::PointCloud< pcl::PointXYZRGB >::Ptr& pointCloud)
{
    publishPointCloud(pointCloud);
    publishBinaryMap();
    publishParticles(m_particlesPub, 1.0);
    publishColumnAverage(0.5);
}

void polar_grid_trackingROS::publishPointCloud(const pcl::PointCloud< pcl::PointXYZRGB >::Ptr& pointCloud)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmpPointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);
    
    for (pcl::PointCloud<pcl::PointXYZRGB>::const_iterator it = pointCloud->begin(); 
        it != pointCloud->end(); it++) {
        
        const pcl::PointXYZRGB & point = *it;
        pcl::PointXYZRGB newPoint;
    
        newPoint.x = point.x - m_cellSizeX / 2.0;
        newPoint.y = point.z - m_cellSizeZ / 2.0;
        newPoint.z = -point.y;
        newPoint.r = point.r;
        newPoint.g = point.g;
        newPoint.b = point.b;
    
        tmpPointCloud->push_back(newPoint);
    }

    
    sensor_msgs::PointCloud2 cloudMsg;
    pcl::toROSMsg (*tmpPointCloud, cloudMsg);
    cloudMsg.header.frame_id=FRAME_ID;
    cloudMsg.header.stamp = ros::Time();
    
    m_pointCloudPub.publish(cloudMsg);
    
    ros::spinOnce();
}


void polar_grid_trackingROS::publishPointCloud(const pcl::PointCloud< PointXYZRGBDirected >::Ptr & pointCloud)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmpPointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);

    for (pcl::PointCloud<PointXYZRGBDirected>::const_iterator it = pointCloud->begin();
            it != pointCloud->end(); it++) {

        const PointXYZRGBDirected & point = *it;
        pcl::PointXYZRGB newPoint;

        newPoint.x = point.x - m_cellSizeX / 2.0;
        newPoint.y = point.z - m_cellSizeZ / 2.0;
        newPoint.z = point.y;
        newPoint.r = point.r;
        newPoint.g = point.g;
        newPoint.b = point.b;

        tmpPointCloud->push_back(newPoint);
    }


    sensor_msgs::PointCloud2 cloudMsg;
    pcl::toROSMsg (*tmpPointCloud, cloudMsg);
    cloudMsg.header.frame_id=FRAME_ID;
    cloudMsg.header.stamp = ros::Time();

    m_extendedPointCloudPub.publish(cloudMsg);
    
    ros::spinOnce();
}

void polar_grid_trackingROS::publishPointCloudOrientation(const pcl::PointCloud< PointXYZRGBDirected >::Ptr & pointCloud)
{
//     ros::Publisher & particlesPub, const double & zPlane
    geometry_msgs::PoseArray poses;
    
    poses.header.frame_id = FRAME_ID;
    poses.header.stamp = ros::Time();
    
    const double offsetX = ((m_grid.cols() + 1) * m_cellSizeX) / 2.0;
    const double offsetZ = m_cellSizeZ / 2.0;
    
    for (pcl::PointCloud<PointXYZRGBDirected>::const_iterator it = pointCloud->begin();
         it != pointCloud->end(); it++) {
        
        const PointXYZRGBDirected & point = *it;
        geometry_msgs::Pose pose;
        
        pose.position.x = point.x - m_cellSizeX / 2.0;
        pose.position.y = point.z - m_cellSizeZ / 2.0;
        pose.position.z = point.y;
    
        const tf::Quaternion & quat = tf::createQuaternionFromRPY(0.0, 0.0, point.yaw);
        pose.orientation.w = quat.w();
        pose.orientation.x = quat.x();
        pose.orientation.y = quat.y();
        pose.orientation.z = quat.z();
        
        poses.poses.push_back(pose);
    }
    
    m_extendedPointCloudOrientationPub.publish(poses);
    
    ros::spinOnce();
}



void polar_grid_trackingROS::publishBinaryMap()
{
    nav_msgs::GridCells gridCells;
    gridCells.header.frame_id = FRAME_ID;
    gridCells.header.stamp = ros::Time();
    gridCells.cell_width = m_cellSizeX;
    gridCells.cell_height = m_cellSizeZ;

    const double maxZ = m_grid.rows() * m_cellSizeZ;
    const double maxX = m_grid.cols() / 2.0 * m_cellSizeX;
    const double minX = -maxX;
    
    const double factorX = m_grid.cols() / (maxX - minX);
    const double factorZ = m_grid.rows() / maxZ;
    
    const double offsetX = (m_grid.cols() * m_cellSizeX) / 2.0;
    
    uint32_t cellIdx = 0;
    for (uint32_t i = 0; i < m_map.rows(); i++) {
        for (int j = 0; j < m_map.cols(); j++) {
            if (m_map(i, j)) {
                geometry_msgs::Point point;
                point.x = j * m_cellSizeX - offsetX;
                point.y = i * m_cellSizeZ;
                point.z = 0;
                gridCells.cells.push_back(point);
            }
        }
    }
    
    m_binaryMapPub.publish(gridCells);
}

void polar_grid_trackingROS::publishParticles(ros::Publisher & particlesPub, const double & zPlane)
{
    geometry_msgs::PoseArray particles;
    
    particles.header.frame_id = FRAME_ID;
    particles.header.stamp = ros::Time();
    
    const double offsetX = ((m_grid.cols() + 1) * m_cellSizeX) / 2.0;
    const double offsetZ = m_cellSizeZ / 2.0;
    
    for (uint32_t z = 0; z < m_grid.rows(); z++) {
        for (uint32_t x = 0; x < m_grid.cols(); x++) {
            const Cell & cell = m_grid(z, x);
            for (uint32_t i = 0; i < cell.numParticles(); i++) {
                const Particle & particle = cell.getParticle(i);
                geometry_msgs::Pose pose;
                
                pose.position.x = particle.x() - offsetX;
                pose.position.y = particle.z() - offsetZ;
                pose.position.z = zPlane;
                
                const double & vx = particle.vx();
                const double & vz = particle.vz();
                                
                const double & norm = sqrt(vx * vx + vz * vz);
                
                double yaw = acos(vx / norm);
                if (vz < 0)
                    yaw = -yaw;
                
                const tf::Quaternion & quat = tf::createQuaternionFromRPY(0.0, 0.0, yaw);
                pose.orientation.w = quat.w();
                pose.orientation.x = quat.x();
                pose.orientation.y = quat.y();
                pose.orientation.z = quat.z();
                
                particles.poses.push_back(pose);
            }
        }
    }
    
    particlesPub.publish(particles);
    
//    ros::spinOnce();
}

void polar_grid_trackingROS::publishColumnAverage(const double & zPlane)
{
    geometry_msgs::PoseArray colAverages;
    
    colAverages.header.frame_id = FRAME_ID;
    colAverages.header.stamp = ros::Time();
    
    const double offsetX = (m_grid.cols() * m_cellSizeX) / 2.0;
    
    for (uint32_t x = 0; x < m_grid.cols(); x++) {
        for (uint32_t z = 0; z < m_grid.rows(); z++) {
            if (m_map(z, x)) {
                
                geometry_msgs::Pose pose;
                
                pose.position.x = x * m_cellSizeX - offsetX;
                pose.position.y = z * m_cellSizeZ;
                pose.position.z = zPlane;
                
                double vx, vz;
                m_grid(z, x).getMainVectors(vx, vz);
                const vector<Particle> & particles = m_grid(z, x).getParticles();
                if (particles.size() != 0) {
                    
                    const double & norm = sqrt(vx * vx + vz * vz);
                    
                    if (norm == 0.0) continue;
                    
                    double yaw = acos(vx / norm);
                    if (vz < 0)
                        yaw = -yaw;
                    
                    const tf::Quaternion & quat = tf::createQuaternionFromRPY(0.0, 0.0, yaw);
                    pose.orientation.w = quat.w();
                    pose.orientation.x = quat.x();
                    pose.orientation.y = quat.y();
                    pose.orientation.z = quat.z();
                                        
                    colAverages.poses.push_back(pose);
                }
                
                break;
            }
        }
    }
    
    m_colAvgPub.publish(colAverages);
    
//    ros::spinOnce();
}

void polar_grid_trackingROS::reconstructObjects(const pcl::PointCloud< pcl::PointXYZRGB >::Ptr& pointCloud)
{   
    pcl::PointCloud< PointXYZRGBDirected >::Ptr extendedPointCloud;
    resetPolarGrid();
    extendPointCloud(pointCloud, extendedPointCloud);
    
    generateObstacles();
}

void polar_grid_trackingROS::publishPolarGrid()
{
    visualization_msgs::Marker line_list;
    line_list.header.frame_id = FRAME_ID;
    line_list.header.stamp = ros::Time();
//     line_list.ns = "points_and_lines";
    line_list.action = visualization_msgs::Marker::ADD;
    line_list.pose.orientation.w = 1.0;
    
    line_list.id = 0;
    line_list.type = visualization_msgs::Marker::LINE_LIST;
    
    line_list.scale.x = 0.01;
    
    // Line list is red
    line_list.color.r = 1.0;
    line_list.color.a = 1.0;
    
    // Create the vertices for the points and lines
    double z = (double)(m_cameraParams.ku * m_cameraParams.baseline) / m_cameraParams.width;
    
    const double maxDepth = m_grid.rows() * m_cellSizeZ * (m_gridDepthFactor + 1.0);
    const double fb = m_cameraParams.ku * m_cameraParams.baseline;
    const double maxCol = m_cameraParams.u0 - m_cameraParams.width - 1;
    while (z <= maxDepth) {
        const double d = fb / z;
        const double xMin = m_cameraParams.baseline * m_cameraParams.u0 / d;
        const double xMax = m_cameraParams.baseline * maxCol / d;
        
        geometry_msgs::Point p1, p2;
        p1.x = xMin;
        p2.x = xMax;
        p1.y = p2.y = z;
        p1.z = p2.z = 0;
        
        line_list.points.push_back(p1);
        line_list.points.push_back(p2);
        
        z *= 1.0 + m_gridDepthFactor;
    }
    
    z /= 1.0 + m_gridDepthFactor;
    
    const double dMin = fb / z;
    for (uint32_t i = 0; i < m_cameraParams.width - 1; i += m_gridColumnFactor) {
        const double x = m_cameraParams.baseline * (i - m_cameraParams.u0) / dMin;
        
        geometry_msgs::Point p1, p2;
        p1.x = x;
        p1.y = z;
        p1.z = 0;
        
        p2.x = p2.y = p2.z = 0;
        
        line_list.points.push_back(p1);
        line_list.points.push_back(p2);
    }
    
    m_polarGridPub.publish(line_list);
    
//    ros::spinOnce();
    
}

void polar_grid_trackingROS::publishPolarCellYaw(const double & zPlane)
{
    geometry_msgs::PoseArray polarCellYaw;
    
    polarCellYaw.header.frame_id = FRAME_ID;
    polarCellYaw.header.stamp = ros::Time();
    
    const double z0 = (double)(m_cameraParams.ku * m_cameraParams.baseline) / m_cameraParams.width;
    const double fb = m_cameraParams.ku * m_cameraParams.baseline;
    
    for (uint32_t r = 0; r < m_polarGrid.rows(); r++) {
        for (uint32_t c = 0; c < m_polarGrid.cols(); c++) {
            if (m_polarGrid(r, c).getNumVectors() != 0) {
                
                const int32_t & obstIdx = m_polarGrid(r, c).obstIdx();
                if (obstIdx == -1) continue;
                if (! m_obstacles[obstIdx].isValid()) continue;
                
                geometry_msgs::Pose pose;
                
                pose.position.y = z0 * pow(1.0 + m_gridDepthFactor, r + 0.5);
                const double dMin = fb / pose.position.y;
                const double u = m_gridColumnFactor * (c + 1.5);
                pose.position.x = pose.position.y * (u - m_cameraParams.u0) / m_cameraParams.ku;
                pose.position.z = zPlane;
                
                const double yaw = m_polarGrid(r, c).getYaw();
                    
                const tf::Quaternion & quat = tf::createQuaternionFromRPY(0.0, 0.0, yaw);
                pose.orientation.w = quat.w();
                pose.orientation.x = quat.x();
                pose.orientation.y = quat.y();
                pose.orientation.z = quat.z();
                
                polarCellYaw.poses.push_back(pose);
            }
        }
    }
    
    m_polarCellYawPub.publish(polarCellYaw);
    
//    ros::spinOnce();
}

void polar_grid_trackingROS::publishObstacles()
{
    visualization_msgs::MarkerArray obstacles;
    
    const double z0 = (double)(m_cameraParams.ku * m_cameraParams.baseline) / m_cameraParams.width;
    const double fb = m_cameraParams.ku * m_cameraParams.baseline;
    
    for (uint32_t i = 0; i < m_obstacles.size(); i++) {
        if (! m_obstacles[i].isValid())
            continue;
        
        const vector<PolarCell> & cells = m_obstacles[i].cells();
//         if (cells.size() <= 1)
//             continue;
        
        visualization_msgs::Marker triangles;
        
        triangles.header.frame_id = FRAME_ID;
        triangles.header.stamp = ros::Time();
        triangles.action = visualization_msgs::Marker::ADD;
        triangles.pose.orientation.w = 1.0;
        
        triangles.id = i;
        triangles.type = visualization_msgs::Marker::TRIANGLE_LIST;
        
        triangles.scale.x = 1.0;
        triangles.scale.y = 1.0;
        triangles.scale.z = 1.0;
        
        triangles.color.r = (double)rand() / RAND_MAX;
        triangles.color.g = (double)rand() / RAND_MAX;
        triangles.color.b = (double)rand() / RAND_MAX;
        
//         triangles.lifetime = ros::Duration(10.0);
        
        if (cells.size() > 1)
            triangles.color.a = 1.0;
        else
            triangles.color.a = 0.5;
                
        BOOST_FOREACH(const PolarCell & cell, cells) {
        
            geometry_msgs::Point p1, p2, p3, p4;
            
            const double row = cell.row();
            const double col = cell.col() + 1.0;

            p1.y = z0 * pow(1.0 + m_gridDepthFactor, row);
            const double u1 = m_gridColumnFactor * col;
            p1.x = p1.y * (u1 - m_cameraParams.u0) / m_cameraParams.ku;
            p1.z = 0.0;

            p2.y = z0 * pow(1.0 + m_gridDepthFactor, row + 1.0);
            const double u2 = m_gridColumnFactor * col;
            p2.x = p2.y * (u2 - m_cameraParams.u0) / m_cameraParams.ku;
            p2.z = 0.0;
            
            p3.y = z0 * pow(1.0 + m_gridDepthFactor, row);
            const double u3 = m_gridColumnFactor * (col + 1.0);
            p3.x = p3.y * (u3 - m_cameraParams.u0) / m_cameraParams.ku;
            p3.z = 0.0;
            
            p4.y = z0 * pow(1.0 + m_gridDepthFactor, row + 1.0);
            const double u4 = m_gridColumnFactor * (col + 1.0);
            p4.x = p4.y * (u4 - m_cameraParams.u0) / m_cameraParams.ku;
            p4.z = 0.0;
            
            triangles.points.push_back(p1);
            triangles.points.push_back(p2);
            triangles.points.push_back(p3);
            
            triangles.points.push_back(p4);
            triangles.points.push_back(p2);
            triangles.points.push_back(p3);
        }
        
        obstacles.markers.push_back(triangles);
    }
    
    m_obstaclesPub.publish(obstacles);
    
//    ros::spinOnce();
}

void polar_grid_trackingROS::publishROIs()
{
     visualization_msgs::MarkerArray obstaclesROI;
     
     for (uint32_t i = 0; i < m_obstacles.size(); i++) {
         if ((m_obstacles[i].cells().size() <= 1) ||
             (m_obstacles[i].magnitude() == 0.0))
             continue;
         
         if (! m_obstacles[i].isValid())
             continue;
         
        visualization_msgs::Marker line_list;
        line_list.header.frame_id = FRAME_ID;
        line_list.header.stamp = ros::Time();
        line_list.ns = "rois";
        line_list.id = i;
        line_list.type = visualization_msgs::Marker::LINE_LIST;
        line_list.action = visualization_msgs::Marker::ADD;

        const pcl::PointCloud<pcl::PointXYZ>::Ptr & roi = m_obstacles[i].roi();

        line_list.pose.orientation.x = 0.0;
        line_list.pose.orientation.y = 0.0;
        line_list.pose.orientation.z = 0.0;
        line_list.pose.orientation.w = 1.0;
        line_list.scale.x = 0.01;
        line_list.scale.y = 0.01;
        line_list.scale.z = 0.01;
        line_list.color.a = 1.0;
        line_list.color.r = (double)rand() / RAND_MAX;
        line_list.color.g = (double)rand() / RAND_MAX;
        line_list.color.b = (double)rand() / RAND_MAX;
        
//         line_list.lifetime = ros::Duration(5.0);
        
        vector<geometry_msgs::Point> points(8);
        geometry_msgs::Point centroid;
        for (uint32_t j = 0; j < 8; j++) {
            points[j].x = roi->at(j).x;
            points[j].y = roi->at(j).y;
            points[j].z = roi->at(j).z;
            
            centroid.x += roi->at(j).x;
            centroid.y += roi->at(j).y;
            centroid.z += roi->at(j).z;
        }
        
        centroid.x /= roi->size();
        centroid.y /= roi->size();
        centroid.z /= roi->size();
            
        line_list.points.push_back(points[0]);
        line_list.points.push_back(points[1]);

        line_list.points.push_back(points[0]);
        line_list.points.push_back(points[3]);

        line_list.points.push_back(points[0]);
        line_list.points.push_back(points[6]);

        line_list.points.push_back(points[1]);
        line_list.points.push_back(points[2]);

        line_list.points.push_back(points[1]);
        line_list.points.push_back(points[7]);

        line_list.points.push_back(points[2]);
        line_list.points.push_back(points[3]);

        line_list.points.push_back(points[2]);
        line_list.points.push_back(points[4]);

        line_list.points.push_back(points[3]);
        line_list.points.push_back(points[5]);

        line_list.points.push_back(points[4]);
        line_list.points.push_back(points[5]);

        line_list.points.push_back(points[4]);
        line_list.points.push_back(points[7]);

        line_list.points.push_back(points[5]);
        line_list.points.push_back(points[6]);

        line_list.points.push_back(points[6]);
        line_list.points.push_back(points[7]);
        
//         line_list.points.push_back(points[0]);
//         line_list.points.push_back(points[4]);


        obstaclesROI.markers.push_back(line_list);
        
        visualization_msgs::Marker obstacles;
        obstacles.header.frame_id = FRAME_ID;
        obstacles.header.stamp = ros::Time();
        obstacles.id = i;
        line_list.ns = "rois";
        obstacles.type = visualization_msgs::Marker::CUBE;
        obstacles.action = visualization_msgs::Marker::ADD;
        
        const double minX = points[0].x;
        const double maxX = points[1].x;
        const double minY = points[0].y;
        const double maxY = points[6].y;
        const double maxZ = points[2].z;
        
        const double & normYaw = sqrt(centroid.x * centroid.x + centroid.y * centroid.y);
        double yaw = acos(centroid.y / normYaw);
        if (centroid.x > 0)
            yaw = -yaw;
        
        const tf::Quaternion & quat = tf::createQuaternionFromRPY(0.0, 0.0, yaw);
        
        obstacles.pose.position.x = centroid.x;
        obstacles.pose.position.y = centroid.y;
        obstacles.pose.position.z = centroid.z;
        obstacles.pose.orientation.x = quat.x();
        obstacles.pose.orientation.y = quat.y();
        obstacles.pose.orientation.z = quat.z();
        obstacles.pose.orientation.w = quat.w();
        obstacles.scale.x = maxX - minX;
        obstacles.scale.y = maxY - minY;
        obstacles.scale.z = maxZ;
        obstacles.color.a = 0.5;
        obstacles.color.r = line_list.color.r;
        obstacles.color.g = line_list.color.g;
        obstacles.color.b = line_list.color.b;
        
        obstaclesROI.markers.push_back(obstacles);
     
        // Orientation
        visualization_msgs::Marker orientation;
        orientation.header.frame_id = FRAME_ID;
        orientation.header.stamp = ros::Time();
        orientation.id = m_obstacles.size() + i - 1;
        orientation.type = visualization_msgs::Marker::ARROW;
        orientation.action = visualization_msgs::Marker::ADD;
        
        orientation.pose.orientation.x = 0.0;
        orientation.pose.orientation.y = 0.0;
        orientation.pose.orientation.z = 0.0;
        orientation.pose.orientation.w = 1.0;
        orientation.scale.x = 0.01;
        orientation.scale.y = 0.03;
        orientation.scale.z = 0.1;
        orientation.color.a = 1.0;
        orientation.color.r = line_list.color.r;
        orientation.color.g = line_list.color.g;
        orientation.color.b = line_list.color.b;
        
        orientation.lifetime = ros::Duration(5.0);

        orientation.points.push_back(centroid);
        centroid.x += cos(m_obstacles[i].yaw()) * m_obstacles[i].magnitude();
        centroid.y += sin(m_obstacles[i].yaw()) * m_obstacles[i].magnitude();
        orientation.points.push_back(centroid);

        obstaclesROI.markers.push_back(orientation);

        // Speed visualization
        visualization_msgs::Marker speedText;
        speedText.header.frame_id = FRAME_ID;
        speedText.header.stamp = ros::Time();
        speedText.id = 2 * (m_obstacles.size() - 1) + i;
        speedText.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
        speedText.action = visualization_msgs::Marker::ADD;
        
        speedText.pose.orientation.x = 0.0;
        speedText.pose.orientation.y = 0.0;
        speedText.pose.orientation.z = 0.0;
        speedText.pose.orientation.w = 1.0;
        
        speedText.scale.x = 0.2;
        speedText.scale.y = 0.2;
        speedText.scale.z = 0.2;
        
        speedText.color.r = 0.0;
        speedText.color.g = 1.0;
        speedText.color.b = 0.0;
        speedText.color.a = 1.0;
        
        speedText.lifetime = ros::Duration(5.0);
        
        speedText.pose.position.x = centroid.x;
        speedText.pose.position.y = centroid.y;
        speedText.pose.position.z = centroid.z;
        
        stringstream ss;
        ss << m_obstacles[i].magnitude() << " m/s";
        speedText.text = ss.str();
        
        obstaclesROI.markers.push_back(speedText);
     
     }
     
     m_roiPub.publish(obstaclesROI);
     
//     ros::spinOnce();
}

void polar_grid_trackingROS::publishRoiArrays()
{
    voxel_grid_tracking::roiArray roiMsg;
    roiMsg.rois3d.reserve(m_obstacles.size());
    roiMsg.rois2d.reserve(m_obstacles.size());
    
    roiMsg.header.seq = m_currentId;
    roiMsg.header.frame_id = m_cameraFrame;
    roiMsg.header.stamp = ros::Time::now();
    
    for (uint32_t i = 0; i < m_obstacles.size(); i++) {
        if ((m_obstacles[i].cells().size() <= 1) ||
            (m_obstacles[i].magnitude() == 0.0))
            continue;
        
        if (! m_obstacles[i].isValid())
            continue;
        
        visualization_msgs::Marker line_list;
        line_list.header.frame_id = FRAME_ID;
        line_list.header.stamp = ros::Time();
        line_list.ns = "rois";
        line_list.id = i;
        line_list.type = visualization_msgs::Marker::LINE_LIST;
        line_list.action = visualization_msgs::Marker::ADD;
        
        const pcl::PointCloud<pcl::PointXYZ>::Ptr & roi = m_obstacles[i].roi();
        
        vector<geometry_msgs::Point> points(8);
        voxel_grid_tracking::roi_and_speed_2d roi2D;
        voxel_grid_tracking::roi_and_speed_3d roi3D;
        
        for (uint32_t j = 0; j < 8; j++) {
            points[j].x = roi->at(j).x;
            points[j].y = roi->at(j).y;
            points[j].z = roi->at(j).z;
            
            roi3D.center.x += roi->at(j).x;
            roi3D.center.y += -roi->at(j).z;
            roi3D.center.z += roi->at(j).y;
        }
        
        roi3D.center.x /= roi->size();
        roi3D.center.y /= roi->size();
        roi3D.center.z /= roi->size();
        
        points[0].z -= 1.45;
        points[1].z -= 1.45;
        points[6].z -= 1.45;
        points[7].z -= 1.45;
        
        pcl::PointXYZRGB point3d, point2d;
        
        // A
        point3d.x = points[3].x;
        point3d.y = -points[3].z;
        point3d.z = points[3].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.A = toPoint32(point3d);
        roi2D.A = toPoint2D(point2d);
        
        // B
        point3d.x = points[2].x;
        point3d.y = -points[2].z;
        point3d.z = points[2].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.B = toPoint32(point3d);
        roi2D.B = toPoint2D(point2d);
        
        // C
        point3d.x = points[0].x;
        point3d.y = -points[0].z;
        point3d.z = points[0].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.C = toPoint32(point3d);
        roi2D.C = toPoint2D(point2d);

        // D
        point3d.x = points[1].x;
        point3d.y = -points[1].z;
        point3d.z = points[1].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.D = toPoint32(point3d);
        roi2D.D = toPoint2D(point2d);
        
        // E
        point3d.x = points[5].x;
        point3d.y = -points[5].z;
        point3d.z = points[5].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.E = toPoint32(point3d);
        roi2D.E = toPoint2D(point2d);
        
        // F
        point3d.x = points[4].x;
        point3d.y = -points[4].z;
        point3d.z = points[4].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.F = toPoint32(point3d);
        roi2D.F = toPoint2D(point2d);
        
        // G
        point3d.x = points[6].x;
        point3d.y = -points[6].z;
        point3d.z = points[6].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.G = toPoint32(point3d);
        roi2D.G = toPoint2D(point2d);
        
        // H
        point3d.x = points[7].x;
        point3d.y = -points[7].z;
        point3d.z = points[7].y;
        project3dTo2d(point3d, point2d, m_stereoCameraModel);
        roi3D.H = toPoint32(point3d);
        roi2D.H = toPoint2D(point2d);
        
        // Speed 3d
        roi3D.speed.x = cos(m_obstacles[i].yaw());
        roi3D.speed.y = sin(m_obstacles[i].yaw());
        roi3D.speed.z = 0.0;
        
        roi3D.speed_magnitude = m_obstacles[i].magnitude();

        // Speed 2d
//         point3d.x = -m_centerX;
//         point3d.y = m_centerY;
//         point3d.z = m_centerZ;
//         project3dTo2d(point3d, point2d, m_stereoCameraModel);
//         cv::Point2d p1(point2d.x, point2d.y);
        
//         point3d.x = -(m_centerX + m_vx);
//         point3d.y = (m_centerY + m_vy);
//         point3d.z = (m_centerZ + m_vz);
//         project3dTo2d(point3d, point2d, m_stereoCameraModel);
//         cv::Point2d p2(point2d.x, point2d.y);
        
//         p2 -= p1;
        
//         roi2D.speed.x = p2.x;
//         roi2D.speed.y = p2.y;
        roiMsg.rois3d.push_back(roi3D);
        roiMsg.rois2d.push_back(roi2D);
        
    }
    
    m_ROIPub.publish(roiMsg);
}


void polar_grid_trackingROS::clearObstaclesAndROIs()
{
    visualization_msgs::MarkerArray obstacles;
    visualization_msgs::MarkerArray obstaclesROI;
    
    for (uint32_t i = 0; i < 1000; i++) {
        visualization_msgs::Marker dummy;
        
        dummy.header.frame_id = FRAME_ID;
        dummy.header.stamp = ros::Time();
        dummy.action = visualization_msgs::Marker::DELETE;
        dummy.id = i;
        
        obstacles.markers.push_back(dummy);

        visualization_msgs::Marker dummy2;
        dummy2.header.frame_id = FRAME_ID;
        dummy2.header.stamp = ros::Time();
        dummy2.ns = "rois";
        dummy2.id = i;
        dummy2.type = visualization_msgs::Marker::LINE_LIST;
        dummy2.action = visualization_msgs::Marker::DELETE;
        
        obstaclesROI.markers.push_back(dummy2);
        
        // Orientation
        visualization_msgs::Marker dummy3;
        dummy3.header.frame_id = FRAME_ID;
        dummy3.header.stamp = ros::Time();
        dummy3.id = /*m_obstacles.size() + i - 1*/i;
        dummy3.type = visualization_msgs::Marker::ARROW;
        dummy3.action = visualization_msgs::Marker::DELETE;
        
        obstaclesROI.markers.push_back(dummy3);
        
        // Speed visualization
        visualization_msgs::Marker dummy4;
        dummy4.header.frame_id = FRAME_ID;
        dummy4.header.stamp = ros::Time();
        dummy4.id = 2 * (m_obstacles.size() - 1) + i;
        dummy4.type = visualization_msgs::Marker::TEXT_VIEW_FACING;
        dummy4.action = visualization_msgs::Marker::DELETE;
        
        obstaclesROI.markers.push_back(dummy4);
        
    }
    
    m_roiPub.publish(obstaclesROI);
    m_obstaclesPub.publish(obstacles);
}

void polar_grid_trackingROS::publishPointCloudInObstacles(const pcl::PointCloud< PointXYZRGBDirected >::Ptr & pointCloud)
{
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr tmpPointCloud(new pcl::PointCloud<pcl::PointXYZRGB>);

    for (pcl::PointCloud<PointXYZRGBDirected>::const_iterator it = pointCloud->begin();
            it != pointCloud->end(); it++) {

        int32_t row, column;
        const double z = it->z, x = it->x;
        getPolarPositionFromCartesian(z, x, row, column);
        if ((row == -1) || (column == -1))
            continue;
        
        const int32_t & obstIdx = m_polarGrid(row, column).obstIdx();
        if (obstIdx == -1)
            continue;
        
        const Obstacle obstacle = m_obstacles[obstIdx];
    
        if (! obstacle.isValid())
            continue;
        
        const PointXYZRGBDirected & point = *it;
        pcl::PointXYZRGB newPoint;

        newPoint.x = point.x/* - m_cellSizeX / 2.0*/;
        newPoint.y = point.z/* - m_cellSizeZ / 2.0*/;
        newPoint.z = point.y;
        newPoint.r = point.r;
        newPoint.g = point.g;
        newPoint.b = point.b;

        tmpPointCloud->push_back(newPoint);
    }


    sensor_msgs::PointCloud2 cloudMsg;
    pcl::toROSMsg (*tmpPointCloud, cloudMsg);
    cloudMsg.header.frame_id=FRAME_ID;
    cloudMsg.header.stamp = ros::Time();

    m_pointCloudInObstaclePub.publish(cloudMsg);

//    ros::spinOnce();
}

void polar_grid_trackingROS::visualizeROI2d()
{
    
    string imgPattern = "/local/imaged/Karlsruhe/2011_09_28/2011_09_28_drive_0038_sync/image_02/data/%010d.png";
    //     string imgPattern = "/local/imaged/Karlsruhe/2011_09_26/2011_09_26_drive_0091_sync/image_02/data/%010d.png";
    char imgNameL[1024];
    sprintf(imgNameL, imgPattern.c_str(), m_currentId + 55);
    
    stringstream ss;
    ss << "/home/nestor/Vídeos/VoxelTracking/pedestrian/output";
    ss.width(10);
    ss.fill('0');
    ss << m_currentId;
    ss << ".png";
    
    cout << "rois2d: " << imgNameL << endl;
    
    cv::Mat img = cv::imread(string(imgNameL));
    
    Eigen::Matrix< double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> pointMat(3, 1);
    
    BOOST_FOREACH(const pcl::PointXYZRGB & point, m_pointCloud->points) {
        //         pointMat << m_cameraParams.t(0) - point.x, m_cameraParams.t(1) + point.y, m_cameraParams.t(2) + point.z;
        pointMat << m_cameraParams.t(0) - point.x, m_cameraParams.t(1) + point.y, m_cameraParams.t(2) + point.z;
        //         pointMat << m_cameraParams.t(0) + point.x, m_cameraParams.t(1) - point.z, m_cameraParams.t(2) + point.y;
        
        pointMat = m_cameraParams.R.inverse() * pointMat;
        
        
        const double d = m_cameraParams.ku * m_cameraParams.baseline / pointMat(2);
        const double u = m_cameraParams.u0 - ((pointMat(0) * d) / m_cameraParams.baseline);
        const double v = m_cameraParams.v0 - ((pointMat(1) * m_cameraParams.kv * d) / (m_cameraParams.ku * m_cameraParams.baseline));
        
        //         cout << cv::Point3d(- point.x, point.y, point.z) << " -> " << pointMat.transpose() 
        //         << " -> " << cv::Point3d(u, v, d) << endl;
        
        if ((u >= 0) && (u < img.cols) &&
            (v >= 0) && (v < img.rows)) {
            
            img.at<cv::Vec3b>(v, u) = cv::Vec3b(point.b, point.g, 255);
            }
    }
    
    pcl::PointXYZRGB point3d, point;
    
    for (uint32_t i = 0; i < m_obstacles.size(); i++) {
        if ((m_obstacles[i].cells().size() <= 1) ||
            (m_obstacles[i].magnitude() == 0.0))
            continue;
        
        if (! m_obstacles[i].isValid())
            continue;
        
        const pcl::PointCloud<pcl::PointXYZ>::Ptr & roi = m_obstacles[i].roi();
        
        cv::Point2d pointUL(img.cols, img.rows), pointBR(0, 0);
        
        for (uint32_t j = 0; j < roi->size(); j++) {
            point3d.x = -roi->at(j).x;
            point3d.y = roi->at(j).z;
            point3d.z = roi->at(j).y;
            project3dTo2d(point3d, point, m_cameraParams);
            
            if (point.x < pointUL.x) pointUL.x = point.x;
            if (point.y < pointUL.y) pointUL.y = point.y;
            if (point.x > pointBR.x) pointBR.x = point.x;
            if (point.y > pointBR.y) pointBR.y = point.y;
        }

        cv::rectangle(img, pointUL, pointBR, cv::Scalar((double)rand() / RAND_MAX * 128 + 128, (double)rand() / RAND_MAX * 128 + 128, (double)rand() / RAND_MAX * 128 + 128), 1);
    }
    
//     cv::imwrite(ss.str(), img);
    
    cv::imshow("rois2d", img);
    
    cv::waitKey(200);
}


}
