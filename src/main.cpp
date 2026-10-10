#include<iostream>
#include<cstdio>
#include<cstdlib>
#include<opencv2/opencv.hpp>
using namespace std;
int main()
{
    cv::Mat img=cv::imread("data/test.jpg",cv::IMREAD_COLOR); //Mat表示矩阵 imread读取图像 IMREAD_COLOR强制读3通道彩色
    cout<<"宽="<<img.cols<<" "<<"高="<<img.rows<<" "<<"通道数="<<img.channels()<<endl;
    
    return 0;
}