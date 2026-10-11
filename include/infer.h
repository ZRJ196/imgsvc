#pragma once

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

// 加载 ONNX 模型并推理
//
// 成功返回 true：
//   mask —— 输出：320*320 个 float，值域 0~1（前景概率，1 = 前景）
//
// 失败返回 false（模型加载失败或推理异常）
//
// 说明：每次调用都会重新加载模型。命令行程序只调一次无所谓，
//       但服务端必须改成"常驻对象"，否则每个请求都要重载 167MB 模型。
bool infer(const std::string& modelPath, const cv::Mat& blob, std::vector<float>& mask);
