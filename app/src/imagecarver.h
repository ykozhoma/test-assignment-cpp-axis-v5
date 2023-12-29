#ifndef IMAGE_CARVER
#define IMAGE_CARVER
#include <iomanip>
#include <iostream>
#include <vector>
#include <forward_list>

#include <thread>
#include <future>
#include <mutex>
#include <condition_variable>

#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/encoding.h>
#include <libxml/xmlwriter.h>

namespace whykozhoma
{
    struct EncodedImage
    {
        uint32_t timestampMs = 0u;
        std::string xmlString;
    };

    class ImageCarver
    {
     public:
        ImageCarver() = default;
        ImageCarver(const std::string& httpServer);

        ~ImageCarver();

     public:
        void GetImageFromCamera();
        void SendXMLViaHttpPost();

     private:
        void GetImageFromCameraAsync();
        std::string AddImageToXML(uint32_t timestampMs);

     private:
        std::string m_httpServer;

        int m_channel;
        cv::VideoCapture m_videoCapture;
        cv::Mat m_frameBuffer;
        std::mutex m_videoCaptureMtx;

        std::forward_list<EncodedImage> m_imageList;
        std::mutex m_imageListMtx;
        std::condition_variable m_imageListCV;
        bool m_imageListReady = false;
    };
}//namespace
#endif
