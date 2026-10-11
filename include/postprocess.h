#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// 把掩码变成"透明背景 PNG"
//
//   outPath —— 输出文件路径
//   orig    —— 原图（BGR，3 通道）
//   mask    —— 320*320 个 float，值域 0~1
//
// 成功返回 true
bool saveCutout(const std::string& outPath, const cv::Mat& orig,
                const std::vector<float>& mask);
