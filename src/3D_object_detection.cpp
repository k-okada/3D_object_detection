#include <ros/ros.h>
// PCL specific includes
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include <pcl/filters/voxel_grid.h>

#include <pcl/io/pcd_io.h>

#include <pcl/correspondence.h>
#include <pcl/features/normal_3d_omp.h>
#include <pcl/features/shot_omp.h>
#include <pcl/features/board.h>
#if ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 7)
#include <pcl/keypoints/uniform_sampling.h>
#elif ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 8)
#include <pcl/filters/uniform_sampling.h>
#endif

//#include <pcl/recognition/cg/hough_3d.h>
//#include <pcl/recognition/cg/geometric_consistency.h>
#include "3D_object_detection/hough_3d.h"
#include "3D_object_detection/geometric_consistency.h"
//#include "pcl/hough_3d.h"
//#include "pcl/geometric_consistency.h"

#include <pcl/visualization/pcl_visualizer.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <pcl/kdtree/impl/kdtree_flann.hpp>
#include <pcl/common/transforms.h>
#include <pcl/console/parse.h>

typedef pcl::PointXYZRGBA PointType;
typedef pcl::Normal NormalType;
typedef pcl::ReferenceFrame RFType;
typedef pcl::SHOT352 DescriptorType;

using namespace std;

ros::Publisher pub;
std::string model_filename_ = "milk.pcd";
std::string scene_filename_ = "milk_cartoon_all_small_clorox.pcd";


//initialization of all variables used for correspondence grouping
//Algorithm params
bool show_keypoints_ (false);
bool show_correspondences_ (false);
bool use_cloud_resolution_ (false);
bool use_hough_ (true);
float model_ss_ (0.01f);
float scene_ss_ (0.03f);
float rf_rad_ (0.015f);
float descr_rad_ (0.02f);
float cg_size_ (0.01f);
float cg_thresh_ (5.0f);
//model_filename_ = "milk.pcd";
//scene_filename_ = "milk_cartoon_all_small_clorox.pcd";
 
void cloud_cb (const sensor_msgs::PointCloud2ConstPtr& cloud_msg)
{
   
   // Container for original & filtered data
   //input point cloud
   //commenting for now
   //pcl::PCLPointCloud2* cloud = new pcl::PCLPointCloud2; 
   //pcl::PCLPointCloud2ConstPtr cloudPtr(cloud);
   //pcl::PCLPointCloud2 cloud_filtered;

   // Convert to PCL data type
   //pcl_conversions::toPCL(*cloud_msg, *cloud);
 
   
    //3D object detection core code
    pcl::PointCloud<PointType>::Ptr model (new pcl::PointCloud<PointType> ());
#if ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 7)
    pcl::PointCloud<int>::Ptr model_indices (new pcl::PointCloud<int> ());
#endif
    pcl::PointCloud<PointType>::Ptr model_keypoints (new pcl::PointCloud<PointType> ());
    pcl::PointCloud<PointType>::Ptr scene (new pcl::PointCloud<PointType> ());
    pcl::PointCloud<PointType>::Ptr scene_keypoints (new pcl::PointCloud<PointType> ());
    pcl::PointCloud<NormalType>::Ptr model_normals (new pcl::PointCloud<NormalType> ());
    pcl::PointCloud<NormalType>::Ptr scene_normals (new pcl::PointCloud<NormalType> ());
    pcl::PointCloud<DescriptorType>::Ptr model_descriptors (new pcl::PointCloud<DescriptorType> ());
    pcl::PointCloud<DescriptorType>::Ptr scene_descriptors (new pcl::PointCloud<DescriptorType> ());

    //
    //  Load clouds
    //
    if (pcl::io::loadPCDFile (model_filename_, *model) < 0)
    {
       std::cout << "Error loading model cloud." << std::endl;
       //return (-1);
    }
   if (pcl::io::loadPCDFile (scene_filename_, *scene) < 0)
   {
       std::cout << "Error loading scene cloud." << std::endl;
       //return (-1);
   }
   
   //
   //  Compute Normals
   //
   pcl::NormalEstimationOMP<PointType, NormalType> norm_est;
   norm_est.setKSearch (10);
   norm_est.setInputCloud (model);
   norm_est.compute (*model_normals);

   norm_est.setInputCloud (scene);
   norm_est.compute (*scene_normals);

   //
   //  Downsample Clouds to Extract keypoints
   //

   pcl::UniformSampling<PointType> uniform_sampling;
   uniform_sampling.setInputCloud (model);
   uniform_sampling.setRadiusSearch (model_ss_);
#if ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 7)
    uniform_sampling.compute(*model_indices);
    // FIX ME : need to add code to compute model_keypoints from model_indices
#elif ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 8)
   uniform_sampling.filter (*model_keypoints);
#endif
   std::cout << "Model total points: " << model->size () << "; Selected Keypoints: " << model_keypoints->size () << std::endl;

   uniform_sampling.setInputCloud (scene);
   uniform_sampling.setRadiusSearch (scene_ss_);
#if ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 7)
    uniform_sampling.compute(*model_indices);
    // FIX ME : need to add code to compute model_keypoints from model_indices
#elif ( PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION == 8)
   uniform_sampling.filter (*scene_keypoints);
#endif
   std::cout << "Scene total points: " << scene->size () << "; Selected Keypoints: " << scene_keypoints->size () << std::endl;


   //
   //  Compute Descriptor for keypoints
   //
   pcl::SHOTEstimationOMP<PointType, NormalType, DescriptorType> descr_est;
   descr_est.setRadiusSearch (descr_rad_);

   descr_est.setInputCloud (model_keypoints);
   descr_est.setInputNormals (model_normals);
   descr_est.setSearchSurface (model);
   descr_est.compute (*model_descriptors);

   descr_est.setInputCloud (scene_keypoints);
   descr_est.setInputNormals (scene_normals);
   descr_est.setSearchSurface (scene);
   descr_est.compute (*scene_descriptors);
   
   //
   //  Find Model-Scene Correspondences with KdTree
   //
   pcl::CorrespondencesPtr model_scene_corrs (new pcl::Correspondences ());
 
   pcl::KdTreeFLANN<DescriptorType> match_search;
   match_search.setInputCloud (model_descriptors);

   //  For each scene keypoint descriptor, find nearest neighbor into the model keypoints descriptor cloud and add it to the correspondences vector.
   for (size_t i = 0; i < scene_descriptors->size (); ++i)
   {
     std::vector<int> neigh_indices (1);
     std::vector<float> neigh_sqr_dists (1);
     if (!pcl_isfinite (scene_descriptors->at (i).descriptor[0])) //skipping NaNs
     {
       continue;
     }
     int found_neighs = match_search.nearestKSearch (scene_descriptors->at (i), 1, neigh_indices, neigh_sqr_dists);
     if(found_neighs == 1 && neigh_sqr_dists[0] < 0.25f) //  add match only if the squared descriptor distance is less than 0.25 (SHOT descriptor distances are between 0 and 1 by design)
     {
       pcl::Correspondence corr (neigh_indices[0], static_cast<int> (i), neigh_sqr_dists[0]);
       model_scene_corrs->push_back (corr);
     }
   }
   std::cout << "Correspondences found: " << model_scene_corrs->size () << std::endl;

   //
   //  Actual Clustering
   //
   std::vector<Eigen::Matrix4f, Eigen::aligned_allocator<Eigen::Matrix4f> > rototranslations;
   std::vector<pcl::Correspondences> clustered_corrs;

   //  Using Hough3D
   if (use_hough_)
   {
     //
     //  Compute (Keypoints) Reference Frames only for Hough
     //
     pcl::PointCloud<RFType>::Ptr model_rf (new pcl::PointCloud<RFType> ());
     pcl::PointCloud<RFType>::Ptr scene_rf (new pcl::PointCloud<RFType> ());
 
     pcl::BOARDLocalReferenceFrameEstimation<PointType, NormalType, RFType> rf_est;
     rf_est.setFindHoles (true);
     rf_est.setRadiusSearch (rf_rad_);

     rf_est.setInputCloud (model_keypoints);
     rf_est.setInputNormals (model_normals);
     rf_est.setSearchSurface (model);
     rf_est.compute (*model_rf);

     rf_est.setInputCloud (scene_keypoints);
     rf_est.setInputNormals (scene_normals);
     rf_est.setSearchSurface (scene);
     rf_est.compute (*scene_rf);

     //  Clustering
     pcl::Hough3DGrouping<PointType, PointType, RFType, RFType> clusterer;
     clusterer.setHoughBinSize (cg_size_);
     clusterer.setHoughThreshold (cg_thresh_);
     clusterer.setUseInterpolation (true);
     clusterer.setUseDistanceWeight (false);

     clusterer.setInputCloud (model_keypoints);
     clusterer.setInputRf (model_rf);
     clusterer.setSceneCloud (scene_keypoints);
     clusterer.setSceneRf (scene_rf);
     clusterer.setModelSceneCorrespondences (model_scene_corrs);

     //clusterer.cluster (clustered_corrs);
     clusterer.recognize (rototranslations, clustered_corrs);
   }
   else // Using GeometricConsistency
   {
     pcl::GeometricConsistencyGrouping<PointType, PointType> gc_clusterer;
     gc_clusterer.setGCSize (cg_size_);
     gc_clusterer.setGCThreshold (cg_thresh_);
 
     gc_clusterer.setInputCloud (model_keypoints);
     gc_clusterer.setSceneCloud (scene_keypoints);
     gc_clusterer.setModelSceneCorrespondences (model_scene_corrs);

     //gc_clusterer.cluster (clustered_corrs);
     gc_clusterer.recognize (rototranslations, clustered_corrs);
   }

   //
   //  Output results
   //
   std::cout << "Model instances found: " << rototranslations.size () << std::endl;
   for (size_t i = 0; i < rototranslations.size (); ++i)
   {
     std::cout << "\n    Instance " << i + 1 << ":" << std::endl;
     std::cout << "        Correspondences belonging to this instance: " << clustered_corrs[i].size () << std::endl;
 
     // Print the rotation matrix and translation vector
     Eigen::Matrix3f rotation = rototranslations[i].block<3,3>(0, 0);
     Eigen::Vector3f translation = rototranslations[i].block<3,1>(0, 3);
 
     printf ("\n");
     printf ("            | %6.3f %6.3f %6.3f | \n", rotation (0,0), rotation (0,1), rotation (0,2));
     printf ("        R = | %6.3f %6.3f %6.3f | \n", rotation (1,0), rotation (1,1), rotation (1,2));
     printf ("            | %6.3f %6.3f %6.3f | \n", rotation (2,0), rotation (2,1), rotation (2,2));
     printf ("\n");
     printf ("        t = < %0.3f, %0.3f, %0.3f >\n", translation (0), translation (1), translation (2));
   }
 
 //Need to figure out how the result point cloud should be visualized 
    
   
   

   // Convert to ROS data type
   //sensor_msgs::PointCloud2 output;
   //pcl_conversions::fromPCL(cloud_filtered, output);
 
   // Publish the data.
   //pub.publish (output);
}
 
int main (int argc, char** argv)
{
   // Initialize ROS
   ros::init (argc, argv, "my_pcl_tutorial");
   ros::NodeHandle nh;
 
   // Create a ROS subscriber for the input point cloud
   ros::Subscriber sub = nh.subscribe<sensor_msgs::PointCloud2> ("input", 1, cloud_cb);
  
   // Create a ROS publisher for the output point cloud
   pub = nh.advertise<sensor_msgs::PointCloud2> ("output", 1);
  
   // Create a ROS subscriber for the input point cloud
   //ros::Subscriber sub = nh.subscribe ("input", 1, cloud_cb);
 
   // Create a ROS publisher for the output point cloud
   //pub = nh.advertise<sensor_msgs::PointCloud2> ("output", 1);
 
   // Spin
   ros::spin ();
}

