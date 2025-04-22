// Copyright (c) 2024 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <chrono>  
#include <vector> 
#include <dirent.h> 
#include "yolov8-pose.h"
#include "image_utils.h"
#include "file_utils.h"
#include "image_drawing.h"
#include <sys/types.h>  
#include <unistd.h> 
#include <cstring> 
#include <opencv2/opencv.hpp>  
// int skeleton[38] ={16, 14, 14, 12, 17, 15, 15, 13, 12, 13, 6, 12, 7, 13, 6, 7, 6, 8, 
//             7, 9, 8, 10, 9, 11, 2, 3, 1, 2, 1, 3, 2, 4, 3, 5, 4, 6, 5, 7}; 

// 修改为适配2个关键点的连接（例如无连接或自定义）：
int skeleton[2] = {0,1}; // 若无连接需求



int read_image_opencv(const char* path, image_buffer_t* image) 
{  
    // 使用 OpenCV 读取图像  
    cv::Mat cv_img = cv::imread(path,cv::IMREAD_COLOR);  
    if (cv_img.empty()) 
    {  
        printf("error: read image %s fail\n", path);  
        return -1;  
    }  


    // 确定图像格式和通道数  
    int channels = cv_img.channels();  
    image->format = (channels == 4) ? IMAGE_FORMAT_RGBA8888 :  
                    (channels == 1) ? IMAGE_FORMAT_GRAY8 :  
                                      IMAGE_FORMAT_RGB888;  
  
    // 设置图像宽度和高度  
    image->width = cv_img.cols;  
    image->height = cv_img.rows;  
  
    // 分配内存并复制图像数据  
    int size = cv_img.total() * channels;  
    if (image->virt_addr != NULL) 
    {  
        // 如果 image->virt_addr 已经分配了内存，则复制数据到该内存  
        memcpy(image->virt_addr, cv_img.data, size);  
    } 
    else 
    {  
        // 否则，分配新内存  
        image->virt_addr = (unsigned char *)malloc(size); 
        if (image->virt_addr == NULL) {  
            printf("error: memory allocation fail\n");  
            return -1;  
        }  
        memcpy(image->virt_addr, cv_img.data, size);  
    }  

    if (channels == 4) 
    {  
        cv::Mat rgb_img;  
        cv::cvtColor(cv_img, rgb_img, cv::COLOR_RGBA2RGB);  
        memcpy(image->virt_addr, rgb_img.data, rgb_img.total() * 3);  
        // 更新大小（去掉 A 通道后的新大小）  
        size = rgb_img.total() * 3;  
    }  
  
    return 0;  
}


int write_image(const char* path, const image_buffer_t* img) {  
    int width = img->width;  
    int height = img->height;  
    int channels = (img->format == IMAGE_FORMAT_RGB888) ? 3 :   
                   (img->format == IMAGE_FORMAT_GRAY8) ? 1 :   
                   4; // 根据image_buffer_t中的format字段确定通道数  
    void* data = img->virt_addr;  
  
    // 假设图像数据是连续的，且每个通道的数据类型是8位无符号整数  
    cv::Mat cv_img(height, width, CV_8UC(channels), data);  
  
    // 如果通道数为3且图像格式是BGR（因为OpenCV默认使用BGR），则需要转换为RGB以正确保存  
    if (channels == 3 && img->format != IMAGE_FORMAT_RGB888) { // 假设IMAGE_FORMAT_BGR888表示BGR格式  
        cv::Mat rgb_img;  
        cv::cvtColor(cv_img, rgb_img, cv::COLOR_BGR2RGB);  
        bool success = cv::imwrite(path, rgb_img);  
        return success ? 0 : -1;  
    }  
  
    // 如果不是3通道，或者图像格式已经是BGR（这里假设你的IMAGE_FORMAT_BGR888表示BGR），则直接保存  
    bool success = cv::imwrite(path, cv_img);  
    return success ? 0 : -1; // 成功返回0，失败返回-1  
}

//去除文件地址&后缀
std::string extractFileNameWithoutExtension(const std::string& path) 
{  
    auto pos = path.find_last_of("/\\");  
    std::string filename = (pos == std::string::npos) ? path : path.substr(pos + 1);  
      
    // 查找并去除文件后缀  
    pos = filename.find_last_of(".");  
    if (pos != std::string::npos) {  
        filename = filename.substr(0, pos);  
    }  
      
    return filename;  
}


// 处理一个文件夹中的所有图像文件  
void processImagesInFolder(const std::string& folderPath, rknn_app_context_t* rknn_app_ctx, const std::string& outputFolderPath) 
{  
    DIR *dir = opendir(folderPath.c_str());  
    if (dir == nullptr) {  
        perror("opendir");  
        return;  
    }  
  
    struct dirent *entry;  
    while ((entry = readdir(dir)) != nullptr) 
    {  
        std::string fileName = entry->d_name;  
        std::string fullPath = folderPath + "/" + fileName;  
         // 检查文件扩展名  
        if ((fileName.size() >= 4 && strcmp(fileName.c_str() + fileName.size() - 4, ".jpg") == 0) ||  
            (fileName.size() >= 5 && strcmp(fileName.c_str() + fileName.size() - 5, ".jpeg") == 0) ||  
            (fileName.size() >= 4 && strcmp(fileName.c_str() + fileName.size() - 4, ".png") == 0)) 
            {  
  
                std::string outputFileName = outputFolderPath + "/" + extractFileNameWithoutExtension(fullPath) + "_out.png";  
    
                int ret;  
                image_buffer_t src_image;  
                memset(&src_image, 0, sizeof(image_buffer_t));  
                ret = read_image_opencv(fullPath.c_str(), &src_image); // 使用 OpenCV 读取图像
                if (ret != 0) 
                {  
                    printf("read image fail! ret=%d image_path=%s\n", ret, fullPath.c_str());  
                    continue;  
                }  
    
                object_detect_result_list od_results;  
                
                auto start4= std::chrono::high_resolution_clock::now(); // 开始时间戳
                ret = inference_yolov8_pose_model(rknn_app_ctx, &src_image, &od_results);  
                if (ret != 0) 
                {  
                    printf("inference_yolov8_model fail! ret=%d\n", ret);  
                    if (src_image.virt_addr != NULL) 
                    {  
                        free(src_image.virt_addr);  
                    }  
                    continue;  
                } 
                auto end4 = std::chrono::high_resolution_clock::now(); // 结束时间戳 
                std::chrono::duration<double, std::milli> elapsed4 = end4 - start4; // 计算经过的时间（毫秒）
                std::cout << "------------------------------------------------------------------------inference_yolov8_model:" << elapsed4.count() << " ms\n"; // 打印经过的时间

                // 画框和概率
                char text[256];
                for (int i = 0; i < od_results.count; i++)
                {
                    object_detect_result *det_result = &(od_results.results[i]);
                    printf("%s @ (%d %d %d %d) %.3f\n", coco_cls_to_name(det_result->cls_id),
                        det_result->box.left, det_result->box.top,
                        det_result->box.right, det_result->box.bottom,
                        det_result->prop);
                    int x1 = det_result->box.left;
                    int y1 = det_result->box.top;
                    int x2 = det_result->box.right;
                    int y2 = det_result->box.bottom;
                    draw_rectangle(&src_image, x1, y1, x2 - x1, y2 - y1, COLOR_BLUE, 3);
                    sprintf(text, "%s %.1f%%", coco_cls_to_name(det_result->cls_id), det_result->prop * 100);
                    draw_text(&src_image, text, x1, y1 - 20, COLOR_RED, 10);

                
                    // 直接绘制关键点0到1的连线，无需循环
                    if (od_results.count > 0) 
                    {
                        object_detect_result *det_result = &(od_results.results[0]);
                        
                        // 打印关键点坐标以验证数据有效性
                        std::cout << "关键点0坐标: (" << det_result->keypoints[0][0] << ", " << det_result->keypoints[0][1] << ")" << std::endl;
                        std::cout << "关键点1坐标: (" << det_result->keypoints[1][0] << ", " << det_result->keypoints[1][1] << ")" << std::endl;

                        // 绘制连线
                        draw_line(
                            &src_image,
                            (int)(det_result->keypoints[0][0]),
                            (int)(det_result->keypoints[0][1]),
                            (int)(det_result->keypoints[1][0]),
                            (int)(det_result->keypoints[1][1]),
                            COLOR_GREEN, 3
                        );
                        // 绘制关键点（红色和黄色）
                        draw_circle(&src_image, 
                            (int)(det_result->keypoints[0][0]), 
                            (int)(det_result->keypoints[0][1]), 
                            4,  // 增大半径到4
                            COLOR_RED, 
                            -1);  // 线宽增加

                        draw_circle(&src_image, 
                            (int)(det_result->keypoints[1][0]), 
                            (int)(det_result->keypoints[1][1]), 
                            4, 
                            COLOR_YELLOW, 
                            -1);
                    }
                    std::cout << "连线完成" << std::endl;
                }  


            write_image(outputFileName.c_str(), &src_image);  
  
            if (src_image.virt_addr != NULL) 
            {  
                free(src_image.virt_addr);  
            }  
        }  
    }  
  
    closedir(dir);  
}  

/*-------------------------------------------
                  Main Function
-------------------------------------------*/
int main(int argc, char **argv)
{
    auto start= std::chrono::high_resolution_clock::now(); // 开始时间戳
    const std::string modelPath = "//home/firefly/GitHUb测试/yolov8pose/model/4_8_knob_pose_head_tail_best.rknn";  
    const std::string imageFolder = "/home/firefly/GitHUb测试/yolov8pose/inputimage";  
    const std::string outputFolder = "/home/firefly/GitHUb测试/yolov8pose/outputimage";

    int ret;
    rknn_app_context_t rknn_app_ctx;
    memset(&rknn_app_ctx, 0, sizeof(rknn_app_context_t));

    init_post_process();

    ret = init_yolov8_pose_model(modelPath.c_str(), &rknn_app_ctx);
    if (ret != 0) 
    {  
        printf("init_yolov8_model fail! ret=%d model_path=%s\n", ret, modelPath.c_str());  
        return -1;  
    } 

    std::cout<<"模型初始化完成"<<std::endl;

    processImagesInFolder(imageFolder, &rknn_app_ctx, outputFolder); 

    ret = release_yolov8_pose_model(&rknn_app_ctx);
    if (ret != 0) 
    {  
        printf("release_yolov8_model fail! ret=%d\n", ret);  
    }  

    deinit_post_process();

    auto end = std::chrono::high_resolution_clock::now(); // 结束时间戳 
    std::chrono::duration<double, std::milli> elapsed = end- start; // 计算经过的时间（毫秒）
    std::cout << "------------------------------------------------------------------------ALL time:" << elapsed.count() << " ms\n"; // 打印经过的时间
    return 0;  
}
