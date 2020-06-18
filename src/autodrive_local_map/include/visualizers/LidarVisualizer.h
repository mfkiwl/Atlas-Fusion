#pragma once

#include <ros/ros.h>

#include <pcl/io/pcd_io.h>
#include <pcl/point_types.h>
#include <sensor_msgs/PointCloud2.h>
#include <visualization_msgs/MarkerArray.h>

#include "Context.h"
#include "Topics.h"
#include "data_models/local_map/LidarDetection.h"

namespace AutoDrive::Visualizers {

    /**
     * Visualization backend (ROS) implementations for visualizing point cloud structures, like raw lidar scans or
     * aggregated point clouds
     */
    class LidarVisualizer {

    public:

        enum class LidarType{
            kLeft,
            kRight,
        };

        /**
         * Constructor
         * @param node ros node reference
         * @param context global services container (timestamps, logging, etc.)
         */
        LidarVisualizer(ros::NodeHandle& node, Context& context)
        : node_{node}
        , context_{context} {

        }


        void drawPointcloudOnTopic(const std::shared_ptr<pcl::PointCloud<pcl::PointXYZ>> pc, std::string topic, std::string frame);

        void drawApproximationOnTopic(std::shared_ptr<std::vector<rtl::LineSegment3D<double>>> ls, std::string topic, std::string frame, visualization_msgs::Marker::_color_type col);

        void drawLidarDetections(std::vector<std::shared_ptr<const DataModels::LidarDetection>> detections, std::string topic, std::string frame);

    private:

        ros::NodeHandle& node_;
        Context& context_;

        std::map<std::string, std::shared_ptr<ros::Publisher>> publishers_;

        visualization_msgs::MarkerArray lidarDetectionsToMarkerArray(std::vector<std::shared_ptr<const DataModels::LidarDetection>> detections, std::string frame);
    };
}
