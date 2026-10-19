#include "postprocess.h"
#include <iostream>

bool make_cutout_rgba(const cv::Mat& orig, const std::vector<float>& mask, cv::Mat& rgba)
{
    const int W = 320;
    const int H = 320;

    if (orig.empty() || (int)mask.size() != W * H) {
        return false;
    }

    // 用 OpenCV 包一层（不拷贝数据，只读）
    cv::Mat maskMat(H, W, CV_32FC1, const_cast<float*>(mask.data()));

    // 放大回原图尺寸：放大用 INTER_LINEAR（缩小才用 INTER_AREA）
    cv::Mat maskFull;
    cv::resize(maskMat, maskFull, orig.size(), 0, 0, cv::INTER_LINEAR);

    // 转成 8 位灰度（0~255）
    cv::Mat mask8;
    maskFull.convertTo(mask8, CV_8U, 255.0);

    // 把掩码作为 alpha 通道，和原图合并成 BGRA
    std::vector<cv::Mat> ch;
    cv::split(orig, ch);          // BGR → 三个单通道
    ch.push_back(mask8);          // 加上 alpha
    cv::merge(ch, rgba);          // → BGRA 四通道

    return true;
}

bool saveCutout(const std::string& outPath, const cv::Mat& orig, const std::vector<float>& mask)
{
    cv::Mat rgba;
    if (!make_cutout_rgba(orig, mask, rgba)) {
        std::cout << "生成掩码图失败" << std::endl;
        return false;
    }
    if (!cv::imwrite(outPath, rgba)) {
        std::cout << "写入失败: " << outPath << std::endl;
        return false;
    }
    return true;
}

bool make_cutout_png(const cv::Mat& orig, const std::vector<float>& mask,
                     std::vector<unsigned char>& png)
{
    cv::Mat rgba;
    if (!make_cutout_rgba(orig, mask, rgba)) {
        return false;
    }
    // imencode：把 Mat 编码成内存里的 PNG 字节（不落盘）
    return cv::imencode(".png", rgba, png);
}
