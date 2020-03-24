#include "algorithms/SelfModel.h"

#include <rtl/Transformation3D.h>

#include "algorithms/tools.h"
#include "local_map/Frames.h"

namespace AutoDrive::Algorithms {

    /// Hooks
    void SelfModel::onGnssPose(std::shared_ptr<DataModels::GnssPoseDataModel> data) {

        auto gnssPose = DataModels::GlobalPosition{ data->getLatitude(), data->getLongitude(), data->getAltitude(), 0};
        auto imuPose = gnssPoseToRootFrame(gnssPose);

        if(!poseInitialized_) {
            anchorPose = imuPose;
            poseInitialized_ = true;

            lastImuTimestamp_ = data->getTimestamp();
            lastDquatTimestamp_ = data->getTimestamp();
        }


        auto heading = fuseHeadings(data);
        updateOrientation(heading.first);
        orientationInitialized_ = true;


        while(positionHistory_.size() >= 100) {
            positionHistory_.pop_front();
        }

        auto offset = DataModels::GlobalPosition::getOffsetBetweenCoords(anchorPose, imuPose);
        cv::Mat measurementX = (cv::Mat_<double>(2, 1) << offset.x(), 0);
        cv::Mat measurementY = (cv::Mat_<double>(2, 1) << offset.y(), 0);
        cv::Mat measurementZ = (cv::Mat_<double>(2, 1) << offset.z(), 0);
        kalmanX_.correct(measurementX);
        kalmanY_.correct(measurementY);
        kalmanZ_.correct(measurementZ);

        DataModels::LocalPosition position{{kalmanX_.getPosition(), kalmanY_.getPosition(), kalmanZ_.getPosition()},
                                           orientation_,
                                           data->getTimestamp()};
        positionHistory_.push_back(position);

        lastGnssTimestamp_ = data->getTimestamp();
    }

    void SelfModel::onImuImuData(std::shared_ptr<DataModels::ImuImuDataModel> data) {


        if(poseInitialized_) {
            auto linAccNoGrav = removeGravitationalForceFromLinAcc(data);
            auto orientatnionTF = rtl::Transformation3D<double>{orientation_,{}};
            auto rotatedLinAccNoGrav = orientatnionTF(linAccNoGrav);

            auto dt = (data->getTimestamp() - lastImuTimestamp_) * 1e-9;
            kalmanX_.predict(dt, rotatedLinAccNoGrav.x());
            kalmanY_.predict(dt, rotatedLinAccNoGrav.y());
            kalmanZ_.predict(dt, rotatedLinAccNoGrav.z());

            accHistory_.emplace_back( std::pair<rtl::Vector3D<double>,uint64_t >{linAccNoGrav, data->getTimestamp()});

            if( (data->getTimestamp() - accHistory_.front().second) * 1e-9 > 1.0  ) {
                accHistory_.pop_front();

            }
        }

        if(orientationInitialized_) {
            double r,p,y;
            quaternionToRPY(data->getOrientation(), r, p, y);
            rtl::Quaternion<double> modifiedImuOrientation = rpyToQuaternion(r, p ,getHeading());
            orientation_ = orientation_.slerp(modifiedImuOrientation, 0.001);
        }

        lastImuTimestamp_ = data->getTimestamp();
    }

    void SelfModel::onImuDquatData(std::shared_ptr<DataModels::ImuDquatDataModel> data) {
//        orientation_ = orientation_ * data->getDQuat();
//        lastDquatTimestamp_ = data->getTimestamp();
    }

    /// Getters

    // Position
    DataModels::LocalPosition SelfModel::getPosition() const {
        if(positionHistory_.empty()){
            return DataModels::LocalPosition{{},{},0};
        }
        return positionHistory_.back();
    }

    std::deque<DataModels::LocalPosition> SelfModel::getPositionHistory() {
        return positionHistory_;
    }

    // Speed
    double SelfModel::getSpeedScalar() const {
        return std::sqrt( std::pow(kalmanX_.getSpeed(),2) + std::pow(kalmanY_.getSpeed(),2) + std::pow(kalmanZ_.getSpeed(),2) );
    }

    rtl::Vector3D<double> SelfModel::getSpeedVector() const {
        auto speed = rtl::Vector3D<double>{kalmanX_.getSpeed(), kalmanY_.getSpeed(), kalmanZ_.getSpeed()};
        auto tf = rtl::Transformation3D<double> {orientation_,{}};
        return tf.inverted()(speed);
    }

    // Acc

    double SelfModel::getAvgAccScalar() const {
        auto acc = getAvgAcceleration();
        return std::sqrt( std::pow(acc.x(),2) + std::pow(acc.y(),2) + std::pow(acc.z(),2) );
    }

    rtl::Vector3D<double> SelfModel::getAvgAcceleration() const{
        rtl::Vector3D<double> sum;
        for(const auto& acc : accHistory_) {
            sum += acc.first;
        }
        return sum / accHistory_.size();
    }

    // Orientation
    rtl::Quaternion<double> SelfModel::getOrientation() const {
        return orientation_;
    }

    double SelfModel::getHeading() const {
        double r, p, y;
        quaternionToRPY(orientation_, r, p, y);
        return y;
    }


    std::string SelfModel::getTelemetryString() const {

        auto acc = getAvgAcceleration();

        std::stringstream ss;
        ss << "Telemetry:" << std::endl
           << "Pose: " << getPosition().getPosition().x() << " " << getPosition().getPosition().y() << " " << getPosition().getPosition().z() << " m"<< std::endl
           << "Speed: " << getSpeedVector().x() << " " << getSpeedVector().y() << " " << getSpeedVector().z() << " m/s " << std::endl
           << "Acc: " << getAvgAcceleration().x() << " " << getAvgAcceleration().y() << " " << getAvgAcceleration().z() << " m/s^2 " << std::endl
           << "Heading: " << getHeading() << std::endl
           << "Speed scal: " << getSpeedScalar() << " m/s" << std::endl
           << "Acc scal" << getAvgAccScalar() << " m/s^2 " << std::endl;

        return ss.str();
    }

    DataModels::LocalPosition SelfModel::estimatePositionInTime(const uint64_t time) {

        for(int i = positionHistory_.size()-1; i >= 0  ; i--) {
            if(positionHistory_.at(i).getTimestamp() < time) {

                if(i == 0) {
                    return positionHistory_.at(i);
                } else {

                    auto poseBefore = &positionHistory_.at(std::min(static_cast<size_t>(i), positionHistory_.size()-2));
                    auto poseAfter = &positionHistory_.at(std::min(static_cast<size_t>(i+1), positionHistory_.size()-1));

                    auto numerator = static_cast<float>(poseAfter->getTimestamp() - time);
                    auto denominator = static_cast<float>(poseAfter->getTimestamp() - poseBefore->getTimestamp());
                    float ratio = numerator / denominator;

                    rtl::Vector3D<double> interpolatedPose{
                            poseBefore->getPosition().x() + (poseAfter->getPosition().x() - poseBefore->getPosition().x()) * ratio,
                            poseBefore->getPosition().y() + (poseAfter->getPosition().y() - poseBefore->getPosition().y()) * ratio,
                            poseBefore->getPosition().z() + (poseAfter->getPosition().z() - poseBefore->getPosition().z()) * ratio,
                    };

                    auto newOrientation = poseBefore->getOrientation().slerp(poseAfter->getOrientation(), ratio);

                    uint64_t duration = poseAfter->getTimestamp() - poseBefore->getTimestamp();
                    uint64_t ts = poseBefore->getTimestamp() + static_cast<uint64_t>(duration * ratio);

                    return DataModels::LocalPosition{interpolatedPose, newOrientation, ts};
                }
            }
        }
        context_.logger_.warning("Unable to estimate position in time! Missing time point in history.");
        return DataModels::LocalPosition{{},{},0};
    }


    /// Private Methods


    std::pair<double, float> SelfModel::validHeading(std::shared_ptr<DataModels::GnssPoseDataModel> data) {
        if(data->getAzimut() != 0) {
            validHeadingCounter_++;
        } else {
            validHeadingCounter_ = 0;
        }
        float validityFactor = std::min(static_cast<float>( 1 / ( 1 + std::exp( -0.1 * ( validHeadingCounter_ - 50.0 ) ) ) ), 0.9f);
        return {data->getHeading(), validityFactor};
    }


    std::pair<double, float> SelfModel::speedHeading() {
        double heading = atan2(kalmanY_.getSpeed(), kalmanX_.getSpeed());
        double speed = getSpeedScalar();
        float validityFactor = static_cast<float>( 1 / ( 1 + std::exp( -1.0 * ( speed - 5.0 ) ) ) );
        return {heading, validityFactor};
    }


    std::pair<double, float> SelfModel::fuseHeadings(std::shared_ptr<DataModels::GnssPoseDataModel> data) {

        auto gnssHeading = validHeading(data);
        auto spdHeading = speedHeading();

        auto gnssQuaternion = rpyToQuaternion(0, 0, gnssHeading.first);
        auto speedQuaternion = rpyToQuaternion(0, 0, spdHeading.first);

        auto slerpFactor = estimateSlerpFactor(gnssHeading.second, spdHeading.second);
        auto fused = gnssQuaternion.slerp(speedQuaternion, 1-slerpFactor);

        double r, p, y;
        quaternionToRPY( fused, r, p, y);
        return {y, std::max(gnssHeading.second, spdHeading.second)};
    }


    float SelfModel::estimateSlerpFactor(float a, float b) {
        a += 0.001;
        b += 0.001;
        return a / (a+b);
    }


    void SelfModel::updateOrientation(double heading) {
        double r, p, y;
        quaternionToRPY( orientation_ , r, p, y);
        orientation_ = rpyToQuaternion(r, p, heading);
    }


    DataModels::GlobalPosition SelfModel::gnssPoseToRootFrame(const DataModels::GlobalPosition gnssPose) {

        // TODO: validate

        auto tf = rtl::Transformation3D<double>{orientation_,{}};
        auto frameOffset = context_.tfTree_.transformPointFromFrameToFrame({}, LocalMap::Frames::kGnssAntennaRear, LocalMap::Frames::kImuFrame);
        auto rotatedFramesOffset = tf.inverted()(frameOffset);

        auto gnssOffset = DataModels::LocalPosition{frameOffset, {}, getCurrentTime()};
        return DataModels::GlobalPosition::localPoseToGlobalPose(gnssOffset, gnssPose);
    }


    rtl::Vector3D<double> SelfModel::removeGravitationalForceFromLinAcc( std::shared_ptr<DataModels::ImuImuDataModel> data ) {

        imuProcessor_.setOrientation(data->getOrientation());
        return imuProcessor_.removeGravitaionAcceleration(data->getLinearAcc());
    }


    uint64_t SelfModel::getCurrentTime() const {
        return std::max(std::max(lastGnssTimestamp_, lastImuTimestamp_), lastDquatTimestamp_);
    }

}

