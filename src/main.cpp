#include<iostream>
#include<opencv2/opencv.hpp>
#include<string>
using namespace std;
int main(int argc,char* argv[])//argc参数个数 argv参数 argv[0]是程序名 都在命令行输入
{
    if(argc<3)
    {
        cout<<"用法："<<argv[0]<<"<输入图片><输出图片>"; //当命令输入不合法时提示
        return 1;
    }
    
    if((string)argv[1] == (string) argv[2])
    {
        cout<<"输入图片和输出图片不能相同"<<endl;
        return 1;
    }
    cv::Mat img=cv::imread(argv[1],cv::IMREAD_COLOR); //Mat表示矩阵 imread读取图像 IMREAD_COLOR强制读3通道彩色BGR

    if(img.empty())
    {
        cout<<"图片不存在或者打不开:"<<argv[1]<<endl;
        return 1;
    }
    
    cv::Mat gray;
    cv::cvtColor(img,gray,cv::COLOR_BGR2GRAY);
    
    bool ok=cv::imwrite(argv[2],gray);
    if(!ok)
    {
        cout<<"写入灰度图像失败"<<argv[2]<<endl;
        return 1;
    }
    cout<<"已保存："<<argv[2];
    cout<<" 宽="<<img.cols<<" 高="<<img.rows<<" 通道数="<<gray.channels()<<endl;
    return 0;
}