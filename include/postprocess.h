#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// 掩码 → 透明背景 RGBA 图
//
//   orig —— 原图（BGR，3 通道）
//   mask —— 320*320 个 float，值域 0~1
//   roi  —— 掩码对应的原图区域（框选裁剪时用）。
//           传 cv::Rect()（默认）表示掩码覆盖整张图。
//           传有效矩形时：掩码只贴到这块，其余区域全透明，输出仍是【原图尺寸】。
bool make_cutout_rgba(const cv::Mat& orig, const std::vector<float>& mask,
                      cv::Mat& rgba, const cv::Rect& roi = cv::Rect());

// 掩码 → 透明背景 PNG 文件（命令行用）
bool saveCutout(const std::string& outPath, const cv::Mat& orig,
                const std::vector<float>& mask, const cv::Rect& roi = cv::Rect());

// 掩码 → 内存里的 PNG 字节（服务端用：直接把图片发给浏览器，不落盘）
bool make_cutout_png(const cv::Mat& orig, const std::vector<float>& mask,
                     std::vector<unsigned char>& png, const cv::Rect& roi = cv::Rect());
