#include "preprocess.h"
#include <iostream>

// 把一张 BGR 图变成模型输入张量
//   blobFromImage：缩放 + BGR→RGB + /255 + HWC→NCHW
//   再按通道减均值、除标准差
//
// blob 是 NCHW 布局：内存里依次是 [通道0 的全部像素][通道1][通道2]
// 所以"通道 c 的第 i 个像素"在内存中的下标是 c * HW + i
bool make_blob(const cv::Mat& img, cv::Mat& blob)
{
    if (img.empty()) {
        return false;
    }

    // U²-Net 训练时就是直接拉伸到 320x320（不保持宽高比）
    blob = cv::dnn::blobFromImage(img, 1.0 / 255.0, cv::Size(320, 320),
                                  cv::Scalar(), true, false, CV_32F);

    const float mean[3] = {0.485f, 0.456f, 0.406f};   // RGB 顺序
    const float stdv[3] = {0.229f, 0.224f, 0.225f};
    const int HW = 320 * 320;

    float* p = blob.ptr<float>();
    for (int c = 0; c < 3; ++c) {
        for (int i = 0; i < HW; ++i) {
            p[c * HW + i] = (p[c * HW + i] - mean[c]) / stdv[c];
        }
    }

    return true;
}

bool preprocess(const std::string& imgPath, cv::Mat& blob, cv::Mat& orig)
{
    orig = cv::imread(imgPath, cv::IMREAD_COLOR);
    if (orig.empty()) {
        std::cout << "图片不存在或者打不开: " << imgPath << std::endl;
        return false;
    }
    return make_blob(orig, blob);
}

bool preprocess_bytes(const std::vector<unsigned char>& bytes, cv::Mat& blob, cv::Mat& orig)
{
    if (bytes.empty()) {
        return false;
    }

    // imdecode：直接从内存里的字节解码成 Mat（和 imread 一样返回 BGR 三通道）
    orig = cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (orig.empty()) {
        return false;                       // 不是有效的图片格式
    }
    return make_blob(orig, blob);
}
