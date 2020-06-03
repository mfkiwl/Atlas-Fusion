#include "data_writers/YoloDetectionWriter.h"

#include <opencv2/opencv.hpp>

namespace AutoDrive::DataWriters {

    void YoloDetectionWriter::writeDetections(std::shared_ptr<std::vector<DataModels::YoloDetection>> detections, size_t frameNo) {

        if(!outputFile_.is_open()) {
            openFile(destinationDir_ + destinationFile_);
            if(!outputFile_.is_open()) {
                context_.logger_.warning("Unable to open " + destinationDir_ + destinationFile_ + " file");
                return;
            }
        }
        
        for (const auto& detection : *detections) {
            outputFile_ << frameNo << ", "
                        << detection.getBoundingBox().x1_ << ", "
                        << detection.getBoundingBox().y1_ << ", "
                        << detection.getBoundingBox().x2_ << ", "
                        << detection.getBoundingBox().y2_ << ", "
                        << detection.getDetectionConfidence() << ", "
                        << detection.getClassConfidence() << ", "
                        << static_cast<int>(detection.getDetectionClass()) << std::endl;
        }
    }


    void YoloDetectionWriter::writeDetectionsAsTrainData(std::shared_ptr<std::vector<DataModels::YoloDetection>> detections, size_t frameNo, int image_width, int image_height) {

        if(!detections->empty()) {

            std::ofstream outputDetFile;
            std::stringstream ss;
            ss << std::setw(10) << std::setfill('0');
            ss << frameNo << ".txt";
            outputDetFile.open(destinationDir_ + "train_ir/labels/" + ss.str());

            if (!outputDetFile.is_open()) {
                context_.logger_.warning("Unable to open file for ir yolo training labels");
                return;
            }

            for (const auto &detection : *detections) {

                int x1 = std::min(std::max(detection.getBoundingBox().x1_, 0), image_width-2);
                int y1 = std::min(std::max(detection.getBoundingBox().y1_, 0), image_width-2);
                int x2 = std::min(std::max(detection.getBoundingBox().x2_, x1+1), image_width-1);
                int y2 = std::min(std::max(detection.getBoundingBox().y2_, y1+1), image_width-1);

                float xCenter = (x1 + x2) / 2.0f;
                float yCenter = (y1 + y2) / 2.0f;
                float width = (x2 - x1);
                float height = (y2 - y1);

                outputDetFile << static_cast<size_t>(detection.getDetectionClass()) << " "
                              << xCenter / image_width << " "
                              << yCenter / image_height << " "
                              << width / image_width << " "
                              << height / image_width << std::endl;
            }
            outputDetFile.close();
        }
    }


    void YoloDetectionWriter::writeIRImageAsTrainData(std::shared_ptr<DataModels::CameraIrFrameDataModel> frame, size_t frameNo) {

        std::stringstream ss;
        ss << std::setw(10) << std::setfill('0');
        ss << frameNo << ".png";

        cv::imwrite(destinationDir_ + "train_ir/images/" + ss.str(), frame->getImage());
    }


    void YoloDetectionWriter::changeDestinationFile(std::string destinationDir, std::string destinationFile) {

        openFile(destinationDir + destinationFile);
    }


    void YoloDetectionWriter::openFile(std::string path) {

        if(outputFile_.is_open()) {
            outputFile_.close();
        }

        outputFile_.open(path,  std::ios::out);
    }
}