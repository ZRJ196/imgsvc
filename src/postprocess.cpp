#include "postprocess.h"
#include <iostream>

bool make_cutout_rgba(const cv::Mat& orig, const std::vector<float>& mask,
                      cv::Mat& rgba, const cv::Rect& roi)
{
    const int MW = 320;
    const int MH = 320;

    if (orig.empty() || (int)mask.size() != MW * MH) {
        return false;
    }

    // 确定掩码覆盖的区域：默认整张图
    // 非法 roi（宽高<=0、越界）一律退回整张图，避免 orig(roi) 抛异常
    cv::Rect r = roi;
    if (r.width <= 0 || r.height <= 0 ||
        r.x < 0 || r.y < 0 ||
        r.x + r.width > orig.cols || r.y + r.height > orig.rows) {
        r = cv::Rect(0, 0, orig.cols, orig.rows);
    }

    // 用 OpenCV 包一层（不拷贝数据，只读）
    cv::Mat maskMat(MH, MW, CV_32FC1, const_cast<float*>(mask.data()));

    // 放大到 roi 的尺寸（放大用 INTER_LINEAR，缩小才用 INTER_AREA）
    cv::Mat maskRoi;
    cv::resize(maskMat, maskRoi, r.size(), 0, 0, cv::INTER_LINEAR);

    // 转成 8 位灰度（0~255）
    cv::Mat mask8;
    maskRoi.convertTo(mask8, CV_8U, 255.0);

    // ★ 关键：先建一张全透明的 alpha，只把 roi 那块填进去
    //   这样输出和原图同尺寸，roi 以外的区域全透明
    cv::Mat alpha(orig.size(), CV_8U, cv::Scalar(0));
    mask8.copyTo(alpha(r));

    // 把 alpha 作为第 4 通道合并成 BGRA
    std::vector<cv::Mat> ch;
    cv::split(orig, ch);
    ch.push_back(alpha);
    cv::merge(ch, rgba);

    return true;
}

bool saveCutout(const std::string& outPath, const cv::Mat& orig,
                const std::vector<float>& mask, const cv::Rect& roi)
{
    cv::Mat rgba;
    if (!make_cutout_rgba(orig, mask, rgba, roi)) {
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
                     std::vector<unsigned char>& png, const cv::Rect& roi)
{
    cv::Mat rgba;
    if (!make_cutout_rgba(orig, mask, rgba, roi)) {
        return false;
    }
    // imencode：把 Mat 编码成内存里的 PNG 字节（不落盘）
    return cv::imencode(".png", rgba, png);
}
