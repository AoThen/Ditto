#define OCRDLL_EXPORTS
#ifdef OCR_DEBUG
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "OcrDllInterface.h"
#include <string>
#include <cstring>
#include <vector>
#include <cstdint>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <windows.h>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include "clipper.hpp"

#ifdef OCR_DEBUG
static FILE* g_ocrLogFile = NULL;

#define OCR_LOG(fmt, ...) do { \
    if (g_ocrLogFile) { \
        fprintf(g_ocrLogFile, "[%.3f] " fmt "\n", \
            (double)GetTickCount64() / 1000.0, ##__VA_ARGS__); \
        fflush(g_ocrLogFile); \
    } \
} while(0)
#else
#define OCR_LOG(fmt, ...) ((void)0)
#endif

struct ScaleParam {
    int srcWidth, srcHeight, dstWidth, dstHeight;
    float ratioWidth, ratioHeight;
};

struct TextBox {
    std::vector<cv::Point> boxPoint;
    float score;
};

struct TextLine {
    std::string text;
    float score;
    int centerX;
    int centerY;
    int width;
    int height;
};

struct OcrHandle {
    std::shared_ptr<Ort::Env> env;
    std::shared_ptr<Ort::MemoryInfo> memInfo;
    std::shared_ptr<Ort::Session> detSession;
    std::shared_ptr<Ort::Session> recSession;
    std::string detInputName;
    std::string detOutputName;
    std::string recInputName;
    std::string recOutputName;
    std::vector<std::string> characterDict;
};

static ScaleParam GetScaleParam(cv::Mat& src, int targetSize) {
    ScaleParam sp;
    sp.srcWidth = src.cols;
    sp.srcHeight = src.rows;
    float ratio = (src.cols > src.rows) ? (float)targetSize / src.cols : (float)targetSize / src.rows;
    sp.dstWidth = (int)(src.cols * ratio);
    sp.dstHeight = (int)(src.rows * ratio);
    sp.dstWidth = (sp.dstWidth / 32) * 32;
    sp.dstHeight = (sp.dstHeight / 32) * 32;
    sp.ratioWidth = (float)sp.dstWidth / src.cols;
    sp.ratioHeight = (float)sp.dstHeight / src.rows;
    return sp;
}

static std::vector<float> SubstractMeanNormalize(cv::Mat& src, const float* meanVals, const float* normVals) {
    int imageSize = src.cols * src.rows;
    std::vector<float> inputTensorValues(src.cols * src.rows * 3);
    for (int pid = 0; pid < imageSize; pid++) {
        for (int ch = 0; ch < 3; ch++) {
            float data = (float)(src.data[pid * 3 + ch] * normVals[ch] - meanVals[ch] * normVals[ch]);
            inputTensorValues[ch * imageSize + pid] = data;
        }
    }
    return inputTensorValues;
}

static cv::Mat MakePadding(cv::Mat& src, int padding) {
    cv::Mat padded;
    cv::copyMakeBorder(src, padded, padding, padding, padding, padding, cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));
    return padded;
}

static cv::Mat GetRotateCropImage(cv::Mat& src, std::vector<cv::Point> box) {
    cv::Mat crop;
    cv::Point2f pts[4];
    for (int i = 0; i < 4; i++) pts[i] = cv::Point2f((float)box[i].x, (float)box[i].y);
    float w = (float)cv::norm(box[0] - box[1]);
    float h = (float)cv::norm(box[0] - box[3]);
    cv::Point2f dstPts[4] = { {0,0}, {w,0}, {w,h}, {0,h} };
    auto M = cv::getPerspectiveTransform(pts, dstPts);
    cv::warpPerspective(src, crop, M, cv::Size((int)w, (int)h));
    return crop;
}

static float BoxScoreFast(cv::Mat& predMat, std::vector<cv::Point>& box) {
    cv::Mat mask = cv::Mat::zeros(predMat.size(), CV_8UC1);
    std::vector<std::vector<cv::Point>> boxes = { box };
    cv::fillPoly(mask, boxes, cv::Scalar(1));
    return cv::mean(predMat, mask)[0];
}

static std::vector<cv::Point> GetMinBoxes(cv::RotatedRect& rect) {
    cv::Point2f pts[4];
    rect.points(pts);
    std::vector<cv::Point> box(4);
    std::vector<float> sums(4), diffs(4);
    for (int i = 0; i < 4; i++) {
        sums[i] = pts[i].x + pts[i].y;
        diffs[i] = pts[i].y - pts[i].x;
    }
    box[0] = pts[std::min_element(sums.begin(), sums.end()) - sums.begin()];
    box[2] = pts[std::max_element(sums.begin(), sums.end()) - sums.begin()];
    box[1] = pts[std::min_element(diffs.begin(), diffs.end()) - diffs.begin()];
    box[3] = pts[std::max_element(diffs.begin(), diffs.end()) - diffs.begin()];
    return box;
}

static std::vector<cv::Point> UnClip(std::vector<cv::Point>& box, float unClipRatio) {
    float perimeter = (float)(cv::norm(box[0] - box[1]) + cv::norm(box[1] - box[2]) +
                              cv::norm(box[2] - box[3]) + cv::norm(box[3] - box[0]));
    float area = (float)cv::contourArea(box);
    float distance = area * unClipRatio / perimeter;

    ClipperLib::Path poly;
    for (auto& pt : box) {
        poly << ClipperLib::IntPoint(pt.x, pt.y);
    }

    ClipperLib::ClipperOffset clipperOffset;
    clipperOffset.AddPath(poly, ClipperLib::jtRound, ClipperLib::etClosedPolygon);

    ClipperLib::Paths expanded;
    clipperOffset.Execute(expanded, distance);

    std::vector<cv::Point> result;
    if (!expanded.empty() && !expanded[0].empty()) {
        for (auto& pt : expanded[0]) {
            result.push_back(cv::Point((int)pt.X, (int)pt.Y));
        }
    }
    return result;
}

static std::vector<TextBox> FindRsBoxes(cv::Mat& predMat, cv::Mat& dilateMat, ScaleParam& scale, float boxScoreThresh, float unClipRatio) {
    std::vector<TextBox> rsBoxes;
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(dilateMat, contours, hierarchy, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

    for (auto& contour : contours) {
        auto rect = cv::minAreaRect(contour);
        auto box = GetMinBoxes(rect);
        float score = BoxScoreFast(predMat, box);
        if (score < boxScoreThresh) continue;

        auto clipBox = UnClip(box, unClipRatio);
        if (clipBox.size() < 4) continue;

        rect = cv::minAreaRect(clipBox);
        box = GetMinBoxes(rect);

        for (auto& pt : box) {
            pt.x = (int)(pt.x / scale.ratioWidth);
            pt.y = (int)(pt.y / scale.ratioHeight);
        }
        rsBoxes.push_back({box, score});
    }
    std::reverse(rsBoxes.begin(), rsBoxes.end());
    return rsBoxes;
}

static TextLine ScoreToTextLine(const float* outputData, size_t h, size_t w, std::vector<std::string>& characterDict) {
    std::string strRes;
    size_t lastIndex = 0;
    float totalScore = 0;
    int count = 0;

    for (size_t i = 0; i < h; i++) {
        size_t start = i * w;
        size_t stop = (i + 1) * w;

        int maxIndex = (int)(std::max_element(outputData + start, outputData + stop) - (outputData + start));
        float maxValue = *std::max_element(outputData + start, outputData + stop);

        if (maxIndex > 0 && maxIndex <= (int)characterDict.size() && !(i > 0 && maxIndex == (int)lastIndex)) {
            strRes += characterDict[maxIndex - 1];
            totalScore += maxValue;
            count++;
#ifdef OCR_DEBUG
            OCR_LOG("  rec timestep[%zu]: maxIdx=%d maxVal=%.4f char=[%s]",
                i, maxIndex, maxValue, (maxIndex > 0 && maxIndex <= (int)characterDict.size()) ? characterDict[maxIndex - 1].c_str() : "?");
#endif
        } else {
#ifdef OCR_DEBUG
            OCR_LOG("  rec timestep[%zu]: maxIdx=%d maxVal=%.4f %s",
                i, maxIndex, maxValue,
                (maxIndex == 0) ? "(blank)" :
                (maxIndex >= (int)characterDict.size()) ? "(out-of-range)" :
                "(collapsed)");
#endif
        }
        lastIndex = maxIndex;
    }
    return {strRes, count > 0 ? totalScore / count : 0};
}

static std::string LayoutTextLines(std::vector<TextLine>& lines) {
    if (lines.empty())
        return "";
    if (lines.size() == 1)
        return lines[0].text;

    const float lineThreshold = 0.3f;

    std::sort(lines.begin(), lines.end(), [](const TextLine& a, const TextLine& b) {
        int avgH = (std::min)(a.height, b.height);
        bool sameRow = (std::abs(a.centerY - b.centerY) <= (int)(avgH * lineThreshold));
        if (!sameRow)
            return a.centerY < b.centerY;
        return a.centerX < b.centerX;
    });

    std::string result;
    const TextLine* prev = nullptr;
    for (auto& line : lines) {
        if (!prev) {
            result += line.text;
        } else {
            int avgH = (std::min)(prev->height, line.height);
            bool sameRow = (std::abs(line.centerY - prev->centerY) <= (int)(avgH * lineThreshold));
            if (!sameRow) {
                result += "\n";
            } else {
                int gap = line.centerX - prev->centerX - prev->width / 2 - line.width / 2;
                if (gap > avgH * 2) {
                    result += "  ";
                } else if (gap > avgH * 0.3) {
                    result += " ";
                }
            }
            result += line.text;
        }
        prev = &line;
    }
    return result;
}

static std::vector<std::string> ParseCharacterDict(const std::string& configPath) {
    std::vector<std::string> dict;
    std::ifstream file(configPath);
    if (!file.is_open()) return dict;

    std::string line;
    bool inDict = false;
    while (std::getline(file, line)) {
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.find("character_dict:") != std::string::npos) {
            inDict = true;
            continue;
        }
        if (inDict) {
            if (line.empty() || line.find("  - ") != 0) {
                break;
            }
            std::string charStr;
            size_t start = line.find("- ");
            if (start == std::string::npos) continue;
            start += 2;
            while (start < line.size() && line[start] == ' ') start++;
            if (start >= line.size()) continue;

            if (line[start] == '\'') {
                start++;
                size_t end = line.find('\'', start);
                if (end == std::string::npos) continue;
                charStr = line.substr(start, end - start);
            } else {
                charStr = line.substr(start);
            }
            dict.push_back(charStr);
        }
    }
    return dict;
}

OCR_API void* OcrInit(const char* modelsDir)
{
    try {
#ifdef OCR_DEBUG
        g_ocrLogFile = fopen("ocr_debug.log", "w");
        OCR_LOG("OcrInit start, modelsDir=%s", modelsDir);
#endif
        std::string dir(modelsDir);
        std::string detPath = dir + "/PP-OCRv6_small_det_infer.onnx";
        std::string recPath = dir + "/PP-OCRv6_small_rec_infer.onnx";
        std::string configPath = dir + "/inference.yml";

        auto env = std::make_shared<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "DittoOCR");
        auto memInfo = std::make_shared<Ort::MemoryInfo>(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

        Ort::SessionOptions sessionOptions;
        sessionOptions.SetIntraOpNumThreads(4);
        sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_EXTENDED);

        int detLen = MultiByteToWideChar(CP_UTF8, 0, detPath.c_str(), -1, nullptr, 0);
        std::wstring detPathW(detLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, detPath.c_str(), -1, &detPathW[0], detLen);
        detPathW.resize(detLen - 1);

        int recLen = MultiByteToWideChar(CP_UTF8, 0, recPath.c_str(), -1, nullptr, 0);
        std::wstring recPathW(recLen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, recPath.c_str(), -1, &recPathW[0], recLen);
        recPathW.resize(recLen - 1);
        auto detSession = std::make_shared<Ort::Session>(*env, detPathW.c_str(), sessionOptions);
        auto recSession = std::make_shared<Ort::Session>(*env, recPathW.c_str(), sessionOptions);

        Ort::AllocatorWithDefaultOptions allocator;
        auto detInputName = std::string(detSession->GetInputNameAllocated(0, allocator).get());
        auto detOutputName = std::string(detSession->GetOutputNameAllocated(0, allocator).get());
        auto recInputName = std::string(recSession->GetInputNameAllocated(0, allocator).get());
        auto recOutputName = std::string(recSession->GetOutputNameAllocated(0, allocator).get());

        std::vector<std::string> characterDict = ParseCharacterDict(configPath);

#ifdef OCR_DEBUG
        {
            auto detInputTypeInfo = detSession->GetInputTypeInfo(0);
            auto detInputShapeInfo = detInputTypeInfo.GetTensorTypeAndShapeInfo();
            auto detInputShape = detInputShapeInfo.GetShape();
            std::string detShapeStr;
            for (size_t i = 0; i < detInputShape.size(); i++)
                detShapeStr += (i ? "," : "") + std::to_string(detInputShape[i]);
            OCR_LOG("Det model: input='%s' shape=[%s] output='%s'",
                detInputName.c_str(), detShapeStr.c_str(), detOutputName.c_str());
        }
        {
            auto recInputTypeInfo = recSession->GetInputTypeInfo(0);
            auto recInputShapeInfo = recInputTypeInfo.GetTensorTypeAndShapeInfo();
            auto recInputShape = recInputShapeInfo.GetShape();
            std::string recShapeStr;
            for (size_t i = 0; i < recInputShape.size(); i++)
                recShapeStr += (i ? "," : "") + std::to_string(recInputShape[i]);
            OCR_LOG("Rec model: input='%s' shape=[%s] output='%s'",
                recInputName.c_str(), recShapeStr.c_str(), recOutputName.c_str());
        }
        OCR_LOG("charDict: size=%zu", characterDict.size());
        for (int i = 0; i < 5 && i < (int)characterDict.size(); i++)
            OCR_LOG("  charDict[%d] = [%s]", i, characterDict[i].c_str());
        for (int i = std::max(0, (int)characterDict.size()-5); i < (int)characterDict.size(); i++)
            OCR_LOG("  charDict[%d] = [%s]", i, characterDict[i].c_str());
        for (int i = 0; i < (int)characterDict.size(); i++) {
            if (characterDict[i] == "2") {
                OCR_LOG("  char '2' at index %d", i);
                break;
            }
        }
#endif

        auto* handle = new OcrHandle();
        handle->env = env;
        handle->memInfo = memInfo;
        handle->detSession = detSession;
        handle->recSession = recSession;
        handle->detInputName = detInputName;
        handle->detOutputName = detOutputName;
        handle->recInputName = recInputName;
        handle->recOutputName = recOutputName;
        OCR_LOG("OcrInit success, charDict=%zu entries", characterDict.size());
        handle->characterDict = std::move(characterDict);

        return (void*)handle;
    } catch (const std::exception& e) {
        OCR_LOG("OcrInit exception: %s", e.what());
#ifdef OCR_DEBUG
        if (g_ocrLogFile) { fclose(g_ocrLogFile); g_ocrLogFile = NULL; }
#endif
        return nullptr;
    }
}

OCR_API char* OcrRecognize(void* handle, const unsigned char* imageData, int width, int height, int stride)
{
    try {
        OCR_LOG("OcrRecognize start, %dx%d stride=%d", width, height, stride);
        auto* ocr = (OcrHandle*)handle;

        cv::Mat bgr(height, width, CV_8UC3);
        for (int y = 0; y < height; y++) {
            const uint8_t* src = imageData + y * stride;
            uint8_t* dst = bgr.ptr(y);
            for (int x = 0; x < width; x++) {
                dst[x * 3] = src[x * 4];
                dst[x * 3 + 1] = src[x * 4 + 1];
                dst[x * 3 + 2] = src[x * 4 + 2];
            }
        }

/* *#ifdef OCR_DEBUG*/
/* *        {*/
/* *            // BMP file (no codec dependency)*/
/* *            std::ofstream f("ocr_debug_input.bmp", std::ios::binary);*/
/* *            if (f.is_open()) {*/
/* *                int w = bgr.cols, h = bgr.rows;*/
/* *                int rowSize = (w * 3 + 3) & ~3;*/
/* *                int dataSize = rowSize * h;*/
/* *                int fileSize = 14 + 40 + dataSize;*/
/* *                uint8_t hdr[14] = {'B','M', 0,0,0,0, 0,0, 0,0, 54,0,0,0};*/
/* *                memcpy(hdr + 2, &fileSize, 4);*/
/* *                uint8_t info[40] = {40,0,0,0};*/
/* *                int tmp = w; memcpy(info + 4, &tmp, 4);*/
/* *                tmp = h; memcpy(info + 8, &tmp, 4);*/
/* *                info[12] = 1; info[14] = 24;*/
/* *                f.write((char*)hdr, 14);*/
/* *                f.write((char*)info, 40);*/
/* *                for (int y = h - 1; y >= 0; y--) {*/
/* *                    f.write((char*)bgr.ptr(y), w * 3);*/
/* *                    for (int p = w * 3; p < rowSize; p++) f.put(0);*/
/* *                }*/
/* *            }*/
/* *        }*/
        cv::Vec3b p0 = bgr.at<cv::Vec3b>(0, 0);
        cv::Vec3b p1 = bgr.at<cv::Vec3b>(height-1, width-1);
        OCR_LOG("OcrRecognize image check: %dx%d stride=%d, pixel[0,0]=(%d,%d,%d), pixel[last]=(%d,%d,%d)",
            width, height, stride, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2]);
        cv::Mat padded = MakePadding(bgr, 50);

        ScaleParam scale = GetScaleParam(padded, 1024);
        cv::Mat resizeImg;
        cv::resize(padded, resizeImg, cv::Size(scale.dstWidth, scale.dstHeight));
#ifdef OCR_DEBUG
        OCR_LOG("Det resize: padded=%dx%d resize=%dx%d targetSize=1024",
            padded.cols, padded.rows, resizeImg.cols, resizeImg.rows);
#endif

        float detMean[3] = {0.485f * 255.0f, 0.456f * 255.0f, 0.406f * 255.0f};
        float detNorm[3] = {1.0f/0.229f/255.0f, 1.0f/0.224f/255.0f, 1.0f/0.225f/255.0f};
        std::vector<float> detInput = SubstractMeanNormalize(resizeImg, detMean, detNorm);
#ifdef OCR_DEBUG
        {
            double inpMin = 1e10, inpMax = -1e10;
            double inpSum[3] = {0,0,0};
            int totalPixels = scale.dstWidth * scale.dstHeight;
            for (int ch = 0; ch < 3; ch++) {
                for (int i = 0; i < totalPixels; i++) {
                    float v = detInput[ch * totalPixels + i];
                    inpMin = std::min(inpMin, (double)v);
                    inpMax = std::max(inpMax, (double)v);
                    inpSum[ch] += v;
                }
            }
            OCR_LOG("Det input: min=%.4f max=%.4f B_mean=%.4f G_mean=%.4f R_mean=%.4f",
                inpMin, inpMax,
                inpSum[0]/totalPixels, inpSum[1]/totalPixels, inpSum[2]/totalPixels);
        }
#endif

        std::vector<int64_t> detShape = {1, 3, scale.dstHeight, scale.dstWidth};
        Ort::Value detInputTensor = Ort::Value::CreateTensor<float>(*ocr->memInfo, detInput.data(), detInput.size(), detShape.data(), detShape.size());

        std::vector<const char*> detInputNames = { ocr->detInputName.c_str() };
        std::vector<const char*> detOutputNames = { ocr->detOutputName.c_str() };

        auto detOutput = ocr->detSession->Run(Ort::RunOptions(), detInputNames.data(), &detInputTensor, 1, detOutputNames.data(), 1);

        float* detOutData = detOutput[0].GetTensorMutableData<float>();
        auto detOutInfo = detOutput[0].GetTensorTypeAndShapeInfo();
        auto detOutShape = detOutInfo.GetShape();

        int outH = (detOutShape.size() == 4) ? (int)detOutShape[2] : (int)detOutShape[1];
        int outW = (detOutShape.size() == 4) ? (int)detOutShape[3] : (int)detOutShape[2];

        #ifdef OCR_DEBUG
        {
            double rawMin = 1e10, rawMax = -1e10, rawSum = 0;
            int rawCount = outH * outW;
            for (int i = 0; i < rawCount; i++) {
                float v = detOutData[i];
                rawMin = std::min(rawMin, (double)v);
                rawMax = std::max(rawMax, (double)v);
                rawSum += v;
            }
            OCR_LOG("Det raw logit: min=%.4f max=%.4f mean=%.4f",
                rawMin, rawMax, rawSum/rawCount);
        }
#endif

        cv::Mat predMat(outH, outW, CV_32FC1);
        for (int i = 0; i < outH * outW; i++) {
            predMat.at<float>(i) = detOutData[i];
        }

#ifdef OCR_DEBUG
        double minVal, maxVal;
        cv::minMaxLoc(predMat, &minVal, &maxVal);
        cv::Scalar meanVal = cv::mean(predMat);
        OCR_LOG("Det output shape=[%lld,%lld,%lld,%lld], outH=%d outW=%d",
            detOutShape[0], detOutShape[1], detOutShape[2], detOutShape[3], outH, outW);
        OCR_LOG("Det predMat sigmoid: min=%.4f max=%.4f mean=%.4f",
            minVal, maxVal, meanVal[0]);
#endif

        cv::Mat binaryMat;
        cv::threshold(predMat, binaryMat, 0.3f, 1.0f, cv::THRESH_BINARY);
        binaryMat.convertTo(binaryMat, CV_8UC1, 255.0);

        cv::Mat dilateMat;
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(2, 2));
        cv::dilate(binaryMat, dilateMat, kernel);

        std::vector<TextBox> boxes = FindRsBoxes(predMat, dilateMat, scale, 0.6f, 1.5f);

#ifdef OCR_DEBUG
        OCR_LOG("Det found %zu text boxes (score>=0.45)", boxes.size());
        for (size_t i = 0; i < boxes.size() && i < 20; i++) {
            OCR_LOG("  box[%zu]: score=%.4f [%d,%d][%d,%d][%d,%d][%d,%d]",
                i, boxes[i].score,
                boxes[i].boxPoint[0].x, boxes[i].boxPoint[0].y,
                boxes[i].boxPoint[1].x, boxes[i].boxPoint[1].y,
                boxes[i].boxPoint[2].x, boxes[i].boxPoint[2].y,
                boxes[i].boxPoint[3].x, boxes[i].boxPoint[3].y);
        }
#endif

        std::vector<TextLine> textLines;
        float recMean[3] = {127.5f, 127.5f, 127.5f};
        float recNorm[3] = {1.0f/127.5f, 1.0f/127.5f, 1.0f/127.5f};

        for (auto& box : boxes) {
            for (auto& pt : box.boxPoint) {
                pt.x -= 50;
                pt.y -= 50;
            }
            cv::Mat cropImg = GetRotateCropImage(bgr, box.boxPoint);
            if (cropImg.empty()) continue;

            int recH = 48;
            int recW = (int)((float)cropImg.cols / cropImg.rows * recH);
            recW = std::max(recW, 16);

            cv::Mat recResize;
            cv::resize(cropImg, recResize, cv::Size(recW, recH));
#ifdef OCR_DEBUG
            OCR_LOG("Rec crop: original=%dx%d resize=%dx%d", cropImg.cols, cropImg.rows, recResize.cols, recResize.rows);
#endif

            std::vector<float> recInput = SubstractMeanNormalize(recResize, recMean, recNorm);

            std::vector<int64_t> recShape = {1, 3, recH, recW};
            Ort::Value recInputTensor = Ort::Value::CreateTensor<float>(*ocr->memInfo, recInput.data(), recInput.size(), recShape.data(), recShape.size());

            std::vector<const char*> recInputNames = { ocr->recInputName.c_str() };
            std::vector<const char*> recOutputNames = { ocr->recOutputName.c_str() };

            auto recOutput = ocr->recSession->Run(Ort::RunOptions(), recInputNames.data(), &recInputTensor, 1, recOutputNames.data(), 1);

            float* recOutData = recOutput[0].GetTensorMutableData<float>();
            auto recOutInfo = recOutput[0].GetTensorTypeAndShapeInfo();
            auto recOutShape = recOutInfo.GetShape();

            size_t recH_out = (recOutShape.size() == 3) ? (size_t)recOutShape[1] : (size_t)recOutShape[0];
            size_t recW_out = (recOutShape.size() == 3) ? (size_t)recOutShape[2] : (size_t)recOutShape[1];

#ifdef OCR_DEBUG
            OCR_LOG("Rec output shape dims=%zu, seq_len=%zu num_classes=%zu",
                recOutShape.size(), recH_out, recW_out);
#endif

            TextLine textLine = ScoreToTextLine(recOutData, recH_out, recW_out, ocr->characterDict);
            textLine.centerX = (box.boxPoint[0].x + box.boxPoint[1].x + box.boxPoint[2].x + box.boxPoint[3].x) / 4;
            textLine.centerY = (box.boxPoint[0].y + box.boxPoint[1].y + box.boxPoint[2].y + box.boxPoint[3].y) / 4;
            textLine.width = (int)cv::norm(box.boxPoint[0] - box.boxPoint[1]);
            textLine.height = (int)cv::norm(box.boxPoint[0] - box.boxPoint[3]);
            textLines.push_back(textLine);
        }

        std::string result = LayoutTextLines(textLines);

        if (result.empty()) {
            OCR_LOG("OcrRecognize no text found");
            return nullptr;
        }
        char* ret = (char*)malloc(result.length() + 1);
        if (ret) memcpy(ret, result.c_str(), result.length() + 1);
        OCR_LOG("OcrRecognize success, text=%s", ret);
        return ret;

    } catch (const std::exception& e) {
        OCR_LOG("OcrRecognize exception: %s", e.what());
        return nullptr;
    }
}

OCR_API void OcrFreeString(char* str)
{
    if (str) free(str);
}

OCR_API void OcrDestroy(void* handle)
{
    if (handle) {
        delete (OcrHandle*)handle;
    }
#ifdef OCR_DEBUG
    if (g_ocrLogFile) {
        OCR_LOG("OcrDestroy");
        fclose(g_ocrLogFile);
        g_ocrLogFile = NULL;
    }
#endif
}