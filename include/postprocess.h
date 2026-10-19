#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// 掩码 → 透明背景 RGBA 图（下面两个函数的公共实现）
//   orig —— 原图（BGR，3 通道）
//   mask —— 320*320 个 float，值域 0~1
bool make_cutout_rgba(const cv::Mat& orig, const std::vector<float>& mask, cv::Mat& rgba);

// 掩码 → 透明背景 PNG 文件（命令行用）
bool saveCutout(const std::string& outPath, const cv::Mat& orig, const std::vector<float>& mask);

// 掩码 → 内存里的 PNG 字节（服务端用：直接把图片发给浏览器，不落盘）
bool make_cutout_png(const cv::Mat& orig, const std::vector<float>& mask,
                     std::vector<unsigned char>& png);
