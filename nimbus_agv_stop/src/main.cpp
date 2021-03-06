//ROS
#include <ros/ros.h>
#include <sensor_msgs/PointCloud2.h>
#include <std_msgs/Int8.h>
#include "visualization_msgs/Marker.h"
#include "visualization_msgs/MarkerArray.h"

// PCL specific includes
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include "pcl_ros/point_cloud.h"
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/io/pcd_io.h>
#include <pcl/surface/convex_hull.h>
#include <pcl/filters/crop_hull.h>
#include <pcl/filters/crop_box.h>
#include <pcl/filters/extract_indices.h>
#include <pcl/io/io.h>
#include <pcl/io/pcd_io.h>
#include <pcl/filters/passthrough.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <pcl_ros/point_cloud.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl/point_cloud.h>
#include <pcl/filters/radius_outlier_removal.h>
#include <pcl/filters/conditional_removal.h>
#include <pcl/filters/filter.h>
#include <pcl/filters/random_sample.h>
#include <pcl/segmentation/sac_segmentation.h>

//Misc
#include <chrono> 

typedef pcl::PointCloud<pcl::PointXYZI> PointCloud;
ros::Publisher pub_roi;
ros::Publisher pub_voxel;
ros::Publisher pub_outlier;
ros::Publisher pub_ground;

ros::Publisher vis_pub;
ros::Publisher warn_pub;


void cloud_cb (const PointCloud::ConstPtr& cloud)
{
    auto start = std::chrono::high_resolution_clock::now();

    //Filter ROI with area inside 3d box
    pcl::CropBox<pcl::PointXYZI> boxFilter;
    boxFilter.setMin(Eigen::Vector4f(-2, 0.6, -0.1, 1.0));
    boxFilter.setMax(Eigen::Vector4f(2, 5, 2, 1.0));
    boxFilter.setInputCloud(cloud);
    PointCloud::Ptr cloud_filtered(new PointCloud);
    boxFilter.filter(*cloud_filtered);

    //Voxelize the point cloud to speed up computation
    pcl::VoxelGrid<pcl::PointXYZI> sor;
    sor.setInputCloud (cloud_filtered);
    sor.setLeafSize (0.02, 0.02, 0.02);
    PointCloud::Ptr cloud_filtered_voxel(new PointCloud);
    sor.filter (*cloud_filtered_voxel);

    //Remove outlier
    pcl::RadiusOutlierRemoval<pcl::PointXYZI> outrem;
    outrem.setInputCloud(cloud_filtered_voxel);
    outrem.setRadiusSearch(0.1);
    outrem.setMinNeighborsInRadius(10);
    outrem.setKeepOrganized(true);
    PointCloud::Ptr cloud_filtered_outlier(new PointCloud);
    outrem.filter(*cloud_filtered_outlier);

    //Remove Ground with RANSAC
    pcl::ModelCoefficients::Ptr coefficients (new pcl::ModelCoefficients);
    pcl::PointIndices::Ptr inliers (new pcl::PointIndices);
    pcl::SACSegmentation<pcl::PointXYZI> seg;
    //Set up parameters for our segmentation/ extraction scheme
    seg.setOptimizeCoefficients(true);
    seg.setModelType(pcl::SACMODEL_PERPENDICULAR_PLANE ); //only want points perpendicular to a given axis
    seg.setMaxIterations(500); // this is key (default is 50 and that sucks)
    seg.setNumberOfThreads(4);
    //seg.setSamplesMaxDist()
    seg.setMethodType(pcl::SAC_RANSAC);
    seg.setDistanceThreshold(0.1); // keep points within 0.1 m of the plane
    //because we want a specific plane (X-Z Plane) (In camera coordinates the ground plane is perpendicular to the y axis)
    Eigen::Vector3f axis = Eigen::Vector3f(0.0,1.0,0.0); //y axis
    seg.setAxis(axis);
    seg.setEpsAngle(30.0f * (3.149/180.0f) ); // plane can be within 30 degrees of X-Z plane
    seg.setInputCloud(cloud_filtered_outlier);
    seg.segment(*inliers, *coefficients);

    //extract the outlier
    pcl::ExtractIndices<pcl::PointXYZI> extract;
    extract.setInputCloud(cloud_filtered_outlier);
    extract.setIndices(inliers);
    extract.setNegative(false);
    PointCloud::Ptr cloud_filtered_ground(new PointCloud);
    extract.filter(*cloud_filtered_ground);

    auto finish = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = finish - start;
  
    ROS_INFO_STREAM("inliers: " << inliers->indices.size());
    ROS_INFO_STREAM("Removed Points: " << cloud->points.size()-cloud_filtered_ground->points.size() << " Points. In time: " << elapsed.count());

    std_msgs::Int8 warn_msg;
    
    visualization_msgs::Marker marker;
    marker.header.frame_id = "nimbus";
    marker.header.stamp = ros::Time();
    marker.ns = "nimbus_agv_emergency";
    marker.id = 0;
    marker.type = visualization_msgs::Marker::CUBE;
    marker.action = visualization_msgs::Marker::ADD;
    marker.pose.position.x = 0;
    marker.pose.position.y = 0;
    marker.pose.position.z = 0;
    marker.pose.orientation.x = 0.0;
    marker.pose.orientation.y = 0.0;
    marker.pose.orientation.z = 0.0;
    marker.pose.orientation.w = 1.0;
    marker.scale.x = 0.5;
    marker.scale.y = 1;
    marker.scale.z = 2;
      
    //Determine closest point and show warning and emergency
    if(cloud_filtered_ground->points.size() > 0){
      float closest_point = 99;
      int closest_point_index = 0; 
      for(int i = 0; i < cloud_filtered_ground->points.size(); i++){
        if(closest_point > cloud_filtered_ground->points[i].y)
          closest_point = cloud_filtered_ground->points[i].y;
      }
      marker.pose.position.x = cloud_filtered_ground->points[closest_point_index].x;
      marker.pose.position.y = cloud_filtered_ground->points[closest_point_index].y;
      marker.pose.position.z = 1;
      if(closest_point < 1){
        marker.color.a = 0.7; 
        marker.color.r = 1.0;
        marker.color.g = 0.0;
        marker.color.b = 0.0;
        warn_msg.data = 2;
      }
      else if(closest_point < 2){
        marker.color.a = 0.5; 
        marker.color.r = 1.0;
        marker.color.g = 1.0;
        marker.color.b = 0.0;
        warn_msg.data = 1;
      }
      else{
        marker.color.a = 0.0; 
        marker.color.r = 0.0;
        marker.color.g = 0.0;
        marker.color.b = 0.0;
        warn_msg.data = 0;
      }
    }
    else{
      marker.color.a = 0.0; 
      marker.color.r = 0.0;
      marker.color.g = 0.0;
      marker.color.b = 0.0; 
      warn_msg.data = 0;
    }
    vis_pub.publish(marker);
    warn_pub.publish(warn_msg);

    //publish the data.
    pub_roi.publish(cloud_filtered);
    pub_voxel.publish(cloud_filtered_voxel);
    pub_outlier.publish(cloud_filtered_outlier);
    pub_ground.publish(cloud_filtered_ground);
}

int main (int argc, char** argv)
{
  // Initialize ROS
  ros::init (argc, argv, "nimbus_agv_emergency");
  ros::NodeHandle nh;

  // Create a ROS publisher for the output point cloud
  pub_roi      = nh.advertise<pcl::PointCloud<pcl::PointXYZI>>("roi", 1);
  pub_voxel    = nh.advertise<pcl::PointCloud<pcl::PointXYZI>>("voxelized", 1);
  pub_outlier  = nh.advertise<pcl::PointCloud<pcl::PointXYZI>>("outlier", 1);
  pub_ground   = nh.advertise<pcl::PointCloud<pcl::PointXYZI>>("ground", 1);

  vis_pub = nh.advertise<visualization_msgs::Marker>( "visualization_marker", 0 );
  warn_pub = nh.advertise<std_msgs::Int8>( "obstacle", 0 );

  // Create a ROS subscriber for the input point cloud
  ros::Subscriber sub = nh.subscribe<pcl::PointCloud<pcl::PointXYZI>>("/nimbus/pointcloud", 1, cloud_cb);

  // Spin
  ros::spin();
}