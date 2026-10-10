#include<iostream>
#include<cstdio>
#include<cstdlib>
#include<opencv2/opencv.hpp>
using namespace std;
int main(int argc,char* argv[])//argc参数个数 argv参数 argv[0]是程序名 都在命令行输入
{
    if(argc<2)
    {
        cout<<"用法："<<argv[0]<<" 图片路径"<<endl;
        return 1;
    }
    cv::Mat img=cv::imread(argv[1],cv::IMREAD_COLOR); //Mat表示矩阵 imread读取图像 IMREAD_COLOR强制读3通道彩色BGR
    if(img.empty())
    {
        cout<<"图片不存在或者打不开:"<<argv[1]<<endl;
        return 1;
    }
    cout<<"宽="<<img.cols<<" "<<"高="<<img.rows<<" "<<"通道数="<<img.channels()<<endl;

    return 0;
}