#include "imagecarver.h"

#include <ctime>

#include <opencv2/imgcodecs.hpp>
#include <curl/curl.h>

namespace
{
    std::string base64_encode(const std::string &s)
    {
        static const std::string base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        size_t i=0,ix=0,leng = s.length();
        std::stringstream q;
     
        for(i=0,ix=leng - leng%3; i<ix; i+=3)
        {
            q<< base64_chars[ (s[i] & 0xfc) >> 2 ];
            q<< base64_chars[ ((s[i] & 0x03) << 4) + ((s[i+1] & 0xf0) >> 4)  ];
            q<< base64_chars[ ((s[i+1] & 0x0f) << 2) + ((s[i+2] & 0xc0) >> 6)  ];
            q<< base64_chars[ s[i+2] & 0x3f ];
        }
        if (ix<leng)
        {
            q<< base64_chars[ (s[ix] & 0xfc) >> 2 ];
            q<< base64_chars[ ((s[ix] & 0x03) << 4) + (ix+1<leng ? (s[ix+1] & 0xf0) >> 4 : 0)];
            q<< (ix+1<leng ? base64_chars[ ((s[ix+1] & 0x0f) << 2) ] : '=');
            q<< '=';
        }
        return q.str();
    }

    std::string getCurrentDateTime() {
        std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
        std::time_t currentTime = std::chrono::system_clock::to_time_t(now);

        struct tm timeInfo;
        localtime_r(&currentTime, &timeInfo);

        std::ostringstream oss;
        oss << std::put_time(&timeInfo, "%Y%m%d %H%M%S");
        
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

        oss << std::setw(3) << std::setfill('0') << milliseconds;

        return oss.str();
    }

}//namespace

namespace whykozhoma
{
    ImageCarver::ImageCarver(const std::string& httpServer)
        : m_httpServer(httpServer)
        , m_channel(1)
        , m_videoCapture(m_channel)
    {
        curl_global_init(CURL_GLOBAL_ALL);

        m_videoCapture.set(cv::CAP_PROP_FPS, 30);
        m_videoCapture.set(cv::CAP_PROP_FRAME_WIDTH,  640);
        m_videoCapture.set(cv::CAP_PROP_FRAME_HEIGHT, 360);

        m_videoCapture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('N','V','1','2'));
        m_videoCapture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('N','V','2','1'));
        m_videoCapture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('R','G','B','3'));

        m_videoCapture.set(cv::CAP_PROP_FOURCC, cv::VideoWriter::fourcc('Y','8','0','0'));
        try { m_videoCapture.grab(); }
        catch(const std::exception& e)
        {
            curl_global_cleanup();
            std::cerr << "Failed to open stream: " << e.what() << std::endl;
            throw;
        }
    }

    ImageCarver::~ImageCarver()
    {
         curl_global_cleanup();
         m_imageList.clear();
    }
    
    void ImageCarver::GetImageFromCamera()
    {
        GetImageFromCameraAsync();
    }

    void ImageCarver::GetImageFromCameraAsync()
    {
        auto future = std::async(std::launch::async, [this]()
            {
                uint32_t timestampMs = 0u;
                std::string xmlString;
                {
                    std::lock_guard<std::mutex> lock(m_videoCaptureMtx);
                    m_videoCapture.read(m_frameBuffer);

                    if (m_frameBuffer.empty()) {
                        curl_global_cleanup();
                        throw std::runtime_error("Failed to fetch frame.");
                    }

                    timestampMs = m_videoCapture.get(cv::CAP_PROP_POS_MSEC);
                    xmlString = AddImageToXML(timestampMs);
                }

                std::unique_lock lock(m_imageListMtx);
                m_imageList.push_front({timestampMs, std::move(xmlString)});
                lock.unlock();
                m_imageListCV.notify_one();
            }
            );
    }

    std::string ImageCarver::AddImageToXML(uint32_t timestampMs)
    {
        xmlDocPtr doc = xmlNewDoc(BAD_CAST "1.0");
        xmlNodePtr imageDataNode = xmlNewNode(nullptr, BAD_CAST "ImageData");
        xmlDocSetRootElement(doc, imageDataNode);

        xmlNodePtr timestampNode = xmlNewChild(imageDataNode, nullptr, BAD_CAST "Timestamp",
        BAD_CAST std::to_string(timestampMs).c_str());
        xmlNodePtr dateTimeNode = xmlNewChild(imageDataNode, nullptr, BAD_CAST "DateTime", BAD_CAST
        getCurrentDateTime().c_str());

        xmlTextWriterPtr base64Writer = xmlNewTextWriterTree( doc, imageDataNode, 0 );
        xmlTextWriterStartElement( base64Writer, (xmlChar*) "ImageBase64" );
        
        std::vector<uchar> tmpBuffer;
        cv::imencode(".jpg", m_frameBuffer, tmpBuffer);
        std::string base64Data = base64_encode(std::string(tmpBuffer.begin(), tmpBuffer.end()));

        xmlTextWriterWriteBase64(base64Writer,
          base64Data.c_str(), 0, base64Data.length() );

        xmlTextWriterEndElement( base64Writer );
        xmlFreeTextWriter(base64Writer);

        int bufferSize = 0;

        xmlChar * xmlBuffer;

        xmlDocDumpFormatMemory(doc, &xmlBuffer, &bufferSize, 1);
        std::string xmlString(reinterpret_cast<const char*>(xmlBuffer));

        xmlFree(xmlBuffer);
        xmlFreeDoc(doc);
        xmlCleanupParser();

        return xmlString;
    }

    void ImageCarver::SendXMLViaHttpPost()
    {
        std::unique_lock lock(m_imageListMtx);
        m_imageListCV.wait(lock, [this]{ return !m_imageList.empty(); });

        CURL *curl = nullptr;

        curl = curl_easy_init();
        if(curl)
        {
            auto image = m_imageList.front();
            curl_easy_setopt(curl, CURLOPT_URL, m_httpServer.c_str());
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, image.xmlString.c_str());

            curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/xml");
            headers = curl_slist_append(headers, "Accept-Encoding: base64");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

            CURLcode res = curl_easy_perform(curl);
            if(res != CURLE_OK)
            {
                curl_easy_cleanup(curl);
                curl_slist_free_all(headers);
                curl_global_cleanup();

                throw std::runtime_error(
                std::string("curl_easy_perform() failed: ") + curl_easy_strerror(res)
                + m_httpServer);
            }

            m_imageList.pop_front();

            curl_easy_cleanup(curl);
            curl_slist_free_all(headers);
        }
    }
} //namespace
