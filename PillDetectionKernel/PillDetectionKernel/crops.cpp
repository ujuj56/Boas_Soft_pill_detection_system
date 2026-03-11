#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#include <windows.h>
#endif

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdlib>
#include <stdexcept>
#include <chrono>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

// GPU 설정 헤더 (AMD 매크로 정의를 위해 먼저 포함)
#include "myHeader.h"

// ONNX Runtime headers (for YOLO model inference)
// Install: vcpkg install onnxruntime or download directly
#include <onnxruntime_cxx_api.h>
#include <onnxruntime_c_api.h>
#if AMD == 1 && defined(_WIN32)
#include <dml_provider_factory.h>  // DirectML provider API
#endif

namespace fs = std::filesystem;
using namespace std;
using namespace std;

// Detection result structure
struct Detection {
    float x1, y1, x2, y2;
    float confidence;
    int class_id;
};

class PillAugmentationSystem {
private:
    string folder_b_path;
    string folder_c_path;
    string empty_tray_path;
    string yolo_model_path;
    cv::Mat tray_image;

    // ONNX Runtime related
    Ort::Env ort_env;
    Ort::Session* ort_session;
    Ort::AllocatorWithDefaultOptions allocator;
    vector<string> input_name_strings;
    vector<string> output_name_strings;
    vector<const char*> input_names;
    vector<const char*> output_names;
    vector<vector<int64_t>> input_shapes;
    vector<vector<int64_t>> output_shapes;
    bool model_loaded;

    // YOLO model preprocessing/postprocessing
    cv::Size model_input_size;
    float conf_threshold;
    float nms_threshold;

public:
    PillAugmentationSystem(const string& folder_b_path,
        const string& folder_c_path,
        const string& empty_tray_path,
        const string& yolo_model_path)
        : folder_b_path(folder_b_path),
        folder_c_path(folder_c_path),
        empty_tray_path(empty_tray_path),
        yolo_model_path(yolo_model_path),
        ort_env(ORT_LOGGING_LEVEL_FATAL, "PillDetection"),  // FATAL 레벨로 변경하여 대부분의 로그 억제
        ort_session(nullptr),
        model_loaded(false),
        model_input_size(640, 640),
        conf_threshold(0.25f),
        nms_threshold(0.45f) {

        // Load empty tray image (not used in crop function but loaded for initialization)

        tray_image = cv::imread(empty_tray_path);

        if (tray_image.empty()) {
            cout << "Warning: Failed to load empty tray image: " << empty_tray_path
                << " (no effect on crop function)" << endl;
        }

        //LogToFile("PATH", "yolo_model_path=" + yolo_model_path);
        // Load YOLO model
        if (!yolo_model_path.empty() && fs::exists(yolo_model_path)) {

            try {
                LoadYOLOModel(yolo_model_path);
                cout << "YOLO model loaded successfully: " << yolo_model_path << endl;
            }
            catch (const exception& e) {
                cout << "YOLO model loading failed: " << e.what() << endl;
                model_loaded = false;
            }
        }
        else {
            cout << "YOLO model not provided or path is invalid." << endl;
            throw runtime_error("YOLO model path is required.");
        }

        // Create output folder
        fs::create_directories(folder_c_path);
    }

    ~PillAugmentationSystem() {
        if (ort_session) {
            delete ort_session;
        }
    }

private:
    void LoadYOLOModel(const string& model_path) {
        // Check if .pt file is provided, convert to .onnx path
        string actual_model_path = model_path;
        if (model_path.size() > 3 && model_path.substr(model_path.size() - 3) == ".pt") {
            // Replace .pt with .onnx
            actual_model_path = model_path.substr(0, model_path.size() - 3) + ".onnx";
            cout << "Note: .pt file detected. Looking for ONNX model: " << actual_model_path << endl;

            if (!fs::exists(actual_model_path)) {
                cout << "Error: ONNX model not found: " << actual_model_path << endl;
                cout << "Please convert your .pt model to .onnx format using:" << endl;
                cout << "  python convert_to_onnx.py" << endl;
                throw runtime_error("ONNX model file not found. Please convert .pt to .onnx first.");
            }
        }

        // ONNX Runtime session options
        Ort::SessionOptions session_options;
        // Use default thread count (0 = auto-detect) for better CPU performance
        // GPU (DirectML) will ignore this setting
        session_options.SetIntraOpNumThreads(0);  // 0 = use all available cores
        session_options.SetInterOpNumThreads(0);  // 0 = use all available cores for parallel execution
        session_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        // Enable memory pattern optimization for better performance
        session_options.EnableMemPattern();
        // Enable CPU memory arena for better memory management
        session_options.EnableCpuMemArena();

        // GPU 설정에 따라 Execution Provider 추가
        // AMD = 0: MX150 (CUDA GPU 가속)
        // AMD = 1: AMD 780M (DirectML GPU 가속)
#if AMD == 0
        // MX150: CUDA provider 시도
        try {
            OrtCUDAProviderOptions cuda_options{};
            cuda_options.device_id = 0;
            session_options.AppendExecutionProvider_CUDA(cuda_options);
            cout << "CUDA provider enabled (MX150 GPU acceleration)" << endl;
        }
        catch (const exception& e) {
            cout << "Warning: Failed to enable CUDA provider: " << e.what() << endl;
            cout << "Falling back to CPU execution." << endl;
        }
#elif AMD == 1 && defined(_WIN32)
        // AMD 780M: DirectML provider 시도
        try {
            // Get DirectML API using OrtApi
            const OrtDmlApi* ort_dml_api = nullptr;
            OrtStatus* status = Ort::GetApi().GetExecutionProviderApi("DML", ORT_API_VERSION, (const void**)&ort_dml_api);

            if (status == nullptr && ort_dml_api != nullptr) {
                // Ort::SessionOptions has implicit conversion to OrtSessionOptions*
                // Use default GPU (device_id = 0)
                OrtStatus* dml_status = ort_dml_api->SessionOptionsAppendExecutionProvider_DML(session_options, 0);
                if (dml_status == nullptr) {
                    cout << "DirectML provider enabled (AMD 780M GPU acceleration)" << endl;
                }
                else {
                    const char* error_msg = Ort::GetApi().GetErrorMessage(dml_status);
                    cout << "Warning: Failed to enable DirectML provider: " << (error_msg ? error_msg : "Unknown error") << endl;
                    Ort::GetApi().ReleaseStatus(dml_status);
                    cout << "Falling back to CPU execution." << endl;
                }
            }
            else {
                if (status != nullptr) {
                    const char* error_msg = Ort::GetApi().GetErrorMessage(status);
                    cout << "Warning: DirectML API not available: " << (error_msg ? error_msg : "Unknown error") << endl;
                    Ort::GetApi().ReleaseStatus(status);
                }
                else {
                    cout << "Warning: DirectML API not available. Using CPU." << endl;
                }
            }
        }
        catch (const exception& e) {
            cout << "Warning: Failed to enable DirectML provider: " << e.what() << endl;
            cout << "Falling back to CPU execution." << endl;
        }
#else
        cout << "Using CPU mode" << endl;
#endif

        // Load model
        // Handle ORTCHAR_T as wchar_t on Windows
#ifdef _WIN32
        // Windows: Convert string to wstring
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, actual_model_path.c_str(), -1, NULL, 0);
        wstring wmodel_path(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, actual_model_path.c_str(), -1, &wmodel_path[0], size_needed);
        ort_session = new Ort::Session(ort_env, wmodel_path.c_str(), session_options);
#else
        // Linux/Mac: Use char* directly
        ort_session = new Ort::Session(ort_env, actual_model_path.c_str(), session_options);
#endif

        // Get input/output information
        size_t num_input_nodes = ort_session->GetInputCount();
        size_t num_output_nodes = ort_session->GetOutputCount();

        input_name_strings.clear();
        output_name_strings.clear();
        input_names.clear();
        output_names.clear();
        input_shapes.clear();
        output_shapes.clear();

        // Input information
        input_name_strings = ort_session->GetInputNames();
        for (size_t i = 0; i < input_name_strings.size(); i++) {
            input_names.push_back(input_name_strings[i].c_str());

            auto input_type_info = ort_session->GetInputTypeInfo(i);
            auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            input_shapes.push_back(shape);
        }

        // Output information
        output_name_strings = ort_session->GetOutputNames();
        for (size_t i = 0; i < output_name_strings.size(); i++) {
            output_names.push_back(output_name_strings[i].c_str());

            auto output_type_info = ort_session->GetOutputTypeInfo(i);
            auto tensor_info = output_type_info.GetTensorTypeAndShapeInfo();
            auto shape = tensor_info.GetShape();
            output_shapes.push_back(shape);

            // Print output shape for debugging
            cout << "Output " << i << " name: " << output_name_strings[i] << ", shape: [";
            for (size_t j = 0; j < shape.size(); j++) {
                cout << shape[j];
                if (j < shape.size() - 1) cout << ", ";
            }
            cout << "]" << endl;
        }

        model_loaded = true;
    }

    // Calculate IoU (Intersection over Union) between two bounding boxes
    float CalculateIoU(const Detection& box1, const Detection& box2) {
        float x1 = max(box1.x1, box2.x1);
        float y1 = max(box1.y1, box2.y1);
        float x2 = min(box1.x2, box2.x2);
        float y2 = min(box1.y2, box2.y2);

        float intersection = max(0.0f, x2 - x1) * max(0.0f, y2 - y1);
        float area1 = (box1.x2 - box1.x1) * (box1.y2 - box1.y1);
        float area2 = (box2.x2 - box2.x1) * (box2.y2 - box2.y1);
        float union_area = area1 + area2 - intersection;

        return (union_area > 0.0f) ? (intersection / union_area) : 0.0f;
    }

    // Apply Non-Maximum Suppression (NMS) to remove overlapping detections
    // Optimized version with memory pre-allocation
    vector<Detection> ApplyNMS(vector<Detection>& detections, float iou_threshold = 0.45f) {
        if (detections.empty()) {
            return {};
        }

        // Sort detections by confidence (descending)
        sort(detections.begin(), detections.end(),
            [](const Detection& a, const Detection& b) {
                return a.confidence > b.confidence;
            });

        vector<Detection> result;
        result.reserve(detections.size());  // Pre-allocate memory to avoid reallocations
        vector<bool> suppressed(detections.size(), false);

        for (size_t i = 0; i < detections.size(); i++) {
            if (suppressed[i]) {
                continue;
            }

            result.push_back(detections[i]);

            // Suppress overlapping detections
            for (size_t j = i + 1; j < detections.size(); j++) {
                if (suppressed[j]) {
                    continue;
                }

                // Only suppress if same class
                if (detections[i].class_id == detections[j].class_id) {
                    float iou = CalculateIoU(detections[i], detections[j]);
                    if (iou > iou_threshold) {
                        suppressed[j] = true;
                    }
                }
            }
        }

        return result;
    }

    vector<Detection> YOLODetectXYXY(const cv::Mat& image_bgr, float conf_thres = 0.25f) {
        if (!model_loaded || !ort_session) {
            return {};
        }

        try {
            // Image preprocessing
            // Note: ultralytics YOLO uses letterbox padding by default, but for simplicity
            // we use simple resize to match the model input size
            cv::Mat resized, blob;
            cv::resize(image_bgr, resized, model_input_size, 0, 0, cv::INTER_LINEAR);
            cv::dnn::blobFromImage(resized, blob, 1.0 / 255.0, model_input_size, cv::Scalar(), true, false, CV_32F);

            // Create input tensor
            vector<int64_t> input_shape = { 1, 3, model_input_size.height, model_input_size.width };
            size_t input_tensor_size = 1 * 3 * model_input_size.height * model_input_size.width;
            vector<float> input_tensor_values(input_tensor_size);
            memcpy(input_tensor_values.data(), blob.data, input_tensor_size * sizeof(float));

            Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
            Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
                memory_info, input_tensor_values.data(), input_tensor_size,
                input_shape.data(), input_shape.size());

            // Run inference
            Ort::RunOptions run_options;
            vector<Ort::Value> output_tensors = ort_session->Run(
                run_options,
                input_names.data(), &input_tensor, 1,
                output_names.data(), output_names.size());

            // Parse output
            float* output_data = output_tensors[0].GetTensorMutableData<float>();
            auto output_shape = output_tensors[0].GetTensorTypeAndShapeInfo().GetShape();

            // YOLO output format detection
            // YOLOv5/v7: [batch, num_detections, 85] where 85 = (x, y, w, h, conf, 80 class_scores)
            // YOLOv8: [batch, num_detections, 84] where 84 = (x, y, w, h, 80 class_scores) - no separate conf
            // Single class or objectness: [batch, features, num_detections] where features = 5 (x, y, w, h, conf)

            int batch_size = output_shape[0];
            int num_detections = 0;
            int num_features = 0;
            bool features_first = false;  // Whether format is [batch, features, detections] vs [batch, detections, features]

            if (output_shape.size() == 3) {
                // [batch, dim1, dim2] format
                // Determine which dimension is features and which is detections
                // Usually: features < detections (e.g., 5 < 8400, 84 < 8400, 85 < 8400)
                if (output_shape[1] < output_shape[2]) {
                    // [batch, features, num_detections] - transposed format
                    num_features = output_shape[1];
                    num_detections = output_shape[2];
                    features_first = true;
                }
                else {
                    // [batch, num_detections, features] - normal format
                    num_detections = output_shape[1];
                    num_features = output_shape[2];
                    features_first = false;
                }
            }
            else if (output_shape.size() == 2) {
                // [batch, flattened] format
                num_detections = output_shape[1] / 84;  // Assume 84 features per detection
                num_features = 84;
                features_first = false;
            }
            else {
                cout << "Warning: Unexpected output shape size: " << output_shape.size() << endl;
                return {};
            }


            bool is_yolov8_format = (num_features == 84);
            bool is_yolov5_format = (num_features == 85);
            bool is_single_class_format = (num_features == 5);  // Single class: (x, y, w, h, conf)

            // Original image size
            // Calculate scale factors for coordinate transformation
            // Note: ultralytics YOLO may use letterbox padding, but ONNX export typically uses simple resize
            // Use double precision for more accurate calculation to reduce 1-2 pixel errors
            double scale_x = static_cast<double>(image_bgr.cols) / static_cast<double>(model_input_size.width);
            double scale_y = static_cast<double>(image_bgr.rows) / static_cast<double>(model_input_size.height);

            vector<Detection> detections;

            if (is_single_class_format) {
                // Single class format: [batch, 5, num_detections] or [batch, num_detections, 5]
                // Each detection: (x, y, w, h, conf) - single class, no class scores
                int valid_detections = 0;
                for (int i = 0; i < num_detections; i++) {
                    float det_values[5];
                    if (features_first) {
                        // [batch, 5, num_detections] format
                        // Memory layout: [x0, x1, ..., x8399, y0, y1, ..., y8399, w0, w1, ..., w8399, h0, h1, ..., h8399, conf0, conf1, ..., conf8399]
                        // For detection i: x at [i], y at [num_detections + i], w at [2*num_detections + i], h at [3*num_detections + i], conf at [4*num_detections + i]
                        det_values[0] = output_data[0 * num_detections + i];  // x
                        det_values[1] = output_data[1 * num_detections + i];  // y
                        det_values[2] = output_data[2 * num_detections + i];  // w
                        det_values[3] = output_data[3 * num_detections + i];  // h
                        det_values[4] = output_data[4 * num_detections + i];  // conf
                    }
                    else {
                        // [batch, num_detections, 5] - normal access
                        det_values[0] = output_data[i * num_features + 0];
                        det_values[1] = output_data[i * num_features + 1];
                        det_values[2] = output_data[i * num_features + 2];
                        det_values[3] = output_data[i * num_features + 3];
                        det_values[4] = output_data[i * num_features + 4];
                    }

                    // Use double precision for coordinate calculation to match Python's precision
                    // Note: YOLO output is in normalized coordinates (0-1) or model input size coordinates
                    // We need to scale to original image size
                    // Python ultralytics may use letterbox padding, but ONNX export uses simple resize
                    // So we use simple scale calculation
                    double center_x = static_cast<double>(det_values[0]) * scale_x;
                    double center_y = static_cast<double>(det_values[1]) * scale_y;
                    double width = static_cast<double>(det_values[2]) * scale_x;
                    double height = static_cast<double>(det_values[3]) * scale_y;
                    float conf = det_values[4];  // Confidence score

                    if (conf >= conf_thres) {
                        Detection det_obj;
                        // Convert center+width/height to xyxy format
                        // Python ultralytics uses: x1 = center_x - width/2, x2 = center_x + width/2
                        // But there might be a small offset due to letterbox padding in Python
                        // Since we're getting 1-2 pixels larger, try adjusting by subtracting 0.5-1 pixel
                        // Actually, let's check if the issue is in the conversion itself
                        // Use exact same calculation as Python ultralytics
                        det_obj.x1 = static_cast<float>(center_x - width / 2.0);
                        det_obj.y1 = static_cast<float>(center_y - height / 2.0);
                        det_obj.x2 = static_cast<float>(center_x + width / 2.0);
                        det_obj.y2 = static_cast<float>(center_y + height / 2.0);
                        det_obj.confidence = conf;
                        det_obj.class_id = 0;  // Single class, always class 0
                        detections.push_back(det_obj);
                        valid_detections++;
                    }
                }
            }
            else if (is_yolov8_format) {
                // YOLOv8 format: [batch, num_detections, 84] or [batch, 84, num_detections]
                // Each detection: (x, y, w, h, class_scores[80])
                for (int i = 0; i < num_detections; i++) {
                    float* det;
                    if (features_first) {
                        // [batch, 84, num_detections] - transpose access
                        vector<float> det_values(num_features);
                        for (int f = 0; f < num_features; f++) {
                            det_values[f] = output_data[f * num_detections + i];
                        }
                        det = det_values.data();
                    }
                    else {
                        // [batch, num_detections, 84] - normal access
                        det = output_data + i * num_features;
                    }

                    float center_x = det[0] * scale_x;
                    float center_y = det[1] * scale_y;
                    float width = det[2] * scale_x;
                    float height = det[3] * scale_y;

                    // Find best class (from index 4)
                    int best_class = 0;
                    float best_class_score = det[4];
                    for (int j = 5; j < num_features; j++) {
                        if (det[j] > best_class_score) {
                            best_class_score = det[j];
                            best_class = j - 4;
                        }
                    }

                    float conf = best_class_score;

                    if (conf >= conf_thres) {
                        Detection det_obj;
                        det_obj.x1 = center_x - width / 2.0f;
                        det_obj.y1 = center_y - height / 2.0f;
                        det_obj.x2 = center_x + width / 2.0f;
                        det_obj.y2 = center_y + height / 2.0f;
                        det_obj.confidence = conf;
                        det_obj.class_id = best_class;
                        detections.push_back(det_obj);
                    }
                }
            }
            else if (is_yolov5_format) {
                // YOLOv5/v7 format: [batch, num_detections, 85]
                // Each detection: (x, y, w, h, conf, class_scores[80])
                for (int i = 0; i < num_detections; i++) {
                    float* det = output_data + i * num_features;

                    float center_x = det[0] * scale_x;
                    float center_y = det[1] * scale_y;
                    float width = det[2] * scale_x;
                    float height = det[3] * scale_y;
                    float conf = det[4];

                    // Find best class (from index 5)
                    int best_class = 0;
                    float best_class_score = det[5];
                    for (int j = 6; j < num_features; j++) {
                        if (det[j] > best_class_score) {
                            best_class_score = det[j];
                            best_class = j - 5;
                        }
                    }
                    conf *= best_class_score;

                    if (conf >= conf_thres) {
                        Detection det_obj;
                        det_obj.x1 = center_x - width / 2.0f;
                        det_obj.y1 = center_y - height / 2.0f;
                        det_obj.x2 = center_x + width / 2.0f;
                        det_obj.y2 = center_y + height / 2.0f;
                        det_obj.confidence = conf;
                        det_obj.class_id = best_class;
                        detections.push_back(det_obj);
                    }
                }
            }
            else {
                // Generic format - try to detect automatically
                cout << "Warning: Unknown YOLO output format. Shape: [";
                for (size_t i = 0; i < output_shape.size(); i++) {
                    cout << output_shape[i];
                    if (i < output_shape.size() - 1) cout << ", ";
                }
                cout << "]" << endl;
                cout << "Attempting generic parsing..." << endl;

                // Generic parsing for [batch, features, detections] or [batch, detections, features]
                if (output_shape.size() == 3) {
                    for (int i = 0; i < num_detections; i++) {
                        float* det;
                        if (features_first) {
                            // [batch, features, num_detections] - transpose access
                            vector<float> det_values(num_features);
                            for (int f = 0; f < num_features; f++) {
                                det_values[f] = output_data[f * num_detections + i];
                            }
                            det = det_values.data();
                        }
                        else {
                            // [batch, num_detections, features] - normal access
                            det = output_data + i * num_features;
                        }

                        if (num_features >= 4) {
                            float center_x = det[0] * scale_x;
                            float center_y = det[1] * scale_y;
                            float width = det[2] * scale_x;
                            float height = det[3] * scale_y;

                            // Find best class or use confidence
                            int best_class = 0;
                            float conf = 0.0f;

                            if (num_features == 5) {
                                // Single class: (x, y, w, h, conf)
                                conf = det[4];
                            }
                            else if (num_features > 5) {
                                // Multi-class: find best class score
                                float best_class_score = det[4];
                                for (int j = 5; j < num_features; j++) {
                                    if (det[j] > best_class_score) {
                                        best_class_score = det[j];
                                        best_class = j - 4;
                                    }
                                }
                                conf = best_class_score;
                            }
                            else {
                                // Not enough features
                                continue;
                            }

                            if (conf >= conf_thres) {
                                Detection det_obj;
                                det_obj.x1 = center_x - width / 2.0f;
                                det_obj.y1 = center_y - height / 2.0f;
                                det_obj.x2 = center_x + width / 2.0f;
                                det_obj.y2 = center_y + height / 2.0f;
                                det_obj.confidence = conf;
                                det_obj.class_id = best_class;
                                detections.push_back(det_obj);
                            }
                        }
                    }
                }
            }

            // Apply NMS to remove overlapping detections (same as Python ultralytics YOLO)
            vector<Detection> nms_detections = ApplyNMS(detections, nms_threshold);
            return nms_detections;
        }
        catch (const exception& e) {
            cout << "YOLO inference error: " << e.what() << endl;
            return {};
        }
    }

public:
    void CropPillsFromTrays(int start_tray = -1, int end_tray = -1) {
        if (!model_loaded) {
            cout << "YOLO model is not loaded. Cannot perform crop operation." << endl;
            return;
        }

        if (folder_c_path.empty()) {
            cout << "Cropped image save path (folder_c_path) is not set." << endl;
            return;
        }

        // Start execution time measurement
        auto start_time = chrono::high_resolution_clock::now();

        cout << "\nStarting pill crop from tray images..." << endl;
        if (start_tray >= 1 && end_tray >= start_tray) {
            cout << "Processing tray range: [" << start_tray << ", " << end_tray << "]" << endl;
        }

        // Find tray*.bmp files
        vector<string> tray_files;
        if (fs::exists(folder_b_path) && fs::is_directory(folder_b_path)) {
            for (const auto& entry : fs::directory_iterator(folder_b_path)) {
                if (entry.is_regular_file()) {
                    string filename = entry.path().filename().string();
                    if (filename.find("tray") == 0 && filename.find(".bmp") != string::npos) {
                        // Extract tray number for filtering
                        int tray_num = 0;
                        try {
                            string num_str = filename;
                            size_t pos = num_str.find("tray");
                            if (pos != string::npos) {
                                num_str = num_str.substr(pos + 4);
                            }
                            pos = num_str.find(".bmp");
                            if (pos != string::npos) {
                                num_str = num_str.substr(0, pos);
                            }
                            tray_num = stoi(num_str);

                            // Filter by tray range if specified
                            if (start_tray >= 1 && end_tray >= start_tray) {
                                if (tray_num >= start_tray && tray_num <= end_tray) {
                                    tray_files.push_back(filename);
                                }
                            }
                            else {
                                // No range specified, include all
                                tray_files.push_back(filename);
                            }
                        }
                        catch (...) {
                            // If tray number extraction fails, include the file if no range is specified
                            if (start_tray < 1 || end_tray < start_tray) {
                                tray_files.push_back(filename);
                            }
                        }
                    }
                }
            }
        }

        sort(tray_files.begin(), tray_files.end());
        cout << "Found tray files: " << tray_files.size() << endl;

        int total_cropped = 0;
        for (const auto& tray_filename : tray_files) {
            // Extract tray number (e.g., tray1.bmp -> 1)
            int tray_num = 0;
            try {
                string num_str = tray_filename;
                size_t pos = num_str.find("tray");
                if (pos != string::npos) {
                    num_str = num_str.substr(pos + 4);
                }
                pos = num_str.find(".bmp");
                if (pos != string::npos) {
                    num_str = num_str.substr(0, pos);
                }
                tray_num = stoi(num_str);
            }
            catch (...) {
                cout << "  Warning: Cannot extract tray number - " << tray_filename << endl;
                continue;
            }

            // Load tray image
            fs::path tray_path = fs::path(folder_b_path) / tray_filename;
            cv::Mat tray_image = cv::imread(tray_path.string());
            if (tray_image.empty()) {
                cout << "  Warning: Cannot load image - " << tray_path.string() << endl;
                continue;
            }

            // Detect pills with YOLO
            vector<Detection> detections = YOLODetectXYXY(tray_image, 0.25f);

            // Crop and save each detected pill
            int idx = 1;
            for (const auto& det : detections) {
                // Python: x1, y1, x2, y2 = int(x1), int(y1), int(x2), int(y2)
                // Python's int() truncates toward zero (same as floor for positive numbers)
                // Use truncation to match Python int() exactly
                int x1 = static_cast<int>(det.x1);  // Truncate (Python int() for positive numbers)
                int y1 = static_cast<int>(det.y1);  // Truncate (Python int() for positive numbers)
                int x2 = static_cast<int>(det.x2);  // Truncate (Python int() for positive numbers)
                int y2 = static_cast<int>(det.y2);  // Truncate (Python int() for positive numbers)

                // Clip to image boundaries (same as Python: max(0, x1), min(w, x2), etc.)
                // Python: h, w = tray_image.shape[:2]
                int h = tray_image.rows;
                int w = tray_image.cols;
                x1 = max(0, x1);
                y1 = max(0, y1);
                x2 = min(w, x2);
                y2 = min(h, y2);

                // Crop (same as Python: tray_image[y1:y2, x1:x2])
                // OpenCV Rect(x, y, width, height) and Mat(roi) is equivalent to Python's [y1:y2, x1:x2]
                cv::Rect roi(x1, y1, x2 - x1, y2 - y1);
                if (roi.width > 0 && roi.height > 0) {
                    cv::Mat cropped_pill = tray_image(roi);

                    if (!cropped_pill.empty()) {
                        // Generate filename: img_(tray_num)_(pill_idx)_(x1,y1,x2,y2)_(conf).bmp
                        ostringstream oss;
                        oss << "img_" << tray_num << "_" << idx
                            << "_(" << x1 << "," << y1 << "," << x2 << "," << y2 << ")_"
                            << "(" << fixed << setprecision(3) << det.confidence << ").bmp";

                        fs::path crop_path = fs::path(folder_c_path) / oss.str();

                        // Save (only if confidence > 0.5)
                        if (det.confidence > 0.5f) {
                            cv::imwrite(crop_path.string(), cropped_pill);
                            total_cropped++;
                        }
                    }
                    else {
                        cout << "    Warning: Crop failed (size is 0) - pill " << idx << endl;
                    }
                }
                idx++;
            }
        }

        // End execution time measurement
        auto end_time = chrono::high_resolution_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
        double elapsed_seconds = duration.count() / 1000.0;

        cout << "\nCrop operation completed!" << endl;
        cout << "Total " << total_cropped << " pill images saved." << endl;
        cout << "Execution time: " << fixed << setprecision(2)
            << elapsed_seconds << " seconds (" << duration.count() << "ms)" << endl;
    }
};

namespace {
    std::unique_ptr<PillAugmentationSystem> g_pill_system;
    std::string g_pill_tray_path;
    std::string g_pill_cropped_path;
    std::string g_pill_empty_tray_path;
    std::string g_pill_yolo_model_path;

    PillAugmentationSystem& GetPillAugmentationSystem(
        const std::string& tray_path,
        const std::string& cropped_path,
        const std::string& empty_tray_path,
        const std::string& yolo_model_path) {

        if (!g_pill_system) {
            g_pill_tray_path = tray_path;
            g_pill_cropped_path = cropped_path;
            g_pill_empty_tray_path = empty_tray_path;
            g_pill_yolo_model_path = yolo_model_path;

            g_pill_system = std::make_unique<PillAugmentationSystem>(
                tray_path,
                cropped_path,
                empty_tray_path,
                yolo_model_path
            );
        }
        else {
            if (tray_path != g_pill_tray_path ||
                cropped_path != g_pill_cropped_path ||
                empty_tray_path != g_pill_empty_tray_path ||
                yolo_model_path != g_pill_yolo_model_path) {
                std::cout << "Warning: PillAugmentationSystem singleton already created. "
                    << "Reusing existing instance with previous paths/settings." << std::endl;
            }
        }

        return *g_pill_system;
    }
}

extern "C" {
    PILLDETECTIONKERNEL_API int PrewarmCropsModel() {
        std::string tray_path = GetTrayPath();
        std::string empty_tray_path = GetEmptyTrayPath();
        std::string yolo_model_path = GetYoloModelPath();
        std::string cropped_dir_str = GetCroppedDir();

        if (cropped_dir_str.empty()) {
            std::cout << "Error: Cannot find LOCALAPPDATA environment variable." << std::endl;
            return 1;
        }

        try {
            GetPillAugmentationSystem(tray_path, cropped_dir_str, empty_tray_path, yolo_model_path);
        }
        catch (const std::exception& e) {
            std::cout << "PrewarmCropsModel failed: " << e.what() << std::endl;
            return 1;
        }
        catch (...) {
            std::cout << "PrewarmCropsModel failed: unknown error" << std::endl;
            return 1;
        }

        return 0;
    }
}

int crops_main(int start_tray, int end_tray) {
    // Path settings (경로는 myHeader.h에서 정의됨)
    string tray_path = GetTrayPath();
    string empty_tray_path = GetEmptyTrayPath();
    string yolo_model_path = GetYoloModelPath();



    // Get cropped path (경로는 myHeader.h에서 정의됨)
    string cropped_dir_str = GetCroppedDir();
    if (cropped_dir_str.empty()) {
        cout << "Error: Cannot find LOCALAPPDATA environment variable." << endl;
        return 1;
    }


    fs::path cropped_path(cropped_dir_str);

    cout << "cropped_path: " << cropped_path << endl;
    // Note: cropped 폴더는 ProcessTrayRanges()에서 이미 삭제 및 생성됨

    cout << string(60, '=') << endl;
    cout << "Pill Image Processing System - Crop Pills from Trays" << endl;
    if (start_tray >= 1 && end_tray >= start_tray) {
        cout << "Tray Range: [" << start_tray << ", " << end_tray << "]" << endl;
    }
    cout << string(60, '=') << endl;


    // Ensure output folder exists (may have been cleared by ProcessTrayRanges)
    fs::create_directories(cropped_path);

    // Initialize PillAugmentationSystem
    try {
        PillAugmentationSystem& system = GetPillAugmentationSystem(
            tray_path,
            cropped_path.string(),
            empty_tray_path,
            yolo_model_path
        );


        cout << "\nExecuting pill crop from trays function." << endl;
        system.CropPillsFromTrays(start_tray, end_tray);

        cout << "\n" << string(60, '=') << endl;
        cout << "Crop processing completed!" << endl;
        cout << string(60, '=') << endl;
    }
    catch (const exception& e) {
        cout << "Initialization failed: " << e.what() << endl;
        return 1;
    }

    return 0;
}
