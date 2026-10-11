#pragma once

#include <opencv2/opencv.hpp>
#include <string>

// 读图并预处理成模型输入张量
//
// 成功返回 true：
//   orig —— 输出：原图（BGR，3 通道）
//   blob —— 输出：模型输入张量 [1,3,320,320] CV_32F，已按 ImageNet 均值/标准差归一化
//
// 失败返回 false（图片打不开）
bool preprocess(const std::string& imgPath, cv::Mat& blob, cv::Mat& orig);
