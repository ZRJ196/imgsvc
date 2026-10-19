#pragma once

#include <onnxruntime_cxx_api.h>
#include <opencv2/opencv.hpp>

#include <memory>
#include <string>
#include <vector>

// 推理引擎：模型在【构造时】加载一次，之后常驻内存
//
// 为什么是"类"而不是"函数"？
//   函数版每次调用都要重新加载模型（167 MB，实测 0.5~0.7 秒）。
//   服务端要处理成百上千个请求，模型必须常驻。
//
//   实测对比（同一台机器）：
//     每次请求都重载模型：0.730 秒/请求
//     模型常驻：          0.220 秒/请求     → 快 3.3 倍
//
// 用法：
//   InferEngine engine("models/u2net.onnx");   // 加载一次
//   if (!engine.ok()) { ... }                  // 检查是否加载成功
//   engine.run(blob, mask);                    // 每个请求只推理
class InferEngine {
public:
    explicit InferEngine(const std::string& modelPath);

    // 模型是否加载成功
    bool ok() const { return session_ != nullptr; }

    // 只做推理（不再加载模型）
    // 成功返回 true，mask 收到 320*320 个 float（值域 0~1，1 = 前景）
    bool run(const cv::Mat& blob, std::vector<float>& mask);

private:
    // 声明顺序很重要：env_ 必须比 session_ 先声明（后销毁），
    // 因为 Session 内部依赖 Env 还活着
    Ort::Env                      env_;
    Ort::SessionOptions           opts_;
    Ort::AllocatorWithDefaultOptions alloc_;
    std::unique_ptr<Ort::Session> session_;

    std::string in_name_;
    std::string out_name_;
};
