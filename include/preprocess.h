#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// 读图并预处理成模型输入张量（命令行用）
//
// 成功返回 true：
//   orig —— 输出：原图（BGR，3 通道）
//   blob —— 输出：模型输入张量 [1,3,320,320] CV_32F，已按 ImageNet 均值/标准差归一化
bool preprocess(const std::string& imgPath, cv::Mat& blob, cv::Mat& orig);

// 从内存字节解码并预处理（服务端用：上传的图片在内存里，不用落盘）
//
// 成功返回 true；bytes 不是有效图片时返回 false
bool preprocess_bytes(const std::vector<unsigned char>& bytes, cv::Mat& blob, cv::Mat& orig);

// 直接把一张 BGR 图变成模型输入张量（不做解码）
//
// 服务端"框选裁剪"用：先把原图裁出一块，再对这块调用本函数，
// 这样框内的物体就重新变成了画面里唯一的显著目标。
bool make_blob(const cv::Mat& img, cv::Mat& blob);
