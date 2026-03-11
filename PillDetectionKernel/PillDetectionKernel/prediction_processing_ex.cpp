//2026/1/27(Tue) completed. saved as prediction_processing_ex_20160127.cpp

#define _CRT_SECURE_NO_WARNINGS

#ifdef _WIN32
#define NOMINMAX
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
#include <map>
#include <set>
#include <tuple>
#include <cmath>
#include <numeric>
#include <fstream>
#include <cstring>
#include <limits>
#include <ctime>
#include <memory>
#include <unordered_map>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

// GPU acceleration setup (AMD GPU uses DirectML provider)
#include "myHeader.h"

// ONNX Runtime headers
#include <onnxruntime_cxx_api.h>
#include <onnxruntime_c_api.h>
#if AMD == 1 && defined(_WIN32)
#include <dml_provider_factory.h>  // DirectML provider API
#endif

using namespace std;
namespace fs = filesystem;

// ===================== Data Structures =====================
struct BBox {
	int x1, y1, x2, y2;
};

struct MaskInfo {
	string mask_path;
	float elongation;
	float area;
};

struct PrescMaskInfo {
	vector<MaskInfo> masks;  // All masks for this prescription
};

// CropMatch 구조체는 이제 myHeader.h에 정의되어 있음
// struct CropMatch {
//	string crop_name;
//	float prob;
//	string best_name;  // No Match: best matching presc name
//};

// Data structures for C# communication
struct ExcelData {
	vector<string> subfolder_names;
	vector<int> numbers;
	vector<string> presc_paths;
	string cropped_dir;
	map<string, map<int, vector<pair<string, float>>>> presc_tray_matches;
	map<int, vector<CropMatch>> unmatched_by_tray;
	vector<int> tray_indices;
	string result_dir;
};

struct NoMatchTrayData {
	map<int, vector<CropMatch>> unmatched_by_tray;
	map<string, map<int, vector<pair<string, float>>>> presc_tray_matches;
	vector<string> subfolder_names;
	vector<int> numbers;
	string tray_dir;
	string result_dir;
	int padding;
	bool draw_overmatch_boxes;
};

// ===================== Siamese Network Inference Engine =====================
class SiameseNetwork {
private:
	Ort::Env ort_env;
	Ort::Session* ort_session;
	Ort::AllocatorWithDefaultOptions allocator;
	vector<string> input_name_strings;
	vector<string> output_name_strings;
	vector<const char*> input_names;
	vector<const char*> output_names;
	cv::Size input_size_;
	bool model_loaded;

public:
	SiameseNetwork()
		: ort_env(ORT_LOGGING_LEVEL_ERROR, "SiameseNetwork"),  // ERROR level to reduce console output
		ort_session(nullptr),
		model_loaded(false),
		input_size_(224, 224) {  // Default input size (can be changed)
	}

	~SiameseNetwork() {
		if (ort_session) {
			delete ort_session;
		}
	}

	bool LoadModel(const string& model_path) {
		try {
			Ort::SessionOptions session_options;
			// Use all available CPU cores for better performance
			session_options.SetIntraOpNumThreads(0);  // 0 = use all available cores
			session_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
			// Enable memory pattern optimization for better performance
			session_options.EnableMemPattern();
			// Enable CPU memory arena for better memory management
			session_options.EnableCpuMemArena();

			// GPU acceleration setup: Execution Provider selection
			// AMD = 0: MX150 (CUDA GPU mode)
			// AMD = 1: AMD 780M (DirectML GPU mode)
#if AMD == 0
			// MX150: CUDA provider setup
			try {
				OrtCUDAProviderOptions cuda_options{};
				cuda_options.device_id = 0;
				session_options.AppendExecutionProvider_CUDA(cuda_options);
				cout << "CUDA provider enabled (MX150 GPU acceleration)" << endl;
				LogToFile("SiameseNetwork", "CUDA provider enabled (MX150 GPU acceleration)");

			}
			catch (const exception& e) {
				cout << "Warning: Failed to enable CUDA provider: " << e.what() << endl;
				cout << "Falling back to CPU execution." << endl;
			}
#elif AMD == 1 && defined(_WIN32)
			// AMD 780M: DirectML provider setup
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
						//LogToFile("SiameseNetwork", "DirectML provider enabled (AMD 780M GPU acceleration)");
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

#ifdef _WIN32
			int size_needed = MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, NULL, 0);
			wstring wmodel_path(size_needed, 0);
			MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, &wmodel_path[0], size_needed);
			ort_session = new Ort::Session(ort_env, wmodel_path.c_str(), session_options);
#else
			ort_session = new Ort::Session(ort_env, model_path.c_str(), session_options);
#endif

			// Input/Output name extraction
			input_name_strings = ort_session->GetInputNames();
			output_name_strings = ort_session->GetOutputNames();

			input_names.clear();
			output_names.clear();
			for (size_t i = 0; i < input_name_strings.size(); i++) {
				input_names.push_back(input_name_strings[i].c_str());
			}
			for (size_t i = 0; i < output_name_strings.size(); i++) {
				output_names.push_back(output_name_strings[i].c_str());
			}

			// Input size extraction
			auto input_type_info = ort_session->GetInputTypeInfo(0);
			auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
			auto shape = tensor_info.GetShape();
			if (shape.size() >= 3) {
				input_size_ = cv::Size(static_cast<int>(shape[3]), static_cast<int>(shape[2]));
				cout << "Siamese model input size detected: " << input_size_.width << "x" << input_size_.height << endl;
			}
			else {
				// Fallback: Python uses (100, 100), so set explicitly
				input_size_ = cv::Size(100, 100);
				cout << "Siamese model input size not detected, using default: 100x100 (Python IMG_SIZE)" << endl;
			}

			model_loaded = true;
			return true;
		}
		catch (const exception& e) {
			cerr << "Siamese model loading failed: " << e.what() << endl;
			return false;
		}
	}

	// Compute similarity between two images (returns 0~1) - Single image version
	float ComputeSimilarity(const cv::Mat& img1, const cv::Mat& img2) {
		if (!model_loaded || !ort_session) {
			return 0.0f;
		}

		try {
			// Image preprocessing
			cv::Mat img1_processed = PreprocessImage(img1);
			cv::Mat img2_processed = PreprocessImage(img2);

			// Prepare input tensor shape and size
			vector<int64_t> input_shape = { 1, 3, input_size_.height, input_size_.width };
			size_t input_tensor_size = 1 * 3 * input_size_.height * input_size_.width;

			// Input 1
			vector<float> input1_values(input_tensor_size);
			memcpy(input1_values.data(), img1_processed.data, input_tensor_size * sizeof(float));

			// Input 2
			vector<float> input2_values(input_tensor_size);
			memcpy(input2_values.data(), img2_processed.data, input_tensor_size * sizeof(float));

			Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

			Ort::Value input_tensor1 = Ort::Value::CreateTensor<float>(
				memory_info, input1_values.data(), input_tensor_size,
				input_shape.data(), input_shape.size());

			Ort::Value input_tensor2 = Ort::Value::CreateTensor<float>(
				memory_info, input2_values.data(), input_tensor_size,
				input_shape.data(), input_shape.size());

			// Run inference
			vector<Ort::Value> input_tensors;
			input_tensors.reserve(2);
			input_tensors.push_back(move(input_tensor1));
			input_tensors.push_back(move(input_tensor2));

			Ort::RunOptions run_options;
			// Disable logging during inference for better performance
			run_options.SetRunLogVerbosityLevel(ORT_LOGGING_LEVEL_FATAL);
			vector<Ort::Value> output_tensors = ort_session->Run(
				run_options,
				input_names.data(), input_tensors.data(), input_tensors.size(),
				output_names.data(), output_names.size());

			// Extract output
			float* output_data = output_tensors[0].GetTensorMutableData<float>();
			return output_data[0];  // Return similarity score

		}
		catch (const exception& e) {
			cerr << "Siamese inference error: " << e.what() << endl;
			return 0.0f;
		}
	}

	// Compute similarity in batch (much faster) - Batch version
	// crop_batch: vector of crop images (batch_size)
	// presc_img: single prescription image (repeated for batch)
	// Returns: vector of similarity scores (batch_size)
	vector<float> ComputeSimilarityBatch(const vector<cv::Mat>& crop_batch, const cv::Mat& presc_img) {
		vector<float> results;
		if (!model_loaded || !ort_session || crop_batch.empty()) {
			return results;
		}

		size_t batch_size = crop_batch.size();
		results.resize(batch_size, 0.0f);

		try {
			// Preprocess all crop images
			vector<cv::Mat> crop_processed;
			crop_processed.reserve(batch_size);
			for (const auto& img : crop_batch) {
				if (img.empty()) {
					crop_processed.push_back(cv::Mat());
					continue;
				}
				crop_processed.push_back(PreprocessImage(img));
			}

			// Preprocess prescription image
			cv::Mat presc_processed = PreprocessImage(presc_img);

			// Prepare batch input tensor shape: (batch_size, 3, H, W)
			size_t single_tensor_size = 3 * input_size_.height * input_size_.width;
			size_t batch_tensor_size = batch_size * single_tensor_size;

			// Prepare input1 (crop batch): (batch_size, 3, H, W)
			vector<float> input1_values(batch_tensor_size);
			for (size_t i = 0; i < batch_size; i++) {
				if (!crop_processed[i].empty()) {
					memcpy(input1_values.data() + i * single_tensor_size,
						crop_processed[i].data, single_tensor_size * sizeof(float));
				}
			}

			// Prepare input2 (presc repeated for batch): (batch_size, 3, H, W)
			vector<float> input2_values(batch_tensor_size);
			for (size_t i = 0; i < batch_size; i++) {
				memcpy(input2_values.data() + i * single_tensor_size,
					presc_processed.data, single_tensor_size * sizeof(float));
			}

			// Use GPU memory if available (DirectML), otherwise CPU
			// For DirectML, we need to use GPU memory allocator
			Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

			// Create batch input tensors
			vector<int64_t> batch_input_shape = { static_cast<int64_t>(batch_size), 3,
												  input_size_.height, input_size_.width };

			// Create tensors - DirectML will handle GPU memory automatically
			Ort::Value input_tensor1 = Ort::Value::CreateTensor<float>(
				memory_info, input1_values.data(), batch_tensor_size,
				batch_input_shape.data(), batch_input_shape.size());

			Ort::Value input_tensor2 = Ort::Value::CreateTensor<float>(
				memory_info, input2_values.data(), batch_tensor_size,
				batch_input_shape.data(), batch_input_shape.size());

			// Run batch inference
			vector<Ort::Value> input_tensors;
			input_tensors.reserve(2);
			input_tensors.push_back(move(input_tensor1));
			input_tensors.push_back(move(input_tensor2));

			Ort::RunOptions run_options;
			vector<Ort::Value> output_tensors = ort_session->Run(
				run_options,
				input_names.data(), input_tensors.data(), input_tensors.size(),
				output_names.data(), output_names.size());

			// Extract batch output: (batch_size,)
			float* output_data = output_tensors[0].GetTensorMutableData<float>();
			for (size_t i = 0; i < batch_size; i++) {
				results[i] = output_data[i];
			}

		}
		catch (const exception& e) {
			cerr << "Siamese batch inference error: " << e.what() << endl;
			// Fill with zeros on error
			results.assign(batch_size, 0.0f);
		}

		return results;
	}

private:
	cv::Mat PreprocessImage(const cv::Mat& image) {
		cv::Mat resized, blob;
		cv::resize(image, resized, input_size_);
		cv::dnn::blobFromImage(resized, blob, 1.0 / 255.0, input_size_, cv::Scalar(), true, false, CV_32F);

		// Convert blob from (1, 3, H, W) format to (3, H*W) format
		// blobFromImage output: (1, 3, H, W) = (1, 3, 224, 224) (default)
		// Resize masks if needed: [B, C, H, W] format conversion
		// Channel-wise data extraction: [channel0 all pixels, channel1 all pixels, channel2 all pixels]

		int H = input_size_.height;
		int W = input_size_.width;
		cv::Mat result(3, H * W, CV_32F);

		// Rearrange blob data channel by channel
		// blob shape: (1, 3, H, W), target format: [channel0(H*W), channel1(H*W), channel2(H*W)]
		float* blob_data = blob.ptr<float>();
		for (int c = 0; c < 3; c++) {
			float* channel_data = blob_data + c * H * W;
			memcpy(result.ptr<float>(c), channel_data, H * W * sizeof(float));
		}

		return result;
	}
};

// ===================== YOLO Segmentation Inference Engine =====================
class YOLOSegmentation {
private:
	Ort::Env ort_env;
	Ort::Session* ort_session;
	Ort::AllocatorWithDefaultOptions allocator;
	vector<string> input_name_strings;
	vector<string> output_name_strings;
	vector<const char*> input_names;
	vector<const char*> output_names;
	cv::Size input_size_;
	float conf_threshold_;
	bool model_loaded;
	bool printed_model_io_once_ = false;

	struct LetterboxInfo {
		float scale = 1.0f;
		int padX = 0;
		int padY = 0;
		int newW = 0;
		int newH = 0;
	};

	static inline float Sigmoid(float x) {
		return 1.0f / (1.0f + std::exp(-x));
	}

	// Letterbox resize to target size, preserving aspect ratio, padding with 114 (Ultralytics default).
	static cv::Mat LetterboxBgr(const cv::Mat& srcBgr, const cv::Size& target, LetterboxInfo& info) {
		const int srcW = srcBgr.cols;
		const int srcH = srcBgr.rows;
		const float r = std::min(target.width / (float)srcW, target.height / (float)srcH);

		info.scale = r;
		info.newW = (int)std::round(srcW * r);
		info.newH = (int)std::round(srcH * r);

		cv::Mat resized;
		cv::resize(srcBgr, resized, cv::Size(info.newW, info.newH), 0, 0, cv::INTER_LINEAR);

		const int dw = target.width - info.newW;
		const int dh = target.height - info.newH;
		info.padX = dw / 2;
		info.padY = dh / 2;

		cv::Mat padded;
		cv::copyMakeBorder(
			resized,
			padded,
			info.padY,
			dh - info.padY,
			info.padX,
			dw - info.padX,
			cv::BORDER_CONSTANT,
			cv::Scalar(114, 114, 114));
		return padded;
	}

public:
	YOLOSegmentation()
		: ort_env(ORT_LOGGING_LEVEL_ERROR, "YOLOSegmentation"),  // ERROR level to reduce console output
		ort_session(nullptr),
		model_loaded(false),
		input_size_(640, 640),
		conf_threshold_(0.25f) {
	}

	~YOLOSegmentation() {
		if (ort_session) {
			delete ort_session;
		}
	}

	bool LoadModel(const string& model_path, float conf_threshold = 0.25f) {
		try {
			Ort::SessionOptions session_options;
			// Use all available CPU cores for better performance
			session_options.SetIntraOpNumThreads(0);  // 0 = use all available cores
			session_options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
			// Enable memory pattern optimization for better performance
			session_options.EnableMemPattern();
			// Enable CPU memory arena for better memory management
			session_options.EnableCpuMemArena();

			// GPU acceleration setup: Execution Provider selection
			// AMD = 0: MX150 (CUDA GPU mode)
			// AMD = 1: AMD 780M (DirectML GPU mode)
#if AMD == 0
			// MX150: CUDA provider setup
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
			// AMD 780M: DirectML provider setup
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

#ifdef _WIN32
			int size_needed = MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, NULL, 0);
			wstring wmodel_path(size_needed, 0);
			MultiByteToWideChar(CP_UTF8, 0, model_path.c_str(), -1, &wmodel_path[0], size_needed);
			ort_session = new Ort::Session(ort_env, wmodel_path.c_str(), session_options);
#else
			ort_session = new Ort::Session(ort_env, model_path.c_str(), session_options);
#endif

			input_name_strings = ort_session->GetInputNames();
			output_name_strings = ort_session->GetOutputNames();

			input_names.clear();
			output_names.clear();
			for (size_t i = 0; i < input_name_strings.size(); i++) {
				input_names.push_back(input_name_strings[i].c_str());
			}
			for (size_t i = 0; i < output_name_strings.size(); i++) {
				output_names.push_back(output_name_strings[i].c_str());
			}

			auto input_type_info = ort_session->GetInputTypeInfo(0);
			auto tensor_info = input_type_info.GetTensorTypeAndShapeInfo();
			auto shape = tensor_info.GetShape();
			if (shape.size() >= 3) {
				input_size_ = cv::Size(static_cast<int>(shape[3]), static_cast<int>(shape[2]));
			}

			conf_threshold_ = conf_threshold;
			model_loaded = true;
			return true;
		}
		catch (const exception& e) {
			cerr << "YOLO model loading failed: " << e.what() << endl;
			return false;
		}
	}

	// Mask generation result with elongation, area info
	struct MaskResult {
		cv::Mat mask;
		float elongation;
		float area;
		bool valid;
	};

	MaskResult GenerateMask(const cv::Mat& image) {
		MaskResult result;
		result.valid = false;

		if (!model_loaded || !ort_session) {
			return result;
		}

		try {
			const int originalH = image.rows;
			const int originalW = image.cols;

			// Preprocess: letterbox (Ultralytics default) -> blob (RGB, 0..1)
			LetterboxInfo lb;
			cv::Mat letterboxed = LetterboxBgr(image, input_size_, lb);

			cv::Mat blob;
			cv::dnn::blobFromImage(letterboxed, blob, 1.0 / 255.0, input_size_, cv::Scalar(), true, false, CV_32F);

			vector<int64_t> input_shape = { 1, 3, input_size_.height, input_size_.width };
			size_t input_tensor_size = 1 * 3 * input_size_.height * input_size_.width;
			vector<float> input_tensor_values(input_tensor_size);
			memcpy(input_tensor_values.data(), blob.data, input_tensor_size * sizeof(float));

			Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
			Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
				memory_info, input_tensor_values.data(), input_tensor_size,
				input_shape.data(), input_shape.size());

			Ort::RunOptions run_options;
			// Disable logging during inference for better performance
			run_options.SetRunLogVerbosityLevel(ORT_LOGGING_LEVEL_FATAL);
			vector<Ort::Value> output_tensors = ort_session->Run( //<=> model.predict()
				run_options,
				input_names.data(), &input_tensor, 1,
				output_names.data(), output_names.size());

			// Print model I/O tensor shapes once to confirm export type (det/proto vs direct masks).
			if (!printed_model_io_once_) {//[see generate_masks.cpp]
				printed_model_io_once_ = true;
				cout << "\n=== YOLOSegmentation: ONNX I/O shapes (printed once) ===" << endl;
				cout << "Input size used: " << input_size_.width << "x" << input_size_.height << endl;
				cout << "Num outputs: " << output_tensors.size() << endl;

				auto printShape = [](const vector<int64_t>& s) {
					cout << "[";
					for (size_t i = 0; i < s.size(); ++i) {
						cout << s[i] << (i + 1 < s.size() ? ", " : "");
					}
					cout << "]";
					};

				int detCandidate = -1;
				int protoCandidate = -1;
				int directMaskCandidate = -1;

				for (size_t oi = 0; oi < output_tensors.size(); ++oi) {
					const auto shape = output_tensors[oi].GetTensorTypeAndShapeInfo().GetShape();
					const char* name = (oi < output_names.size() ? output_names[oi] : "(unknown)");
					cout << "  Output[" << oi << "] name=" << name << " shape=";
					printShape(shape);
					cout << endl;

					// Heuristics:
					// - det:   [1, C, N]
					// - proto: [1, M, H, W]
					// - direct masks often: [1, num_masks, H, W] (num_masks can be > 1)
					if (shape.size() == 3 && shape[0] == 1 && detCandidate < 0) detCandidate = (int)oi;
					if (shape.size() == 4 && shape[0] == 1) {
						if (protoCandidate < 0) protoCandidate = (int)oi;
						// If 2nd dim looks like "num_masks" (>1) and H/W are plausible, mark as direct mask candidate.
						if (shape[1] > 1 && shape[2] > 16 && shape[3] > 16 && directMaskCandidate < 0) {
							directMaskCandidate = (int)oi;
						}
					}
				}

				if (detCandidate >= 0 && protoCandidate >= 0) {
					cout << "Inference export type guess: det/proto (YOLOv8-seg decode required)." << endl;
				}
				else if (directMaskCandidate >= 0) {
					cout << "Inference export type guess: direct masks tensor (binarize/select best mask)." << endl;
				}
				else {
					cout << "Inference export type guess: unknown (please inspect shapes above)." << endl;
				}
				cout << "========================================================\n" << endl;
			}

			// Prefer YOLOv8-seg det+proto decoding (generate_masks.cpp style).
			int detIdx = -1;
			int protoIdx = -1;
			vector<vector<int64_t>> outShapes(output_tensors.size());
			for (size_t oi = 0; oi < output_tensors.size(); ++oi) {
				outShapes[oi] = output_tensors[oi].GetTensorTypeAndShapeInfo().GetShape();
			}
			for (size_t oi = 0; oi < outShapes.size(); ++oi) {
				const auto& s = outShapes[oi];
				if (s.size() == 3 && s[0] == 1) detIdx = (int)oi;   // [1, C, N]
				if (s.size() == 4 && s[0] == 1) protoIdx = (int)oi; // [1, M, H, W]
			}

			if (detIdx >= 0 && protoIdx >= 0) {
				const float* det = output_tensors[detIdx].GetTensorData<float>();
				const float* proto = output_tensors[protoIdx].GetTensorData<float>();
				const auto& detShape = outShapes[detIdx];
				const auto& protoShape = outShapes[protoIdx];

				const int C = (int)detShape[1];
				const int N = (int)detShape[2];
				const int maskDim = (int)protoShape[1];
				const int protoH = (int)protoShape[2];
				const int protoW = (int)protoShape[3];
				const int numClasses = C - 4 - maskDim;
				if (numClasses <= 0 || maskDim <= 0 || protoH <= 0 || protoW <= 0) {
					return result;
				}

				auto getDet = [&](int c, int n) -> float { return det[c * N + n]; };

				// Heuristics to match export variations:
				// - Some exports output class scores as logits (need sigmoid), others already in [0,1].
				// - Some exports output boxes normalized to [0,1], others in pixel coordinates.
				bool applySigmoidToCls = false;
				bool boxesAreNormalized = false;
				{
					const int sampleN = std::min(N, 256);

					float clsMin = std::numeric_limits<float>::infinity();
					float clsMax = -std::numeric_limits<float>::infinity();
					for (int i = 0; i < sampleN; ++i) {
						for (int c = 0; c < numClasses; ++c) {
							const float v = getDet(4 + c, i);
							clsMin = std::min(clsMin, v);
							clsMax = std::max(clsMax, v);
						}
					}
					applySigmoidToCls = (clsMin < 0.0f || clsMax > 1.0f);

					float boxMax = 0.0f;
					float boxMin = std::numeric_limits<float>::infinity();
					for (int i = 0; i < sampleN; ++i) {
						boxMin = std::min(boxMin, getDet(0, i));
						boxMin = std::min(boxMin, getDet(1, i));
						boxMin = std::min(boxMin, getDet(2, i));
						boxMin = std::min(boxMin, getDet(3, i));
						boxMax = std::max(boxMax, getDet(0, i));
						boxMax = std::max(boxMax, getDet(1, i));
						boxMax = std::max(boxMax, getDet(2, i));
						boxMax = std::max(boxMax, getDet(3, i));
					}
					// If boxes are normalized, values are typically within [0,1] (allow small margin).
					boxesAreNormalized = (boxMin >= 0.0f && boxMax <= 2.0f);
				}

				struct Det {
					float score;
					int cls;
					float x1, y1, x2, y2;
					vector<float> coeff;
				};

				vector<Det> dets;
				dets.reserve(64);
				for (int i = 0; i < N; ++i) {
					float x = getDet(0, i);
					float y = getDet(1, i);
					float w = getDet(2, i);
					float h = getDet(3, i);
					if (boxesAreNormalized) {
						x *= (float)input_size_.width;
						y *= (float)input_size_.height;
						w *= (float)input_size_.width;
						h *= (float)input_size_.height;
					}

					int bestCls = 0;
					float bestScore = applySigmoidToCls ? Sigmoid(getDet(4, i)) : getDet(4, i);
					for (int c = 1; c < numClasses; ++c) {
						const float sRaw = getDet(4 + c, i);
						const float s = applySigmoidToCls ? Sigmoid(sRaw) : sRaw;
						if (s > bestScore) { bestScore = s; bestCls = c; }
					}
					if (bestScore < conf_threshold_) continue;

					float x1 = x - w * 0.5f;
					float y1 = y - h * 0.5f;
					float x2 = x + w * 0.5f;
					float y2 = y + h * 0.5f;

					x1 = std::clamp(x1, 0.0f, (float)input_size_.width - 1);
					y1 = std::clamp(y1, 0.0f, (float)input_size_.height - 1);
					x2 = std::clamp(x2, 0.0f, (float)input_size_.width - 1);
					y2 = std::clamp(y2, 0.0f, (float)input_size_.height - 1);

					Det d;
					d.score = bestScore;
					d.cls = bestCls;
					d.x1 = x1; d.y1 = y1; d.x2 = x2; d.y2 = y2;
					d.coeff.resize(maskDim);
					for (int m = 0; m < maskDim; ++m) {
						d.coeff[m] = getDet(4 + numClasses + m, i);
					}
					dets.push_back(std::move(d));
				}

				if (dets.empty()) return result;

				// NMS (simple)
				std::sort(dets.begin(), dets.end(), [](const Det& a, const Det& b) { return a.score > b.score; });
				auto iou = [](const Det& a, const Det& b) {
					const float xx1 = std::max(a.x1, b.x1);
					const float yy1 = std::max(a.y1, b.y1);
					const float xx2 = std::min(a.x2, b.x2);
					const float yy2 = std::min(a.y2, b.y2);
					const float w = std::max(0.0f, xx2 - xx1);
					const float h = std::max(0.0f, yy2 - yy1);
					const float inter = w * h;
					const float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
					const float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);
					const float uni = areaA + areaB - inter;
					return uni <= 0 ? 0.0f : (inter / uni);
					};
				const float nmsIou = 0.45f;
				vector<Det> keep;
				keep.reserve(dets.size());
				for (const auto& d : dets) {
					bool ok = true;
					for (const auto& k : keep) {
						if (iou(d, k) > nmsIou) { ok = false; break; }
					}
					if (ok) keep.push_back(d);
					if ((int)keep.size() >= 10) break;
				}

				// proto -> [maskDim, protoH*protoW]
				cv::Mat protoMat(maskDim, protoH * protoW, CV_32F);
				for (int m = 0; m < maskDim; ++m) {
					const float* src = proto + (size_t)m * protoH * protoW;
					float* dst = protoMat.ptr<float>(m);
					std::memcpy(dst, src, (size_t)protoH * protoW * sizeof(float));
				}

				cv::Mat bestMask;
				float bestArea = 0.0f;

				for (const auto& d : keep) {
					cv::Mat coeffMat(1, maskDim, CV_32F);
					std::memcpy(coeffMat.ptr<float>(0), d.coeff.data(), (size_t)maskDim * sizeof(float));

					cv::Mat logits = coeffMat * protoMat;
					cv::Mat maskProto = logits.reshape(1, protoH); // protoH x protoW

					// sigmoid
					for (int r = 0; r < maskProto.rows; ++r) {
						float* row = maskProto.ptr<float>(r);
						for (int c = 0; c < maskProto.cols; ++c) row[c] = Sigmoid(row[c]);
					}

					// upsample to input size
					cv::Mat maskInF;
					cv::resize(maskProto, maskInF, input_size_, 0, 0, cv::INTER_LINEAR);

					// bbox crop + threshold
					cv::Mat maskInU8 = cv::Mat::zeros(input_size_, CV_8U);
					const int bx1 = (int)std::floor(d.x1);
					const int by1 = (int)std::floor(d.y1);
					const int bx2 = (int)std::ceil(d.x2);
					const int by2 = (int)std::ceil(d.y2);
					const int x1i = std::clamp(bx1, 0, input_size_.width - 1);
					const int y1i = std::clamp(by1, 0, input_size_.height - 1);
					const int x2i = std::clamp(bx2, 0, input_size_.width);
					const int y2i = std::clamp(by2, 0, input_size_.height);
					const int bw = x2i - x1i;
					const int bh = y2i - y1i;
					if (bw <= 1 || bh <= 1) continue;

					cv::Rect bboxRect(x1i, y1i, bw, bh);
					cv::Mat roi = maskInF(bboxRect);
					cv::Mat roiU8;
					cv::threshold(roi, roiU8, 0.5, 255.0, cv::THRESH_BINARY);
					roiU8.convertTo(roiU8, CV_8U);
					roiU8.copyTo(maskInU8(bboxRect));

					// unpad to original aspect, then resize to original image size
					const int rx = std::clamp(lb.padX, 0, input_size_.width - 1);
					const int ry = std::clamp(lb.padY, 0, input_size_.height - 1);
					const int rw = std::clamp(lb.newW, 1, input_size_.width - rx);
					const int rh = std::clamp(lb.newH, 1, input_size_.height - ry);
					cv::Rect unpadRect(rx, ry, rw, rh);
					cv::Mat unpadded = maskInU8(unpadRect);

					cv::Mat maskOrig;
					cv::resize(unpadded, maskOrig, cv::Size(originalW, originalH), 0, 0, cv::INTER_NEAREST);

					float area = ComputeMinAreaRectArea(maskOrig);
					if (area > bestArea) {
						bestArea = area;
						bestMask = maskOrig.clone();
					}
				}

				if (bestMask.empty() || cv::countNonZero(bestMask) == 0) return result;

				result.mask = bestMask;
				result.elongation = ComputeElongationFromMask(result.mask);
				result.area = ComputeMinAreaRectArea(result.mask);
				result.valid = true;
				return result;
			}

			// Fallback: if model really outputs [1, num_masks, H, W] in output[1], keep the old path.
			int num_outputs = (int)output_tensors.size();
			if (num_outputs >= 2) {
				float* mask_data = output_tensors[1].GetTensorMutableData<float>();
				auto mask_shape = output_tensors[1].GetTensorTypeAndShapeInfo().GetShape();
				if (mask_shape.size() == 4 && mask_shape[0] == 1) {
					int mask_h = static_cast<int>(mask_shape[2]);
					int mask_w = static_cast<int>(mask_shape[3]);
					int num_masks = static_cast<int>(mask_shape[1]);

					cv::Mat best_mask;
					float best_area = 0;
					for (int i = 0; i < num_masks; i++) {
						cv::Mat mask(mask_h, mask_w, CV_32F, mask_data + i * mask_h * mask_w);
						cv::Mat mask_u8;
						cv::threshold(mask, mask_u8, 0.5, 255.0, cv::THRESH_BINARY);
						mask_u8.convertTo(mask_u8, CV_8U);

						float area = ComputeMinAreaRectArea(mask_u8);
						if (area > best_area) {
							best_area = area;
							best_mask = mask_u8.clone();
						}
					}

					if (!best_mask.empty()) {
						cv::resize(best_mask, result.mask, image.size(), 0, 0, cv::INTER_NEAREST);
						result.elongation = ComputeElongationFromMask(result.mask);
						result.area = ComputeMinAreaRectArea(result.mask);
						result.valid = true;
					}
				}
			}

			return result;
		}
		catch (const exception& e) {
			cerr << "YOLO inference error: " << e.what() << endl;
			return result;
		}
	}

private:

	float ComputeElongationFromMask(const cv::Mat& mask_u8) {
		cv::Mat mask_binary;
		if (mask_u8.channels() == 3) {
			cv::cvtColor(mask_u8, mask_binary, cv::COLOR_BGR2GRAY);
		}
		else {
			mask_binary = mask_u8.clone();
		}

		mask_binary = (mask_binary > 0);

		vector<cv::Point> points;
		cv::findNonZero(mask_binary, points);

		if (points.size() < 20) {
			return numeric_limits<float>::infinity();
		}

		// Convert points to matrix
		cv::Mat points_mat(points.size(), 2, CV_32F);
		for (size_t i = 0; i < points.size(); i++) {
			points_mat.at<float>(i, 0) = static_cast<float>(points[i].x);
			points_mat.at<float>(i, 1) = static_cast<float>(points[i].y);
		}

		cv::Mat mean, cov;
		cv::calcCovarMatrix(points_mat, cov, mean, cv::COVAR_NORMAL | cv::COVAR_ROWS);
		cov = cov / (points.size() - 1);

		cv::Mat eigenvalues, eigenvectors;
		cv::eigen(cov, eigenvalues, eigenvectors);

		// cv::eigen returns CV_64F (double) format, need to cast to float
		float lam1 = static_cast<float>(eigenvalues.at<double>(0));
		float lam2 = static_cast<float>(eigenvalues.at<double>(1));

		float major = sqrt(max(lam1, 1e-12f));
		float minor = sqrt(max(lam2, 1e-12f));

		if (minor < 1e-6f) {
			return numeric_limits<float>::infinity();
		}

		return major / minor;
	}

	float ComputeMinAreaRectArea(const cv::Mat& mask_u8) {
		cv::Mat mask_binary;
		if (mask_u8.channels() == 3) {
			cv::cvtColor(mask_u8, mask_binary, cv::COLOR_BGR2GRAY);
		}
		else {
			mask_binary = mask_u8.clone();
		}

		mask_binary = (mask_binary > 0) * 255;
		mask_binary.convertTo(mask_binary, CV_8U);

		vector<vector<cv::Point>> contours;
		cv::findContours(mask_binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

		if (contours.empty()) {
			return numeric_limits<float>::infinity();
		}

		auto largest_contour = max_element(contours.begin(), contours.end(),
			[](const vector<cv::Point>& a, const vector<cv::Point>& b) {
				return cv::contourArea(a) < cv::contourArea(b);
			});

		cv::RotatedRect rect = cv::minAreaRect(*largest_contour);
		cv::Size2f size = rect.size;
		float area = size.width * size.height;

		return area > 0 ? area : numeric_limits<float>::infinity();
	}
};

// ===================== Color Comparison Functions =====================
cv::Vec3f BGRToLAB(const cv::Vec3b& bgr) {
	cv::Mat bgr_mat(1, 1, CV_8UC3, cv::Scalar(bgr[0], bgr[1], bgr[2]));
	cv::Mat lab_mat;
	cv::cvtColor(bgr_mat, lab_mat, cv::COLOR_BGR2Lab);

	cv::Vec3b lab_u8 = lab_mat.at<cv::Vec3b>(0, 0);
	cv::Vec3f lab;
	lab[0] = lab_u8[0] * (100.0f / 255.0f);  // L*
	lab[1] = lab_u8[1] - 128.0f;              // a*
	lab[2] = lab_u8[2] - 128.0f;              // b*

	return lab;
}

cv::Vec3f MaskedLABMedian(const cv::Mat& img_bgr, const cv::Mat& mask_u8, float trim_L_percent = 1.0f) {
	cv::Mat mask_binary;
	if (mask_u8.channels() == 3) {
		cv::cvtColor(mask_u8, mask_binary, cv::COLOR_BGR2GRAY);
	}
	else {
		mask_binary = mask_u8.clone();
	}
	mask_binary = (mask_binary > 0);

	vector<cv::Point> points;
	cv::findNonZero(mask_binary, points);

	if (points.size() < 50) {
		return cv::Vec3f(0, 0, 0);  // Invalid
	}

	// OPTIMIZATION: Convert entire image to LAB once (much faster than per-pixel conversion)
	cv::Mat lab_img;
	cv::cvtColor(img_bgr, lab_img, cv::COLOR_BGR2Lab);

	vector<float> L_values;
	vector<float> a_values;
	vector<float> b_values;
	L_values.reserve(points.size());
	a_values.reserve(points.size());
	b_values.reserve(points.size());

	// Extract LAB values from pre-converted image (much faster)
	for (const auto& pt : points) {
		cv::Vec3b lab_u8 = lab_img.at<cv::Vec3b>(pt);
		// Convert from uint8 to float LAB (L*: 0-100, a*,b*: -128 to 127)
		float L = lab_u8[0] * (100.0f / 255.0f);
		float a = static_cast<float>(lab_u8[1]) - 128.0f;
		float b = static_cast<float>(lab_u8[2]) - 128.0f;
		L_values.push_back(L);
		a_values.push_back(a);
		b_values.push_back(b);
	}

	// L* trimming
	if (trim_L_percent > 0 && L_values.size() > 0) {
		sort(L_values.begin(), L_values.end());
		size_t trim_count = static_cast<size_t>(L_values.size() * trim_L_percent / 100.0f);
		if (trim_count > 0 && trim_count < L_values.size()) {
			L_values.erase(L_values.begin(), L_values.begin() + trim_count);
			L_values.erase(L_values.end() - trim_count, L_values.end());
		}
	}

	// Median calculation
	sort(L_values.begin(), L_values.end());
	sort(a_values.begin(), a_values.end());
	sort(b_values.begin(), b_values.end());

	float L_median = L_values[L_values.size() / 2];
	float a_median = a_values[a_values.size() / 2];
	float b_median = b_values[b_values.size() / 2];

	return cv::Vec3f(L_median, a_median, b_median);
}

float DeltaE2000(const cv::Vec3f& lab1, const cv::Vec3f& lab2) {
	float L1 = lab1[0], a1 = lab1[1], b1 = lab1[2];
	float L2 = lab2[0], a2 = lab2[1], b2 = lab2[2];

	float C1 = sqrt(a1 * a1 + b1 * b1);
	float C2 = sqrt(a2 * a2 + b2 * b2);
	float Cbar = (C1 + C2) / 2.0f;

	float Cbar7 = pow(Cbar, 7.0f);
	float G = 0.5f * (1.0f - sqrt(Cbar7 / (Cbar7 + pow(25.0f, 7.0f))));

	float a1p = (1.0f + G) * a1;
	float a2p = (1.0f + G) * a2;
	float C1p = sqrt(a1p * a1p + b1 * b1);
	float C2p = sqrt(a2p * a2p + b2 * b2);

	auto hp = [](float ap, float b) -> float {
		if (ap == 0 && b == 0) return 0.0f;
		float h = atan2(b, ap) * 180.0f / 3.14159265359f;
		if (h < 0) h += 360.0f;
		return h;
		};

	float h1p = hp(a1p, b1);
	float h2p = hp(a2p, b2);

	float dLp = L2 - L1;
	float dCp = C2p - C1p;

	float dhp = 0.0f;
	if (C1p * C2p != 0) {
		float dh = h2p - h1p;
		if (dh > 180.0f) dh -= 360.0f;
		else if (dh < -180.0f) dh += 360.0f;
		dhp = dh;
	}

	float dHp = 2.0f * sqrt(C1p * C2p) * sin((dhp / 2.0f) * 3.14159265359f / 180.0f);

	float Lbarp = (L1 + L2) / 2.0f;
	float Cbarp = (C1p + C2p) / 2.0f;

	float hbarp = 0.0f;
	if (C1p * C2p == 0) {
		hbarp = h1p + h2p;
	}
	else {
		float hsum = h1p + h2p;
		float hdiff = abs(h1p - h2p);
		if (hdiff > 180.0f) {
			hbarp = (hsum < 360.0f) ? (hsum + 360.0f) / 2.0f : (hsum - 360.0f) / 2.0f;
		}
		else {
			hbarp = hsum / 2.0f;
		}
	}

	float T = 1.0f
		- 0.17f * cos((hbarp - 30.0f) * 3.14159265359f / 180.0f)
		+ 0.24f * cos(2.0f * hbarp * 3.14159265359f / 180.0f)
		+ 0.32f * cos((3.0f * hbarp + 6.0f) * 3.14159265359f / 180.0f)
		- 0.20f * cos((4.0f * hbarp - 63.0f) * 3.14159265359f / 180.0f);

	float dtheta = 30.0f * exp(-pow((hbarp - 275.0f) / 25.0f, 2.0f));
	float RC = (Cbarp > 0) ? 2.0f * sqrt(pow(Cbarp, 7.0f) / (pow(Cbarp, 7.0f) + pow(25.0f, 7.0f))) : 0.0f;

	float SL = 1.0f + (0.015f * pow(Lbarp - 50.0f, 2.0f)) / sqrt(20.0f + pow(Lbarp - 50.0f, 2.0f));
	float SC = 1.0f + 0.045f * Cbarp;
	float SH = 1.0f + 0.015f * Cbarp * T;

	float RT = -sin(2.0f * dtheta * 3.14159265359f / 180.0f) * RC;

	float kL = 1.0f, kC = 1.0f, kH = 1.0f;
	float dE = sqrt(
		pow(dLp / (kL * SL), 2.0f) +
		pow(dCp / (kC * SC), 2.0f) +
		pow(dHp / (kH * SH), 2.0f) +
		RT * (dCp / (kC * SC)) * (dHp / (kH * SH))
	);

	return dE;
}

float ComparePillColor(const cv::Mat& img1_bgr, const cv::Mat& mask1,
	const cv::Mat& img2_bgr, const cv::Mat& mask2,
	float trim_L_percent = 1.0f) {
	// OPTIMIZATION: Only clone/resize if sizes differ
	cv::Mat mask1_resized;
	cv::Mat mask2_resized;

	// Resize masks if needed (avoid unnecessary clone)
	if (img1_bgr.size() != mask1.size()) {
		cv::resize(mask1, mask1_resized, img1_bgr.size(), 0, 0, cv::INTER_NEAREST);
	}
	else {
		mask1_resized = mask1;  // No clone needed if sizes match
	}
	if (img2_bgr.size() != mask2.size()) {
		cv::resize(mask2, mask2_resized, img2_bgr.size(), 0, 0, cv::INTER_NEAREST);
	}
	else {
		mask2_resized = mask2;  // No clone needed if sizes match
	}

	cv::Vec3f lab1_med = MaskedLABMedian(img1_bgr, mask1_resized, trim_L_percent);
	cv::Vec3f lab2_med = MaskedLABMedian(img2_bgr, mask2_resized, trim_L_percent);

	if (lab1_med[0] == 0 && lab1_med[1] == 0 && lab1_med[2] == 0) {
		return numeric_limits<float>::infinity();
	}
	if (lab2_med[0] == 0 && lab2_med[1] == 0 && lab2_med[2] == 0) {
		return numeric_limits<float>::infinity();
	}

	return DeltaE2000(lab1_med, lab2_med);
}

// ===================== Helper Functions =====================
BBox ParseBBoxFromFilename(const string& filename) {
	BBox bbox = { -1, -1, -1, -1 };
	size_t pos = filename.find('(');
	if (pos == string::npos) return bbox;

	size_t pos2 = filename.find(')', pos);
	if (pos2 == string::npos) return bbox;

	string bbox_str = filename.substr(pos + 1, pos2 - pos - 1);
	istringstream iss(bbox_str);
	char comma;
	if (iss >> bbox.x1 >> comma >> bbox.y1 >> comma >> bbox.x2 >> comma >> bbox.y2) {
		return bbox;
	}

	return bbox;
}

vector<string> GetImageFiles(const string& folder, int start_tray = -1, int end_tray = -1) {
	vector<string> files;
	if (!fs::exists(folder) || !fs::is_directory(folder)) {
		return files;
	}

	auto extractTrayNumber = [](const string& filename) -> int {
		// Extract tray number from filename like "img_1_2_(x,y,x,y)_(conf).bmp"
		// Format: img_{tray_num}_{idx}_...
		size_t pos1 = filename.find("img_");
		if (pos1 == string::npos) return -1;
		size_t pos2 = filename.find("_", pos1 + 4);
		if (pos2 == string::npos) return -1;
		try {
			string num_str = filename.substr(pos1 + 4, pos2 - pos1 - 4);
			return stoi(num_str);
		}
		catch (...) {
			return -1;
		}
		};

	for (const auto& entry : fs::directory_iterator(folder)) {
		if (entry.is_regular_file()) {
			string ext = entry.path().extension().string();
			transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
			if (ext == ".bmp" || ext == ".png" || ext == ".jpg" || ext == ".jpeg") {
				string filename = entry.path().filename().string();

				// Filter by tray range if specified
				if (start_tray >= 1 && end_tray >= start_tray) {
					int tray_num = extractTrayNumber(filename);
					if (tray_num >= start_tray && tray_num <= end_tray) {
						files.push_back(filename);
					}
				}
				else {
					// No range specified, include all
					files.push_back(filename);
				}
			}
		}
	}

	sort(files.begin(), files.end());
	return files;
}

// ===================== Mask Geometry Helpers (shared) =====================
static float ComputeElongationFromMaskU8(const cv::Mat& mask_u8) {
	cv::Mat mask_binary;
	if (mask_u8.channels() == 3) {
		cv::cvtColor(mask_u8, mask_binary, cv::COLOR_BGR2GRAY);
	}
	else {
		mask_binary = mask_u8.clone();
	}
	mask_binary = (mask_binary > 0);

	vector<cv::Point> points;
	cv::findNonZero(mask_binary, points);
	if (points.size() < 20) {
		return numeric_limits<float>::infinity();
	}

	cv::Mat points_mat((int)points.size(), 2, CV_32F);
	for (int i = 0; i < (int)points.size(); i++) {
		points_mat.at<float>(i, 0) = (float)points[i].x;
		points_mat.at<float>(i, 1) = (float)points[i].y;
	}

	cv::Mat mean, cov;
	cv::calcCovarMatrix(points_mat, cov, mean, cv::COVAR_NORMAL | cv::COVAR_ROWS);
	cov = cov / std::max(1, (int)points.size() - 1);

	cv::Mat eigenvalues, eigenvectors;
	cv::eigen(cov, eigenvalues, eigenvectors);

	float lam1 = (float)eigenvalues.at<double>(0);
	float lam2 = (float)eigenvalues.at<double>(1);
	float major = std::sqrt(std::max(lam1, 1e-12f));
	float minor = std::sqrt(std::max(lam2, 1e-12f));
	if (minor < 1e-6f) return numeric_limits<float>::infinity();
	return major / minor;
}

static float ComputeMinAreaRectAreaU8(const cv::Mat& mask_u8) {
	cv::Mat mask_binary;
	if (mask_u8.channels() == 3) {
		cv::cvtColor(mask_u8, mask_binary, cv::COLOR_BGR2GRAY);
	}
	else {
		mask_binary = mask_u8.clone();
	}
	mask_binary = (mask_binary > 0) * 255;
	mask_binary.convertTo(mask_binary, CV_8U);

	vector<vector<cv::Point>> contours;
	cv::findContours(mask_binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	if (contours.empty()) return numeric_limits<float>::infinity();

	auto largest_contour = max_element(contours.begin(), contours.end(),
		[](const vector<cv::Point>& a, const vector<cv::Point>& b) {
			return cv::contourArea(a) < cv::contourArea(b);
		});
	cv::RotatedRect rect = cv::minAreaRect(*largest_contour);
	cv::Size2f size = rect.size;
	float area = size.width * size.height;
	return area > 0 ? area : numeric_limits<float>::infinity();
}

// ===================== Hungarian (Max-Weight Bipartite Matching) =====================
// Returns assignment for each row: assigned column index, or -1.
// Maximizes total weight. Rows/cols can be rectangular; internal uses a min-cost Hungarian.
static vector<int> HungarianMaximize(const vector<vector<float>>& weights, float invalid_weight = -1e8f) {
	const int n = (int)weights.size();
	if (n == 0) return {};
	const int m = (int)weights[0].size();
	if (m == 0) return vector<int>(n, -1);

	// Ensure all rows have same col count
	for (int i = 1; i < n; ++i) {
		if ((int)weights[i].size() != m) {
			return vector<int>(n, -1);
		}
	}

	bool transposed = false;
	int rows = n, cols = m;
	vector<vector<float>> w = weights;
	if (rows > cols) {
		// transpose to satisfy cols >= rows
		transposed = true;
		rows = m;
		cols = n;
		w.assign(rows, vector<float>(cols, invalid_weight));
		for (int i = 0; i < n; ++i) {
			for (int j = 0; j < m; ++j) {
				w[j][i] = weights[i][j];
			}
		}
	}

	float maxW = 0.0f;
	bool anyValid = false;
	for (int i = 0; i < rows; ++i) {
		for (int j = 0; j < cols; ++j) {
			const float v = w[i][j];
			if (v > invalid_weight / 2.0f) {
				maxW = std::max(maxW, v);
				anyValid = true;
			}
		}
	}
	if (!anyValid) {
		return vector<int>(n, -1);
	}

	// Hungarian (min cost) for 1-indexed arrays; requires cols >= rows
	const float BIG = 1e9f;
	vector<float> u(rows + 1, 0.0f), v(cols + 1, 0.0f);
	vector<int> p(cols + 1, 0), way(cols + 1, 0);

	auto cost = [&](int i1, int j1) -> float {
		// i1, j1 are 1-based
		const float ww = w[i1 - 1][j1 - 1];
		if (ww <= invalid_weight / 2.0f) return BIG;
		// maximize weight -> minimize (maxW - weight)
		return (maxW - ww);
		};

	for (int i = 1; i <= rows; ++i) {
		p[0] = i;
		int j0 = 0;
		vector<float> minv(cols + 1, BIG);
		vector<char> used(cols + 1, false);
		do {
			used[j0] = true;
			const int i0 = p[j0];
			float delta = BIG;
			int j1 = 0;
			for (int j = 1; j <= cols; ++j) {
				if (used[j]) continue;
				float cur = cost(i0, j) - u[i0] - v[j];
				if (cur < minv[j]) {
					minv[j] = cur;
					way[j] = j0;
				}
				if (minv[j] < delta) {
					delta = minv[j];
					j1 = j;
				}
			}
			for (int j = 0; j <= cols; ++j) {
				if (used[j]) {
					u[p[j]] += delta;
					v[j] -= delta;
				}
				else {
					minv[j] -= delta;
				}
			}
			j0 = j1;
		} while (p[j0] != 0);

		// augmenting
		do {
			int j1 = way[j0];
			p[j0] = p[j1];
			j0 = j1;
		} while (j0 != 0);
	}

	// p[j] = matched row for column j
	vector<int> rowToCol(rows, -1);
	for (int j = 1; j <= cols; ++j) {
		if (p[j] > 0 && p[j] <= rows) {
			rowToCol[p[j] - 1] = j - 1;
		}
	}

	if (!transposed) {
		vector<int> ans(n, -1);
		for (int i = 0; i < n; ++i) {
			ans[i] = (i < rows) ? rowToCol[i] : -1;
		}
		return ans;
	}

	// If transposed: original rows=n were columns in transposed.
	// We need mapping from original row i to assigned original column.
	// In transposed solution, "row" corresponds to original column, and "col" corresponds to original row.
	// rowToCol[row(origCol)] = col(origRow)
	vector<int> ans(n, -1);
	for (int origRow = 0; origRow < n; ++origRow) {
		int chosenOrigCol = -1;
		for (int origCol = 0; origCol < m; ++origCol) {
			const int colInTransposed = rowToCol[origCol];
			if (colInTransposed == origRow) {
				chosenOrigCol = origCol;
				break;
			}
		}
		ans[origRow] = chosenOrigCol;
	}
	return ans;
}

vector<string> ReadSubfolderNames(const string& txt_file) {
	vector<string> names;
	ifstream file(txt_file);
	if (!file.is_open()) {
		cerr << "Failed to open file: " << txt_file << endl;
		return names;
	}

	string line;
	while (getline(file, line)) {
		// Trim whitespace
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		if (!line.empty()) {
			names.push_back(line);
		}
	}

	return names;
}

// ===================== Main Processing Class =====================
class PredictionProcessing {
private:
	SiameseNetwork siamese_model;
	YOLOSegmentation yolo_seg_model;
	string cropped_dir;
	string prescription_dir;
	string result_dir;
	string siamese_model_path;
	string yolo_seg_model_path;
	float siamese_threshold;

public:
	map<int, PrescMaskInfo> LoadPrescriptionMasks(const vector<string>& subfolder_names) {
		cout << "\n=== Load prescription pill masks ===" << endl;

		map<int, PrescMaskInfo> presc_mask_info;
		if (!fs::exists(prescription_dir) || !fs::is_directory(prescription_dir)) {
			cout << "Warning: prescription_dir is not a directory: " << prescription_dir << endl;
			return presc_mask_info;
		}

		for (int j = 0; j < (int)subfolder_names.size(); ++j) {
			const string& presc_name = subfolder_names[j];
			PrescMaskInfo info;

			vector<fs::path> mask_paths;
			for (const auto& entry : fs::directory_iterator(prescription_dir)) {
				if (!entry.is_regular_file()) continue;
				const fs::path p = entry.path();
				const string filename = p.filename().string();
				const string ext = p.extension().string();

				// match: {presc_name}_mask_*.bmp (also allow png)
				if (filename.rfind(presc_name + "_mask_", 0) != 0) continue;
				string extLower = ext;
				transform(extLower.begin(), extLower.end(), extLower.begin(), ::tolower);
				if (extLower != ".bmp" && extLower != ".png" && extLower != ".jpg" && extLower != ".jpeg") continue;

				mask_paths.push_back(p);
			}
			sort(mask_paths.begin(), mask_paths.end());

			for (const auto& mask_path : mask_paths) {
				cv::Mat mask_u8 = cv::imread(mask_path.string(), cv::IMREAD_GRAYSCALE);
				if (mask_u8.empty()) continue;
				mask_u8 = (mask_u8 > 127);
				mask_u8.convertTo(mask_u8, CV_8U, 255.0);

				MaskInfo mi;
				mi.mask_path = mask_path.string();
				mi.elongation = ComputeElongationFromMaskU8(mask_u8);
				mi.area = ComputeMinAreaRectAreaU8(mask_u8);
				info.masks.push_back(mi);
			}

			if (!info.masks.empty()) {
				cout << "  " << presc_name << ": " << info.masks.size() << " masks" << endl;
				presc_mask_info[j] = info;
			}
			else {
				cout << "  Warning: no masks found for " << presc_name << endl;
			}
		}

		return presc_mask_info;
	}

	// Python's match_with_bipartite_matching(): not Hungarian.
	// It computes 0..3 score per (crop, presc) and then selects best presc per crop
	// (tie-break by Siamese prob). best_score==0 is kept for later validation.
	void MatchWithBipartiteMatching(
		const map<int, map<string, vector<int>>>& C,
		const map<string, MaskInfo>& mask_info,
		const map<int, PrescMaskInfo>& presc_mask_info,
		const vector<string>& subfolder_names,
		const vector<string>& presc_paths,
		const vector<int>& numbers,
		const vector<int>& tray_indices,
		map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		map<int, vector<CropMatch>>& unmatched_by_tray,
		float elong_tolerance = 0.1f,
		float area_tolerance = 0.2f,
		float color_threshold = 2.0f) {

		cout << "\n=== Reassignment matching (Python match_with_bipartite_matching) ===" << endl;

		// (A) Build siamese_probs from presc_tray_matches 
		map<int, map<string, map<int, float>>> siamese_probs; // tray -> crop -> presc_j -> prob
		for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
			auto it = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
			if (it == subfolder_names.end()) continue;
			int presc_j = (int)distance(subfolder_names.begin(), it);
			for (const auto& [tray_idx, items] : tray_dict) {
				for (const auto& [crop_name, prob] : items) {
					siamese_probs[tray_idx][crop_name][presc_j] = prob;
				}
			}
		}

		auto get_prob = [&](int tray_idx, const string& crop_name, int presc_j) -> float {
			auto tIt = siamese_probs.find(tray_idx);
			if (tIt == siamese_probs.end()) return 0.0f;
			auto cIt = tIt->second.find(crop_name);
			if (cIt == tIt->second.end()) return 0.0f;
			auto pIt = cIt->second.find(presc_j);
			return (pIt == cIt->second.end()) ? 0.0f : pIt->second;
			};

		// Cache presc images/masks
		map<int, cv::Mat> presc_img_cache;
		map<string, cv::Mat> presc_mask_cache;
		auto load_presc_img = [&](int presc_j) -> cv::Mat {
			auto it = presc_img_cache.find(presc_j);
			if (it != presc_img_cache.end()) return it->second;
			if (presc_j < 0 || presc_j >= (int)presc_paths.size()) return cv::Mat();
			cv::Mat img = cv::imread((fs::path(prescription_dir) / presc_paths[presc_j]).string());
			presc_img_cache[presc_j] = img;
			return img;
			};
		auto load_presc_mask = [&](const string& mask_path) -> cv::Mat {
			auto it = presc_mask_cache.find(mask_path);
			if (it != presc_mask_cache.end()) return it->second;
			cv::Mat m = cv::imread(mask_path, cv::IMREAD_GRAYSCALE);
			presc_mask_cache[mask_path] = m;
			return m;
			};

		for (const auto& [tray_idx, tray_C] : C) {
			if (tray_C.empty()) continue;

			cout << "\nTray " << tray_idx << " processing..." << endl;

			vector<string> crop_list;
			crop_list.reserve(tray_C.size());
			for (const auto& [crop_name, _] : tray_C) crop_list.push_back(crop_name);

			// presc_list = union of candidates
			set<int> presc_set;
			for (const auto& [crop_name, presc_j_list] : tray_C) {
				for (int pj : presc_j_list) presc_set.insert(pj);
			}
			vector<int> presc_list(presc_set.begin(), presc_set.end());
			sort(presc_list.begin(), presc_list.end());

			const float INF_COST = 9999.0f;
			const int n_crops = (int)crop_list.size();
			const int n_prescs = (int)presc_list.size();

			// Pre-load all crop images and masks once (major performance improvement - eliminates repeated I/O)
			map<string, cv::Mat> crop_img_cache;
			map<string, cv::Mat> crop_mask_cache;
			for (const string& crop_name : crop_list) {
				auto miIt = mask_info.find(crop_name);
				if (miIt == mask_info.end()) continue;

				// Pre-compute paths once to avoid repeated string operations
				string crop_path_str = (fs::path(cropped_dir) / crop_name).string();
				cv::Mat crop_img = cv::imread(crop_path_str, cv::IMREAD_COLOR);
				if (!crop_img.empty()) {
					crop_img_cache[crop_name] = crop_img;
				}

				cv::Mat crop_mask = cv::imread(miIt->second.mask_path, cv::IMREAD_GRAYSCALE);
				if (!crop_mask.empty()) {
					crop_mask = (crop_mask > 127);
					crop_mask_cache[crop_name] = crop_mask;
				}
			}

			vector<vector<float>> cost_matrix(n_crops, vector<float>(n_prescs, INF_COST));

			for (int i = 0; i < n_crops; ++i) {
				const string& crop_name = crop_list[i];
				auto miIt = mask_info.find(crop_name);
				if (miIt == mask_info.end()) continue;

				const float crop_elong = miIt->second.elongation;
				const float crop_area = miIt->second.area;

				// Use cached images (no I/O here - major performance improvement)
				auto imgIt = crop_img_cache.find(crop_name);
				auto maskIt = crop_mask_cache.find(crop_name);
				if (imgIt == crop_img_cache.end() || maskIt == crop_mask_cache.end()) continue;

				const cv::Mat& crop_img = imgIt->second;
				const cv::Mat& crop_mask = maskIt->second;

				const auto& candidates = tray_C.at(crop_name);
				for (int j = 0; j < n_prescs; ++j) {
					const int presc_j = presc_list[j];
					if (find(candidates.begin(), candidates.end(), presc_j) == candidates.end()) continue;
					if (presc_j < 0 || presc_j >= (int)subfolder_names.size()) continue;
					auto pmIt = presc_mask_info.find(presc_j);
					if (pmIt == presc_mask_info.end()) continue;

					cv::Mat presc_img = load_presc_img(presc_j);
					if (presc_img.empty()) continue;

					int best_score = 0;
					// Cache crop image size to avoid repeated access
					const int crop_h = crop_img.rows;
					const int crop_w = crop_img.cols;

					// Cache resized presc images/masks per (presc_j, size, mask_path) to avoid redundant resizing
					// Key: (presc_j, width, height, mask_path) -> resized image/mask
					using ResizeKey = tuple<int, int, int, string>;
					static map<ResizeKey, cv::Mat> presc_img_resized_cache;
					static map<ResizeKey, cv::Mat> presc_mask_resized_cache;

					for (const auto& presc_mask : pmIt->second.masks) {
						int score = 0;
						if (isfinite(crop_elong) && isfinite(presc_mask.elongation)) {
							if (std::abs(crop_elong - presc_mask.elongation) < elong_tolerance) score += 1;
						}
						if (isfinite(crop_area) && isfinite(presc_mask.area)) {
							float area_ratio = std::max(crop_area, presc_mask.area) /
								(std::min(crop_area, presc_mask.area) + 1e-6f);
							if (area_ratio < 1.0f + area_tolerance) score += 1;
						}

						cv::Mat pm = load_presc_mask(presc_mask.mask_path);
						if (!pm.empty()) {
							cv::Mat presc_mask_u8 = (pm > 127);

							// Check cache for resized presc image/mask
							ResizeKey img_key = make_tuple(presc_j, crop_w, crop_h, string());
							ResizeKey mask_key = make_tuple(presc_j, crop_w, crop_h, presc_mask.mask_path);

							cv::Mat presc_img_rs, presc_mask_rs;

							// Get or create resized presc image
							auto img_cache_it = presc_img_resized_cache.find(img_key);
							if (img_cache_it != presc_img_resized_cache.end()) {
								presc_img_rs = img_cache_it->second;
							}
							else {
								if (presc_img.size() != crop_img.size()) {
									cv::resize(presc_img, presc_img_rs, cv::Size(crop_w, crop_h), 0, 0, cv::INTER_AREA);
									presc_img_resized_cache[img_key] = presc_img_rs.clone();
								}
								else {
									presc_img_rs = presc_img;
								}
							}

							// Get or create resized presc mask
							auto mask_cache_it = presc_mask_resized_cache.find(mask_key);
							if (mask_cache_it != presc_mask_resized_cache.end()) {
								presc_mask_rs = mask_cache_it->second;
							}
							else {
								if (presc_mask_u8.size() != crop_mask.size()) {
									cv::resize(presc_mask_u8, presc_mask_rs, cv::Size(crop_w, crop_h), 0, 0, cv::INTER_NEAREST);
									presc_mask_rs = (presc_mask_rs > 0);
									presc_mask_resized_cache[mask_key] = presc_mask_rs.clone();
								}
								else {
									presc_mask_rs = presc_mask_u8;
								}
							}

							float color_score = ComparePillColor(crop_img, crop_mask, presc_img_rs, presc_mask_rs);
							if (isfinite(color_score) && color_score < color_threshold) score += 1;
						}

						best_score = std::max(best_score, score);
					}
					cost_matrix[i][j] = (float)best_score;
				}
			}

			// (B) Per-crop best selection (no Hungarian)
			for (int i = 0; i < n_crops; ++i) {
				const string& crop_name = crop_list[i];
				const auto& candidates = tray_C.at(crop_name);

				float best_score = 0.0f;
				int best_presc_idx = -1; // index into presc_list
				float best_prob = 0.0f;

				for (int j = 0; j < n_prescs; ++j) {
					const int presc_j = presc_list[j];
					if (find(candidates.begin(), candidates.end(), presc_j) == candidates.end()) continue;

					const float score = cost_matrix[i][j];
					if (std::abs(score - INF_COST) < 1e-3f) continue;

					const float prob = get_prob(tray_idx, crop_name, presc_j);
					if (score > best_score || (score == best_score && prob > best_prob)) {
						best_score = score;
						best_presc_idx = j;
						best_prob = prob;
					}
				}

				if (best_presc_idx < 0) {
					// fallback: first non-INF candidate even if score==0
					for (int j = 0; j < n_prescs; ++j) {
						const float score = cost_matrix[i][j];
						if (std::abs(score - INF_COST) < 1e-3f) continue;
						best_presc_idx = j;
						best_score = score;
						best_prob = get_prob(tray_idx, crop_name, presc_list[j]);
						break;
					}
				}

				// Remove crop from any existing presc match in this tray (python does this)
				for (auto& [existing_presc_name, tray_dict] : presc_tray_matches) {
					auto itTray = tray_dict.find(tray_idx);
					if (itTray == tray_dict.end()) continue;
					auto& vec = itTray->second;
					vec.erase(remove_if(vec.begin(), vec.end(),
						[&](const pair<string, float>& x) { return x.first == crop_name; }),
						vec.end());
				}

				// Also remove from unmatched list (will re-add if needed)
				auto itU = unmatched_by_tray.find(tray_idx);
				if (itU != unmatched_by_tray.end()) {
					auto& vec = itU->second;
					vec.erase(remove_if(vec.begin(), vec.end(),
						[&](const CropMatch& x) { return x.crop_name == crop_name; }),
						vec.end());
				}

				if (best_presc_idx < 0) {
					// No candidate -> keep as No Match with best guess empty
					CropMatch nm{ crop_name, 0.0f, "" };
					unmatched_by_tray[tray_idx].push_back(nm);
					continue;
				}

				const int presc_j = presc_list[best_presc_idx];
				const string& presc_name = subfolder_names[presc_j];
				const float prob = (best_prob > 0.0f) ? best_prob : 0.8f; // python default
				presc_tray_matches[presc_name][tray_idx].push_back({ crop_name, prob });

				// best_score==0 -> keep in unmatched with info=presc_name for later validation
				if (best_score == 0.0f) {
					CropMatch nm{ crop_name, prob, presc_name };
					unmatched_by_tray[tray_idx].push_back(nm);
				}
			}
		}
	}

	void ValidateNoMatchCrops(
		map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		map<int, vector<CropMatch>>& unmatched_by_tray,
		const vector<string>& subfolder_names,
		const vector<int>& numbers,
		const vector<int>& tray_indices) {

		cout << "\n============================================================" << endl;
		cout << "Validate No Match crops (best_cost == 0) ..." << endl;
		cout << "============================================================" << endl;

		for (int tray_idx : tray_indices) {
			auto itU = unmatched_by_tray.find(tray_idx);
			if (itU == unmatched_by_tray.end()) continue;

			// Collect actions like python (remove from presc vs remove from unmatched)
			vector<pair<string, string>> remove_from_presc; // (presc_name, crop_name)
			vector<string> remove_from_unmatched;           // crop_name

			for (const auto& item : itU->second) {
				const string& crop_name = item.crop_name;
				const float prob = item.prob;
				const string& info = item.best_name; // info field in python
				(void)prob;

				if (info.empty()) continue; // only best_cost==0 markers

				bool found = false;
				string matched_presc;
				int required_count = 1;
				int matched_crop_count = 0;

				for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
					auto itT = tray_dict.find(tray_idx);
					if (itT == tray_dict.end()) continue;
					const auto& vec = itT->second;
					bool in = any_of(vec.begin(), vec.end(), [&](const auto& p) { return p.first == crop_name; });
					if (!in) continue;

					matched_presc = presc_name;
					found = true;
					auto itName = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
					if (itName != subfolder_names.end()) {
						const int presc_j = (int)distance(subfolder_names.begin(), itName);
						required_count = (presc_j >= 0 && presc_j < (int)numbers.size()) ? numbers[presc_j] : 1;
					}
					matched_crop_count = (int)vec.size();
					break;
				}

				if (!found) continue;

				if (matched_crop_count != required_count) {
					// keep in No Match, remove from presc
					cout << "  Tray " << tray_idx << ": " << crop_name
						<< " keep No Match and remove from presc_tray_matches "
						<< "(required=" << required_count << ", matched=" << matched_crop_count << ")" << endl;
					remove_from_presc.push_back({ matched_presc, crop_name });
				}
				else {
					// remove from No Match, keep match
					cout << "  Tray " << tray_idx << ": " << crop_name
						<< " remove from No Match (required==matched==" << required_count << ")" << endl;
					remove_from_unmatched.push_back(crop_name);
				}
			}

			// Apply removals
			for (const auto& [presc_name, crop_name] : remove_from_presc) {
				auto itP = presc_tray_matches.find(presc_name);
				if (itP == presc_tray_matches.end()) continue;
				auto itT = itP->second.find(tray_idx);
				if (itT == itP->second.end()) continue;
				auto& vec = itT->second;
				vec.erase(remove_if(vec.begin(), vec.end(),
					[&](const pair<string, float>& x) { return x.first == crop_name; }),
					vec.end());
			}
			if (!remove_from_unmatched.empty()) {
				auto& vec = itU->second;
				vec.erase(remove_if(vec.begin(), vec.end(),
					[&](const CropMatch& x) {
						return find(remove_from_unmatched.begin(), remove_from_unmatched.end(), x.crop_name) != remove_from_unmatched.end();
					}),
					vec.end());
			}
		}

		cout << "Validate No Match crops done." << endl;
	}

	void FilterInvalidCropsFromNoMatch(
		map<int, vector<CropMatch>>& unmatched_by_tray,
		const string& cropped_masks_path,
		int max_components = 4,
		float bg_color_similarity_threshold = 15.0f) {

		cout << "\n============================================================" << endl;
		cout << "Filter invalid crops from No Match (Python filter_invalid_crops_from_no_match) ..." << endl;
		cout << "============================================================" << endl;

		// Collect all No Match crops
		map<string, int> crop_to_tray;
		vector<string> crops;
		for (const auto& [tray_idx, items] : unmatched_by_tray) {
			for (const auto& it : items) {
				crop_to_tray[it.crop_name] = tray_idx;
				crops.push_back(it.crop_name);
			}
		}
		if (crops.empty()) {
			cout << "No Match crops empty. Skip filter." << endl;
			return;
		}

		// Load existing masks from cropped_masks_path instead of generating new ones
		map<string, MaskInfo> mask_info_nomatch;
		fs::path masks_dir(cropped_masks_path);

		if (!fs::exists(masks_dir) || !fs::is_directory(masks_dir)) {
			cout << "Warning: cropped_masks_path does not exist: " << cropped_masks_path << ". Skip filter." << endl;
			return;
		}

		// Load mask files from cropped_masks_path
		// Mask file format: {crop_stem}_mask_({elongation:.2f}, {area:.1f}).bmp
		for (const string& crop_name : crops) {
			// Extract crop stem (filename without extension)
			string crop_stem;
			size_t dot_pos = crop_name.find_last_of('.');
			if (dot_pos != string::npos) {
				crop_stem = crop_name.substr(0, dot_pos);
			}
			else {
				crop_stem = crop_name;
			}

			// Search for mask file with pattern: {crop_stem}_mask_*.bmp
			bool found = false;
			for (const auto& entry : fs::directory_iterator(masks_dir)) {
				if (!entry.is_regular_file()) continue;
				const fs::path p = entry.path();
				const string filename = p.filename().string();

				// Check if filename starts with {crop_stem}_mask_ and ends with .bmp
				if (filename.find(crop_stem + "_mask_") == 0 && p.extension().string() == ".bmp") {
					// Try to parse elongation and area from filename
					// Format: {crop_stem}_mask_({elongation:.2f}, {area:.1f}).bmp
					string mask_path_str = p.string();
					cv::Mat mask_test = cv::imread(mask_path_str, cv::IMREAD_GRAYSCALE);
					if (!mask_test.empty()) {
						MaskInfo info;
						info.mask_path = mask_path_str;

						// Try to extract elongation and area from filename
						// Example: "tray1_crop1_mask_(1.23, 456.7).bmp"
						size_t open_paren = filename.find('(');
						size_t comma = filename.find(',', open_paren);
						size_t close_paren = filename.find(')', comma);

						if (open_paren != string::npos && comma != string::npos && close_paren != string::npos) {
							try {
								string elong_str = filename.substr(open_paren + 1, comma - open_paren - 1);
								string area_str = filename.substr(comma + 1, close_paren - comma - 1);
								info.elongation = stof(elong_str);
								info.area = stof(area_str);
							}
							catch (...) {
								// If parsing fails, set to NaN
								info.elongation = numeric_limits<float>::quiet_NaN();
								info.area = numeric_limits<float>::quiet_NaN();
							}
						}
						else {
							info.elongation = numeric_limits<float>::quiet_NaN();
							info.area = numeric_limits<float>::quiet_NaN();
						}

						mask_info_nomatch[crop_name] = info;
						found = true;
						break;
					}
				}
			}

			if (!found) {
				cout << "Warning: mask file not found for crop: " << crop_name << " in " << cropped_masks_path << endl;
			}
		}

		if (mask_info_nomatch.empty()) {
			cout << "Warning: failed to load masks for No Match crops from " << cropped_masks_path << ". Skip filter." << endl;
			return;
		}

		cout << "Loaded " << mask_info_nomatch.size() << " mask files from " << cropped_masks_path << endl;

		set<string> removed;

		for (const auto& crop_name : crops) {
			auto miIt = mask_info_nomatch.find(crop_name);
			if (miIt == mask_info_nomatch.end()) continue;

			cv::Mat crop_img = cv::imread((fs::path(cropped_dir) / crop_name).string());
			cv::Mat mask_u8 = cv::imread(miIt->second.mask_path, cv::IMREAD_GRAYSCALE);
			if (crop_img.empty() || mask_u8.empty()) continue;

			mask_u8 = (mask_u8 > 127);
			mask_u8.convertTo(mask_u8, CV_8U);

			// (1) components filter
			cv::Mat labels, stats, centroids;
			int num_labels = cv::connectedComponentsWithStats(mask_u8, labels, stats, centroids, 8);
			const int components = num_labels - 1;
			if (components > max_components) {
				removed.insert(crop_name);
				continue;
			}

			// (2) background color similarity filter
			const int h = crop_img.rows;
			const int w = crop_img.cols;
			const int border_size = std::min(8, std::min(h, w) / 10);
			if (border_size <= 0) continue;

			cv::Mat border_mask = cv::Mat::ones(h, w, CV_8U);
			cv::Rect inner(border_size, border_size, std::max(0, w - 2 * border_size), std::max(0, h - 2 * border_size));
			if (inner.width > 0 && inner.height > 0) {
				border_mask(inner) = 0;
			}

			cv::Mat bg_mask = ((border_mask > 0) & (mask_u8 == 0));
			cv::Mat fg_mask = (mask_u8 > 0);
			if (cv::countNonZero(bg_mask) > 50 && cv::countNonZero(fg_mask) > 50) {
				cv::Mat bg_mask255, fg_mask255;
				bg_mask.convertTo(bg_mask255, CV_8U, 255.0);
				fg_mask.convertTo(fg_mask255, CV_8U, 255.0);

				cv::Vec3f bg_lab = MaskedLABMedian(crop_img, bg_mask255, 1.0f);
				cv::Vec3f fg_lab = MaskedLABMedian(crop_img, fg_mask255, 1.0f);
				// If median failed, it returns (0,0,0) in our impl
				if (!(bg_lab[0] == 0 && bg_lab[1] == 0 && bg_lab[2] == 0) &&
					!(fg_lab[0] == 0 && fg_lab[1] == 0 && fg_lab[2] == 0)) {
					float de = DeltaE2000(bg_lab, fg_lab);
					if (isfinite(de) && de < bg_color_similarity_threshold) {
						removed.insert(crop_name);
						continue;
					}
				}
			}
		}

		if (removed.empty()) {
			cout << "No crops removed by filter." << endl;
			return;
		}

		// Apply removal
		for (auto& [tray_idx, items] : unmatched_by_tray) {
			vector<CropMatch> kept;
			kept.reserve(items.size());
			for (const auto& it : items) {
				if (removed.find(it.crop_name) == removed.end()) kept.push_back(it);
			}
			items.swap(kept);
		}

		cout << "Filtered out " << removed.size() << " No Match crops." << endl;
	}

	int SaveTraysWithNoMatchAnnotations(
		const map<int, vector<CropMatch>>& unmatched_by_tray,
		const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		const vector<string>& subfolder_names,
		const vector<int>& numbers,
		const vector<int>& tray_indices,
		const string& tray_dir,
		const string& result_dir,
		int padding = 5,
		bool draw_overmatch_boxes = true) {

		cout << "\n============================================================" << endl;
		cout << "Save trays with No Match / overmatch annotations (Python save_trays_with_no_match_annotations) ..." << endl;
		cout << "============================================================" << endl;

		const fs::path nomatch_dir = fs::path(result_dir) / "NoMatch";
		fs::create_directories(nomatch_dir);

		// tray_total_counts: aligned with final_result.txt (required = all presc, actual = matched + no_match)
		map<int, pair<int, int>> tray_total_counts; // tray -> (total_required, total_actual)
		map<int, map<string, pair<int, int>>> tray_presc_matches; // tray -> presc -> (req, act)

		for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
			int required_count = 1;
			auto it = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
			if (it != subfolder_names.end()) {
				int presc_j = (int)distance(subfolder_names.begin(), it);
				required_count = (presc_j >= 0 && presc_j < (int)numbers.size()) ? numbers[presc_j] : 1;
			}

			for (const auto& [tray_idx, matched_items] : tray_dict) {
				int actual_count = (int)matched_items.size();
				auto& totals = tray_total_counts[tray_idx];
				totals.first += required_count;
				totals.second += actual_count;
				tray_presc_matches[tray_idx][presc_name] = { required_count, actual_count };
			}
		}

		set<int> all_tray_indices;
		for (int tray_idx : tray_indices) {
			all_tray_indices.insert(tray_idx);
		}

		const int total_required_count = (int)accumulate(numbers.begin(), numbers.end(), 0);
		for (int tray_idx : all_tray_indices) {
			int total_actual_count = 0;
			auto itPM = tray_presc_matches.find(tray_idx);
			if (itPM != tray_presc_matches.end()) {
				for (const auto& [_, rcac] : itPM->second) {
					total_actual_count += rcac.second;
				}
			}
			auto itU = unmatched_by_tray.find(tray_idx);
			if (itU != unmatched_by_tray.end()) {
				total_actual_count += (int)itU->second.size();
			}
			tray_total_counts[tray_idx] = { total_required_count, total_actual_count };
		}

		set<int> trays_to_process;
		for (int tray_idx : all_tray_indices) {
			const int total_required = tray_total_counts.count(tray_idx) ? tray_total_counts[tray_idx].first : 0;
			const int total_actual = tray_total_counts.count(tray_idx) ? tray_total_counts[tray_idx].second : 0;
			const bool has_no_match = unmatched_by_tray.count(tray_idx) && !unmatched_by_tray.at(tray_idx).empty();

			const bool total_match = (total_actual == total_required);
			bool all_presc_match = true;
			auto itPM = tray_presc_matches.find(tray_idx);
			if (itPM != tray_presc_matches.end()) {
				for (const auto& [pn, rcac] : itPM->second) {
					if (rcac.first != rcac.second) { all_presc_match = false; break; }
				}
			}
			const bool no_match_empty = !has_no_match;

			if (!total_match || !(all_presc_match && no_match_empty)) {
				trays_to_process.insert(tray_idx);
			}
		}

		if (trays_to_process.empty()) {
			cout << "No trays to process." << endl;
			return 0;
		}

		vector<pair<int, cv::Mat>> tray_images;

		for (int tray_idx : trays_to_process) {
			// locate tray image
			fs::path tray_path = fs::path(tray_dir) / ("tray" + to_string(tray_idx) + ".bmp");
			if (!fs::exists(tray_path)) {
				vector<string> exts = { ".BMP", ".png", ".PNG", ".jpg", ".JPG" };
				bool found = false;
				for (const auto& ext : exts) {
					fs::path alt = fs::path(tray_dir) / ("tray" + to_string(tray_idx) + ext);
					if (fs::exists(alt)) { tray_path = alt; found = true; break; }
				}
				if (!found) continue;
			}

			cv::Mat tray_img = cv::imread(tray_path.string());
			if (tray_img.empty()) continue;

			const int h_tray = tray_img.rows;
			const int w_tray = tray_img.cols;
			cv::Mat annotated = tray_img.clone();

			// (1) red boxes for No Match
			int no_match_count = 0;
			auto itU = unmatched_by_tray.find(tray_idx);
			if (itU != unmatched_by_tray.end()) {
				for (const auto& item : itU->second) {
					BBox bbox = ParseBBoxFromFilename(item.crop_name);
					if (bbox.x1 < 0) continue;
					int x1 = max(0, bbox.x1 - padding);
					int y1 = max(0, bbox.y1 - padding);
					int x2 = min(w_tray, bbox.x2 + padding);
					int y2 = min(h_tray, bbox.y2 + padding);
					cv::rectangle(annotated, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(0, 0, 255), 3);
					no_match_count++;
				}
			}

			// (2) overmatch boxes: per presc, draw only lowest-prob crop
			vector<cv::Scalar> colors = {
				cv::Scalar(0,255,0), cv::Scalar(255,0,0), cv::Scalar(0,255,255),
				cv::Scalar(255,0,255), cv::Scalar(255,128,0), cv::Scalar(128,0,255)
			};
			int color_idx = 0;

			if (draw_overmatch_boxes) {
				for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
					auto itT = tray_dict.find(tray_idx);
					if (itT == tray_dict.end()) continue;

					int required_count = 1;
					auto it = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
					if (it != subfolder_names.end()) {
						int presc_j = (int)distance(subfolder_names.begin(), it);
						required_count = (presc_j >= 0 && presc_j < (int)numbers.size()) ? numbers[presc_j] : 1;
					}

					const auto& matched_items = itT->second;
					const int actual_count = (int)matched_items.size();
					if (actual_count <= required_count) continue;

					// find lowest prob crop
					auto lowest = *min_element(matched_items.begin(), matched_items.end(),
						[](const auto& a, const auto& b) { return a.second < b.second; });

					const string& crop_name = lowest.first;
					BBox bbox = ParseBBoxFromFilename(crop_name);
					if (bbox.x1 < 0) continue;
					cv::Scalar color = colors[color_idx % (int)colors.size()];
					color_idx++;

					int x1 = max(0, bbox.x1 - padding);
					int y1 = max(0, bbox.y1 - padding);
					int x2 = min(w_tray, bbox.x2 + padding);
					int y2 = min(h_tray, bbox.y2 + padding);
					cv::rectangle(annotated, cv::Point(x1, y1), cv::Point(x2, y2), color, 3);
				}
			}

			const int total_required = tray_total_counts.count(tray_idx) ? tray_total_counts[tray_idx].first : 0;
			const int total_actual = tray_total_counts.count(tray_idx) ? tray_total_counts[tray_idx].second : 0;
			const int total_diff = total_actual - total_required;
			string tray_label = "Tray" + to_string(tray_idx) + " [" + (total_diff >= 0 ? "+" : "") + to_string(total_diff) + "]";
			cv::putText(annotated, tray_label, cv::Point(10, 50), cv::FONT_HERSHEY_SIMPLEX, 2.0, cv::Scalar(0, 255, 0), 3);

			tray_images.push_back({ tray_idx, annotated });
		}

		if (tray_images.empty()) {
			cout << "No annotated trays produced." << endl;
			return 0;
		}

		const int num_trays = (int)tray_images.size();
		const int cols = (int)ceil(sqrt((double)num_trays));
		const int rows = (int)ceil((double)num_trays / cols);
		const int h_img = tray_images[0].second.rows;
		const int w_img = tray_images[0].second.cols;
		const int spacing = 10;
		const int total_h = rows * h_img + (rows - 1) * spacing;
		const int total_w = cols * w_img + (cols - 1) * spacing;

		cv::Mat combined = cv::Mat::ones(total_h, total_w, CV_8UC3) * 255;
		for (int idx = 0; idx < num_trays; ++idx) {
			int r = idx / cols;
			int c = idx % cols;
			int y = r * (h_img + spacing);
			int x = c * (w_img + spacing);
			tray_images[idx].second.copyTo(combined(cv::Rect(x, y, w_img, h_img)));
		}

		const fs::path out_path = nomatch_dir / "no_match_trays.bmp";
		cv::imwrite(out_path.string(), combined);
		cout << "Saved: " << out_path.string() << " (trays=" << num_trays << ")" << endl;
		return num_trays;
	}

public:
	PredictionProcessing(const string& cropped_dir,
		const string& prescription_dir,
		const string& result_dir,
		const string& siamese_model_path,
		const string& yolo_seg_model_path,
		float siamese_threshold = 0.5f)
		: cropped_dir(cropped_dir),
		prescription_dir(prescription_dir),
		result_dir(result_dir),
		siamese_model_path(siamese_model_path),
		yolo_seg_model_path(yolo_seg_model_path),
		siamese_threshold(siamese_threshold) {

		fs::create_directories(result_dir);
	}

	bool Initialize() {
		cout << "Loading Siamese model..." << endl;
		if (!siamese_model.LoadModel(siamese_model_path)) {
			cerr << "Siamese model loading failed" << endl;
			return false;
		}
		cout << "Siamese model loaded successfully" << endl;

		cout << "Loading YOLO Segmentation model..." << endl;
		if (!yolo_seg_model.LoadModel(yolo_seg_model_path)) {
			cerr << "YOLO Segmentation model loading failed" << endl;
			return false;
		}
		cout << "YOLO Segmentation model loaded successfully" << endl;

		// Verify GPU usage
		cout << "\n=== GPU Configuration ===" << endl;
#if AMD == 1 && defined(_WIN32)
		cout << "DirectML provider configured (AMD 780M GPU)" << endl;
		cout << "Note: If you see 'DirectML provider enabled' above, GPU is active." << endl;
		cout << "      If you see 'Warning: DirectML...', GPU may not be available." << endl;
#elif AMD == 0
		cout << "CUDA provider configured (MX150 GPU)" << endl;
		cout << "Note: If you see 'CUDA provider enabled' above, GPU is active." << endl;
		cout << "      If you see 'Warning: Failed to enable CUDA...', GPU may not be available." << endl;
#else
		cout << "CPU mode (no GPU acceleration)" << endl;
#endif
		cout << "==========================\n" << endl;

		return true;
	}

	// (1) Siamese Net Inference
	void ComputeSiameseInference(
		const vector<string>& presc_paths,
		const vector<string>& cropped_paths,
		vector<vector<float>>& probs_mxn,
		vector<vector<uint8_t>>& preds_mxn) {

		// Start timing for Siamese inference
		auto siamese_start = chrono::high_resolution_clock::now();

		cout << "\n=== Siamese Net Inference Start ===" << endl;

		size_t m = cropped_paths.size();
		size_t n = presc_paths.size();

		probs_mxn.resize(m, vector<float>(n, 0.0f));
		preds_mxn.resize(m, vector<uint8_t>(n, 0));

		// Load prescription images once (cache them to avoid repeated I/O)
		vector<cv::Mat> presc_images;
		presc_images.reserve(presc_paths.size());
		vector<string> presc_full_paths;
		presc_full_paths.reserve(presc_paths.size());
		for (const auto& path : presc_paths) {
			fs::path full_path = fs::path(prescription_dir) / path;
			string full_path_str = full_path.string();
			presc_full_paths.push_back(full_path_str);
			cv::Mat img = cv::imread(full_path_str, cv::IMREAD_COLOR);
			if (img.empty()) {
				cerr << "Error: Failed to load image: " << full_path_str << endl;
				presc_images.push_back(cv::Mat());
			}
			else {
				presc_images.push_back(img);
			}
		}

		// Pre-load all crop images once to avoid repeated I/O (major performance improvement)
		vector<cv::Mat> all_crop_images;
		all_crop_images.reserve(m);
		vector<string> crop_full_paths;
		crop_full_paths.reserve(m);
		for (const auto& crop_name : cropped_paths) {
			fs::path crop_path = fs::path(cropped_dir) / crop_name;
			string crop_path_str = crop_path.string();
			crop_full_paths.push_back(crop_path_str);
			cv::Mat img = cv::imread(crop_path_str, cv::IMREAD_COLOR);
			if (img.empty()) {
				cerr << "Error: Failed to load image: " << crop_path_str << endl;
				all_crop_images.push_back(cv::Mat());
			}
			else {
				all_crop_images.push_back(img);
			}
		}

		// Batch processing with configured batch size
		const size_t batch_size = static_cast<size_t>(BATCH_SIZE);
		// Reduce console output frequency for better performance (print every 5 batches or at start/end)
		const size_t print_interval = (m / batch_size > 0) ? max(static_cast<size_t>(1), (m / batch_size) / 5) : 1;  // Print ~5 times total
		for (size_t start = 0; start < m; start += batch_size) {
			size_t end = min(start + batch_size, m);
			size_t batch_num = start / batch_size;
			if (batch_num == 0 || batch_num % print_interval == 0 || end >= m) {
				cout << "Processing batch: " << (start + 1) << "~" << end << "/" << m << endl;
			}

			// Use pre-loaded crop images (no I/O here)
			vector<cv::Mat> crop_batch;
			crop_batch.reserve(end - start);
			for (size_t i = start; i < end; i++) {
				crop_batch.push_back(all_crop_images[i]);
			}

			// Compare with all prescriptions using batch processing (much faster!)
			for (size_t j = 0; j < n; j++) {
				if (presc_images[j].empty()) continue;

				// Try batch processing first, fallback to individual if it fails
				bool batch_success = false;
				vector<float> batch_probs;

				try {
					batch_probs = siamese_model.ComputeSimilarityBatch(crop_batch, presc_images[j]);
					batch_success = true;
				}
				catch (const exception& e) {
					cerr << "Batch inference failed, falling back to individual processing: " << e.what() << endl;
					batch_success = false;
				}

				if (batch_success && batch_probs.size() == crop_batch.size()) {
					// Use batch results
					for (size_t i = 0; i < crop_batch.size(); i++) {
						if (crop_batch[i].empty()) continue;

						size_t idx = start + i;
						float prob = batch_probs[i];
						probs_mxn[idx][j] = prob;
						preds_mxn[idx][j] = (prob > siamese_threshold) ? 1 : 0;
					}
				}
				else {
					// Fallback to individual processing
					for (size_t i = 0; i < crop_batch.size(); i++) {
						if (crop_batch[i].empty()) continue;

						size_t idx = start + i;
						float prob = siamese_model.ComputeSimilarity(crop_batch[i], presc_images[j]);
						probs_mxn[idx][j] = prob;
						preds_mxn[idx][j] = (prob > siamese_threshold) ? 1 : 0;
					}
				}
			}
		}

		// Calculate and display Siamese inference time
		auto siamese_end = chrono::high_resolution_clock::now();
		auto siamese_duration = chrono::duration_cast<chrono::milliseconds>(siamese_end - siamese_start);
		auto siamese_duration_seconds = chrono::duration_cast<chrono::seconds>(siamese_end - siamese_start);

		cout << "Siamese Net inference completed" << endl;
		if (siamese_duration_seconds.count() > 0) {
			auto remaining_ms = siamese_duration.count() % 1000;
			cout << "Siamese inference time: " << siamese_duration_seconds.count() << "."
				<< setfill('0') << setw(3) << remaining_ms << " seconds (" << siamese_duration.count() << " ms)" << endl;
		}
		else {
			cout << "Siamese inference time: " << siamese_duration.count() << " ms" << endl;
		}
	}

	// (2) YOLO Segmentation
	void GenerateMasksWithYOLO(
		const vector<string>& cropped_paths,
		const string& predict_dir,
		map<string, MaskInfo>& mask_info) {

		cout << "\n=== YOLO Segmentation Start ===" << endl;

		if (cropped_paths.empty()) {
			cout << "Warning: cropped_paths is empty. No masks will be generated." << endl;
			return;
		}

		// Note: predict_dir should already be created by the caller (e.g., at line 1738)

		// Pre-compute full paths once to avoid repeated string operations
		vector<string> crop_full_paths;
		crop_full_paths.reserve(cropped_paths.size());
		for (const auto& crop_name : cropped_paths) {
			fs::path crop_path = fs::path(cropped_dir) / crop_name;
			crop_full_paths.push_back(crop_path.string());
		}

		for (size_t idx = 0; idx < cropped_paths.size(); ++idx) {
			const string& crop_name = cropped_paths[idx];
			const string& crop_path_str = crop_full_paths[idx];

			cv::Mat crop_img = cv::imread(crop_path_str, cv::IMREAD_COLOR);

			if (crop_img.empty()) {
				cerr << "Error: Failed to load image: " << crop_path_str << endl;
				continue;
			}

			// Generate mask using YOLO
			auto result = yolo_seg_model.GenerateMask(crop_img);

			if (!result.valid) {
				cerr << "Error: Failed to generate mask: " << crop_name << endl;
				continue;
			}

			// Save mask
			// File name format: {crop_stem}_mask_({elongation:.2f}, {area:.1f}).bmp
			// Match Python: mask_filename = f"{crop_stem}_mask_({elong:.2f}, {area:.1f}).bmp"
			// Pre-compute crop_stem once (avoid repeated string operations)
			string crop_stem;
			size_t dot_pos = crop_name.find_last_of('.');
			if (dot_pos != string::npos) {
				crop_stem = crop_name.substr(0, dot_pos);
			}
			else {
				crop_stem = crop_name;
			}

			string mask_filename;
			if (isfinite(result.elongation) && isfinite(result.area)) {
				char buffer[256];
				snprintf(buffer, sizeof(buffer), "%s_mask_(%.2f, %.1f).bmp",
					crop_stem.c_str(), result.elongation, result.area);
				mask_filename = buffer;
			}
			else {
				mask_filename = crop_stem + "_mask_(inf, inf).bmp";
			}

			// Pre-compute mask path string once
			string mask_path_str = (fs::path(predict_dir) / mask_filename).string();
			bool saved = cv::imwrite(mask_path_str, result.mask);

			if (!saved) {
				cerr << "Error: Failed to save mask: " << mask_path_str << endl;
				continue;
			}

			MaskInfo info;
			info.mask_path = mask_path_str;
			info.elongation = result.elongation;
			info.area = result.area;
			mask_info[crop_name] = info;
		}

		cout << "YOLO Segmentation completed" << endl;
	}

	// (3) Elongation, Area, Colors Comparison
	void CompareFeatures(
		const map<string, MaskInfo>& mask_info,
		const map<int, PrescMaskInfo>& presc_mask_info,
		const vector<string>& cropped_paths,
		const vector<string>& presc_paths,
		const vector<string>& subfolder_names,
		map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		map<int, vector<CropMatch>>& unmatched_by_tray,
		float elong_tolerance = 0.1f,
		float area_tolerance = 0.2f,
		float color_threshold = 2.0f) {

		cout << "\n=== Elongation, Area, Colors Comparison Start ===" << endl;

		// Cache images/masks to avoid repeated disk I/O in nested loops.
		unordered_map<string, cv::Mat> color_cache;
		unordered_map<string, cv::Mat> mask_cache;
		auto get_color = [&](const fs::path& p) -> cv::Mat& {
			const string key = p.string();
			auto it = color_cache.find(key);
			if (it != color_cache.end()) return it->second;
			cv::Mat img = cv::imread(key, cv::IMREAD_COLOR);
			auto [inserted, _] = color_cache.emplace(key, img);
			return inserted->second;
			};
		auto get_mask = [&](const fs::path& p) -> cv::Mat& {
			const string key = p.string();
			auto it = mask_cache.find(key);
			if (it != mask_cache.end()) return it->second;
			cv::Mat img = cv::imread(key, cv::IMREAD_GRAYSCALE);
			auto [inserted, _] = mask_cache.emplace(key, img);
			return inserted->second;
			};

		// Process all tray matches (safe: do not erase while iterating)
		for (auto& [presc_name, tray_dict] : presc_tray_matches) {
			auto presc_name_it = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
			if (presc_name_it == subfolder_names.end()) continue;
			const int presc_j = distance(subfolder_names.begin(), presc_name_it);
			if (presc_j < 0 || presc_j >= (int)presc_paths.size()) {
				continue;
			}

			auto presc_mask_it = presc_mask_info.find(presc_j);
			if (presc_mask_it == presc_mask_info.end()) continue;

			const PrescMaskInfo& presc_masks = presc_mask_it->second;
			const fs::path presc_path = fs::path(prescription_dir) / presc_paths[presc_j];

			for (auto& [tray_idx, matches] : tray_dict) {
				vector<pair<string, float>> kept;
				kept.reserve(matches.size());
				for (const auto& [crop_name, prob] : matches) {
					// Get crop mask info
					auto crop_mask_it = mask_info.find(crop_name);
					if (crop_mask_it == mask_info.end()) { kept.push_back({ crop_name, prob }); continue; }

					const MaskInfo& crop_mask_info = crop_mask_it->second;
					float crop_elong = crop_mask_info.elongation;
					float crop_area = crop_mask_info.area;

					const fs::path crop_path = fs::path(cropped_dir) / crop_name;
					const fs::path crop_mask_path = fs::path(crop_mask_info.mask_path);

					// Find best matching prescription mask
					int best_score = 0;
					for (const auto& presc_mask : presc_masks.masks) {
						int score = 0;

						// (1) Elongation comparison
						if (isfinite(crop_elong) && isfinite(presc_mask.elongation)) {
							float elong_diff = abs(crop_elong - presc_mask.elongation);
							if (elong_diff < elong_tolerance) {
								score += 1;
							}
						}

						// (2) Area comparison
						if (isfinite(crop_area) && isfinite(presc_mask.area)) {
							float area_ratio = max(crop_area, presc_mask.area) /
								(min(crop_area, presc_mask.area) + 1e-6f);
							if (area_ratio < 1.0f + area_tolerance) {
								score += 1;
							}
						}

						// (3) Color comparison
						fs::path presc_mask_path = fs::path(presc_mask.mask_path);

						cv::Mat& crop_img = get_color(crop_path);
						cv::Mat& presc_img = get_color(presc_path);
						cv::Mat& crop_mask = get_mask(crop_mask_path);
						cv::Mat& presc_mask = get_mask(presc_mask_path);

						if (!crop_img.empty() && !presc_img.empty() &&
							!crop_mask.empty() && !presc_mask.empty()) {

							float color_score = ComparePillColor(crop_img, crop_mask, presc_img, presc_mask);
							if (isfinite(color_score) && color_score < color_threshold) {
								score += 1;
							}
						}

						if (score > best_score) {
							best_score = score;
						}
					}

					// If score is too low, move to unmatched
					if (best_score < 2) {  // Need at least 2 matches
						// Add to unmatched_by_tray
						CropMatch match;
						match.crop_name = crop_name;
						match.prob = prob;
						match.best_name = presc_name;
						unmatched_by_tray[tray_idx].push_back(match);
					}
					else {
						kept.push_back({ crop_name, prob });
					}
				}
				matches.swap(kept);
			}
		}

		cout << "Elongation, Area, Colors comparison completed" << endl;
	}

	// (4) Save results directly to files (CSV and BMP)
	void SaveResultsToFiles(
		const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		const map<int, vector<CropMatch>>& unmatched_by_tray,
		const vector<string>& subfolder_names,
		const vector<int>& numbers,
		const vector<int>& tray_indices,
		const string& result_dir) {

		cout << "\n=== Save Results to Files ===" << endl;

		// Save CSV file (Excel can open CSV files)
		string csv_path = (fs::path(result_dir) / "final_result.csv").string();
		SaveResultsToCSV(presc_tray_matches, unmatched_by_tray, subfolder_names, numbers, tray_indices, csv_path);

		// Save tray annotations (Python save_trays_with_no_match_annotations equivalent)
		string tray_dir = (fs::path(result_dir).parent_path() / "trays").string();
		SaveTraysWithNoMatchAnnotations(unmatched_by_tray, presc_tray_matches, subfolder_names, numbers,
			tray_indices, tray_dir, result_dir, 5, true);

		cout << "Results saved successfully!" << endl;
		cout << "- CSV file: " << csv_path << endl;
		cout << "- Tray annotation BMP: " << (fs::path(result_dir) / "NoMatch" / "no_match_trays.bmp").string() << endl;
	}

	// (5) Print final result statistics (Python print_final_result_statistics equivalent)
	void PrintFinalResultStatistics(
		const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		const map<int, vector<CropMatch>>& unmatched_by_tray,
		const vector<string>& subfolder_names,
		const vector<int>& numbers,
		const vector<int>& tray_indices,
		const string& result_dir) {

		vector<string> output_lines;

		auto add_line = [&](const string& line) {
			output_lines.push_back(line);
			cout << line << endl;
			};

		add_line("\n" + string(80, '#'));
		add_line("Final Result 통계");
		add_line(string(80, '#'));

		// Display prescription name helper
		auto display_presc_name = [](const string& presc_name, int required_count) -> string {
			if (required_count >= 2) {
				return presc_name + "(" + to_string(required_count) + ")";
			}
			return presc_name;
			};

		// Extract crop prefix from filename (remove extension, remove trailing part after '(')
		auto extract_crop_prefix = [](const string& crop_name) -> string {
			string name_wo_ext = fs::path(crop_name).stem().string();
			size_t pos = name_wo_ext.find('(');
			string prefix = (pos != string::npos) ? name_wo_ext.substr(0, pos) : name_wo_ext;
			// rstrip '_' and spaces
			while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == ' ' || prefix.back() == '\t')) {
				prefix.pop_back();
			}
			return prefix;
			};

		// Process each tray
		vector<int> sorted_trays = tray_indices;
		sort(sorted_trays.begin(), sorted_trays.end());

		for (int tray_idx : sorted_trays) {
			add_line("\n" + string(60, '='));
			add_line("Tray " + to_string(tray_idx));
			add_line(string(60, '='));

			// (1) Tray overall statistics
			int total_required_count = 0;
			int total_crop_count = 0;

			// Calculate total required count (all prescriptions, matched or not)
			for (size_t presc_j = 0; presc_j < subfolder_names.size(); ++presc_j) {
				const string& presc_name = subfolder_names[presc_j];
				int required_count = (presc_j < numbers.size()) ? numbers[presc_j] : 1;

				// Check if this prescription is matched in current tray
				auto itP = presc_tray_matches.find(presc_name);
				if (itP != presc_tray_matches.end()) {
					auto itT = itP->second.find(tray_idx);
					if (itT != itP->second.end()) {
						int matched_crop_count = static_cast<int>(itT->second.size());
						total_crop_count += matched_crop_count;
					}
				}
				// Always include required count (matched or not)
				total_required_count += required_count;
			}

			// Add No Match crop count
			int no_match_count = 0;
			auto itU = unmatched_by_tray.find(tray_idx);
			if (itU != unmatched_by_tray.end()) {
				no_match_count = static_cast<int>(itU->second.size());
			}
			total_crop_count += no_match_count;

			// Calculate difference
			int diff = total_crop_count - total_required_count;

			add_line("\n[전체 통계]");
			add_line("  조제 개수: " + to_string(total_required_count) + "개");
			add_line("  크롭 개수: " + to_string(total_crop_count) + "개 (매칭: " + to_string(total_crop_count - no_match_count) + "개, No Match: " + to_string(no_match_count) + "개)");

			if (diff > 0) {
				add_line("  => " + to_string(diff) + u8"개가 넘칩니다 ⚠️");
			}
			else if (diff < 0) {
				add_line("  => " + to_string(abs(diff)) + u8"개가 모자랍니다 ⚠️");
			}
			else {
				add_line("  => 개수가 일치합니다 ✓");
			}

			// (2) Detailed information for each prescription
			add_line("\n[조제 알약별 상세]");

			// Iterate through all prescriptions (including unmatched ones)
			for (size_t presc_j = 0; presc_j < subfolder_names.size(); ++presc_j) {
				const string& presc_name = subfolder_names[presc_j];
				int required_count = (presc_j < numbers.size()) ? numbers[presc_j] : 1;

				// Display prescription name (add (2) if count >= 2)
				string display_name = display_presc_name(presc_name, required_count);

				// Check matched crops for this prescription in current tray
				vector<pair<string, float>> matched_crops;
				auto itP = presc_tray_matches.find(presc_name);
				if (itP != presc_tray_matches.end()) {
					auto itT = itP->second.find(tray_idx);
					if (itT != itP->second.end()) {
						matched_crops = itT->second;
					}
				}

				int matched_crop_count = static_cast<int>(matched_crops.size());

				// Create crop info list
				vector<string> crop_info_list;
				for (const auto& [crop_name, prob] : matched_crops) {
					string crop_prefix = extract_crop_prefix(crop_name);
					ostringstream oss;
					oss << fixed << setprecision(2) << prob;
					crop_info_list.push_back(crop_prefix + " (" + oss.str() + ")");
				}

				// If no matched crops, warning message
				string crop_info_str;
				if (matched_crop_count == 0) {
					crop_info_str = u8"(매칭되는 크롭 알약 없음) ⚠️";
				}
				else {
					stringstream ss;
					for (size_t i = 0; i < crop_info_list.size(); ++i) {
						if (i > 0) ss << ", ";
						ss << crop_info_list[i];
					}
					crop_info_str = ss.str();
				}

				// Warning symbol
				string warning_symbol = "";
				if (matched_crop_count > required_count) {
					warning_symbol = u8" ⚠️ (과다)";
				}
				else if (matched_crop_count < required_count && matched_crop_count > 0) {
					warning_symbol = u8" ⚠️ (부족)";
				}

				add_line("  " + display_name + ": " + crop_info_str + warning_symbol);
			}

			// No Match crops
			if (itU != unmatched_by_tray.end() && !itU->second.empty()) {
				add_line("\n[No Match]");
				vector<string> no_match_list;
				for (const auto& match : itU->second) {
					string crop_prefix = extract_crop_prefix(match.crop_name);
					ostringstream oss;
					oss << fixed << setprecision(2) << match.prob;

					if (!match.best_name.empty() && match.prob > 0.0f) {
						no_match_list.push_back(crop_prefix + " (" + oss.str() + ", " + match.best_name + ")");
					}
					else {
						no_match_list.push_back(crop_prefix + " (" + oss.str() + ", Unknown)");
					}
				}

				stringstream ss;
				for (size_t i = 0; i < no_match_list.size(); ++i) {
					if (i > 0) ss << ", ";
					ss << no_match_list[i];
				}
				add_line("  " + ss.str());
			}
		}

		add_line("\n" + string(80, '#'));
		add_line("통계 출력 완료");
		add_line(string(80, '#') + "\n");

		// Save to file (UTF-8 with BOM for proper Korean character display)
		if (!result_dir.empty()) {
			fs::path txt_file_path = fs::path(result_dir) / "final_result.txt";
			try {
				// Open file in binary mode to write UTF-8 BOM
				ofstream f(txt_file_path.string(), ios::binary);
				if (f.is_open()) {
					// Write UTF-8 BOM (0xEF 0xBB 0xBF) for Windows compatibility
					unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
					f.write(reinterpret_cast<const char*>(bom), 3);

					// Write content as UTF-8
					for (const string& line : output_lines) {
						f << line << "\n";
					}
					f.close();
					cout << "\n통계 파일 저장 완료 (UTF-8): " << txt_file_path.string() << endl;
				}
			}
			catch (const exception& e) {
				cout << "\nWarning: Failed to save statistics file - " << e.what() << endl;
			}

			// Generate summary.txt
			vector<string> summary_lines;
			for (int tray_idx : sorted_trays) {
				bool has_output = false;

				// (1) No Match crops exist
				auto itU = unmatched_by_tray.find(tray_idx);
				if (itU != unmatched_by_tray.end() && !itU->second.empty()) {
					vector<string> valid_best_names;
					bool has_invalid = false;

					for (const auto& match : itU->second) {
						if (!match.best_name.empty() && match.prob > 0.0f) {
							// Trim whitespace
							string trimmed = match.best_name;
							trimmed.erase(0, trimmed.find_first_not_of(" \t"));
							trimmed.erase(trimmed.find_last_not_of(" \t") + 1);
							if (!trimmed.empty()) {
								valid_best_names.push_back(trimmed);
							}
						}
						else {
							has_invalid = true;
						}
					}

					if (!valid_best_names.empty()) {
						// Remove duplicates
						set<string> unique_set(valid_best_names.begin(), valid_best_names.end());
						vector<string> unique_best_names(unique_set.begin(), unique_set.end());

						if (unique_best_names.size() == 1) {
							summary_lines.push_back("tray" + to_string(tray_idx) +
								": No Match 에 속하는 알약은 조제 알약 " + unique_best_names[0] + "인지 확인하세요.");
						}
						else {
							stringstream ss;
							for (size_t i = 0; i < unique_best_names.size(); ++i) {
								if (i > 0) ss << ", ";
								ss << unique_best_names[i];
							}
							summary_lines.push_back("tray" + to_string(tray_idx) +
								": No Match 에 속하는 알약은 조제 알약은 각각 " + ss.str() + " 인지 확인하세요.");
						}
						has_output = true;
					}
					else if (has_invalid) {
						summary_lines.push_back("tray" + to_string(tray_idx) +
							": No Match 에 속하는 알약은 조제 알약인지 이물질인지 확인하세요.");
						has_output = true;
					}
				}

				// (2) Total required count > total crop count (only when required > crop)
				int total_required_count = 0;
				int total_crop_count = 0;

				// Calculate required count
				for (size_t presc_j = 0; presc_j < subfolder_names.size(); ++presc_j) {
					int required_count = (presc_j < numbers.size()) ? numbers[presc_j] : 1;
					total_required_count += required_count;
				}

				// Calculate crop count
				for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
					auto itT = tray_dict.find(tray_idx);
					if (itT != tray_dict.end()) {
						total_crop_count += static_cast<int>(itT->second.size());
					}
				}

				// Add No Match count
				if (itU != unmatched_by_tray.end()) {
					total_crop_count += static_cast<int>(itU->second.size());
				}

				// Only when required > crop
				if (total_required_count > total_crop_count) {
					// Check each prescription
					for (size_t presc_j = 0; presc_j < subfolder_names.size(); ++presc_j) {
						const string& presc_name = subfolder_names[presc_j];
						int required_count = (presc_j < numbers.size()) ? numbers[presc_j] : 1;

						int matched_crop_count = 0;
						auto itP = presc_tray_matches.find(presc_name);
						if (itP != presc_tray_matches.end()) {
							auto itT = itP->second.find(tray_idx);
							if (itT != itP->second.end()) {
								matched_crop_count = static_cast<int>(itT->second.size());
							}
						}

						// Prescription needed but matched crops are insufficient
						if (required_count > 0 && matched_crop_count < required_count) {
							string trimmed_name = presc_name;
							trimmed_name.erase(0, trimmed_name.find_first_not_of(" \t"));
							trimmed_name.erase(trimmed_name.find_last_not_of(" \t") + 1);
							summary_lines.push_back("tray" + to_string(tray_idx) +
								": 조제 알약 " + trimmed_name + " 이 없는 것 같습니다.");
							has_output = true;
						}
					}
				}
			}

			// Save summary.txt (UTF-8 with BOM for proper Korean character display)
			if (!summary_lines.empty()) {
				fs::path summary_file_path = fs::path(result_dir) / "summary.txt";
				try {
					// Open file in binary mode to write UTF-8 BOM
					ofstream f(summary_file_path.string(), ios::binary);
					if (f.is_open()) {
						// Write UTF-8 BOM (0xEF 0xBB 0xBF) for Windows compatibility
						unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
						f.write(reinterpret_cast<const char*>(bom), 3);

						// Write content as UTF-8
						for (const string& line : summary_lines) {
							f << line << "\n";
						}
						f.close();
						cout << "summary.txt 파일 저장 완료 (UTF-8): " << summary_file_path.string() << endl;
					}
				}
				catch (const exception& e) {
					cout << "경고: summary.txt 파일 저장 실패 - " << e.what() << endl;
				}
			}
			else {
				cout << "summary.txt에 출력할 내용이 없습니다." << endl;
			}
		}
	}

private:
	// Save results to CSV file (can be opened in Excel)
	void SaveResultsToCSV(
		const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		const map<int, vector<CropMatch>>& unmatched_by_tray,
		const vector<string>& subfolder_names,
		const vector<int>& numbers,
		const vector<int>& tray_indices,
		const string& csv_path) {

		// Open CSV file in binary mode to write UTF-8 BOM
		ofstream csv_file(csv_path, ios::binary);
		if (!csv_file.is_open()) {
			cerr << "Failed to create CSV file: " << csv_path << endl;
			return;
		}

		// Write UTF-8 BOM (0xEF 0xBB 0xBF) for Windows Excel compatibility
		unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
		csv_file.write(reinterpret_cast<const char*>(bom), 3);

		// 목표: prediction_processing_ex.py의 create_excel_from_result_transposed_with_prob_images()
		// 와 동일한 "행렬(Transposed) 구조"로 CSV 저장 (이미지 삽입만 제외).
		//
		// Row 1: prescription_name, prescription_image, tray{t}...
		// Rows 2..: each prescription (display_name 적용: a(2) 등), tray cell에는 "crop_prefix (prob)"를 줄바꿈으로 나열
		// Last row: No Match, tray cell에는 "crop_prefix (prob, best_presc)" 또는 Unknown

		// numbers 길이 보정 (python과 동일하게 짧으면 1로 채우고 길면 자름)
		vector<int> numbers_safe = numbers;
		if (numbers_safe.size() < subfolder_names.size()) {
			numbers_safe.resize(subfolder_names.size(), 1);
		}
		else if (numbers_safe.size() > subfolder_names.size()) {
			numbers_safe.resize(subfolder_names.size());
		}

		map<string, int> name_to_num;
		for (size_t i = 0; i < subfolder_names.size(); ++i) {
			name_to_num[subfolder_names[i]] = numbers_safe[i];
		}

		auto looks_like_long_number = [&](const string& s) -> bool {
			// Excel may auto-format long digit strings as scientific notation.
			// Treat pure-digit strings (e.g., barcode-like IDs) as text by prefixing apostrophe.
			if (s.size() < 10) return false;
			for (char ch : s) {
				if (ch < '0' || ch > '9') return false;
			}
			return true;
			};

		auto excel_text = [&](const string& s) -> string {
			// Leading apostrophe forces Excel to treat the cell as text (usually not displayed).
			return looks_like_long_number(s) ? ("'" + s) : s;
			};

		auto display_name = [&](const string& presc_name) -> string {
			auto it = name_to_num.find(presc_name);
			const int k = (it != name_to_num.end()) ? it->second : 1;
			if (k > 1) return excel_text(presc_name) + "(" + to_string(k) + ")";
			return excel_text(presc_name);
			};

		auto extract_crop_prefix = [&](const string& crop_name) -> string {
			// python: remove extension, then if '(' exists, keep before it, rstrip('_')
			string name_wo_ext = fs::path(crop_name).stem().string();
			const size_t pos = name_wo_ext.find('(');
			string prefix = (pos != string::npos) ? name_wo_ext.substr(0, pos) : name_wo_ext;
			// rstrip '_' and spaces
			while (!prefix.empty() && (prefix.back() == '_' || prefix.back() == ' ' || prefix.back() == '\t')) {
				prefix.pop_back();
			}
			return prefix;
			};

		auto format_prob_2 = [&](float prob) -> string {
			ostringstream oss;
			oss << fixed << setprecision(2) << prob;
			return oss.str();
			};

		auto csv_escape = [&](const string& s) -> string {
			// Always quote fields; double quotes must be escaped as "".
			string out;
			out.reserve(s.size() + 2);
			out.push_back('"');
			for (char ch : s) {
				if (ch == '"') out += "\"\"";
				else out.push_back(ch);
			}
			out.push_back('"');
			return out;
			};

		auto write_row = [&](const vector<string>& fields) {
			for (size_t i = 0; i < fields.size(); ++i) {
				if (i > 0) csv_file << ",";
				csv_file << csv_escape(fields[i]);
			}
			csv_file << "\n";
			};

		// Header row (same shape as final_result.xlsx, without images)
		{
			vector<string> header;
			header.reserve(2 + tray_indices.size());
			header.push_back("prescription_name");
			header.push_back("prescription_image");
			for (int t : tray_indices) {
				header.push_back("tray" + to_string(t));
			}
			write_row(header);
		}

		// Prescription rows (keep the same order as Python: subfolder_names)
		for (const auto& presc_name : subfolder_names) {
			vector<string> row;
			row.reserve(2 + tray_indices.size());
			row.push_back(display_name(presc_name));
			row.push_back(""); // image placeholder column (no images in CSV)

			const auto tray_it = presc_tray_matches.find(presc_name);
			for (int t : tray_indices) {
				string cell;
				if (tray_it != presc_tray_matches.end()) {
					const auto items_it = tray_it->second.find(t);
					if (items_it != tray_it->second.end()) {
						const auto& items = items_it->second;
						for (size_t i = 0; i < items.size(); ++i) {
							const string crop_prefix = extract_crop_prefix(items[i].first);
							const string prob_str = format_prob_2(items[i].second);
							if (!cell.empty()) cell += "\n";
							cell += crop_prefix + " (" + prob_str + ")";
						}
					}
				}
				row.push_back(cell);
			}
			write_row(row);
		}

		// No Match row (last)
		{
			vector<string> row;
			row.reserve(2 + tray_indices.size());
			row.push_back("No Match");
			row.push_back("");

			for (int t : tray_indices) {
				string cell;
				const auto it = unmatched_by_tray.find(t);
				if (it != unmatched_by_tray.end()) {
					for (const auto& match : it->second) {
						const string crop_prefix = extract_crop_prefix(match.crop_name);
						const string prob_str = format_prob_2(match.prob);
						string best_disp;
						if (!match.best_name.empty() && match.prob > 0.0f) best_disp = display_name(match.best_name);
						else best_disp = "Unknown";

						if (!cell.empty()) cell += "\n";
						cell += crop_prefix + " (" + prob_str + ", " + best_disp + ")";
					}
				}
				row.push_back(cell);
			}
			write_row(row);
		}

		csv_file.close();
		cout << "CSV file saved: " << csv_path << endl;
	}

	// Save no_match_trays.bmp (combine tray images with annotations)
	void SaveNoMatchTraysBMP(
		const map<int, vector<CropMatch>>& unmatched_by_tray,
		const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
		const vector<string>& subfolder_names,
		const vector<int>& numbers,
		const string& tray_dir,
		const string& bmp_path) {

		vector<pair<int, cv::Mat>> tray_images;

		// Process each tray that has unmatched items
		for (const auto& [tray_idx, matches] : unmatched_by_tray) {
			if (matches.empty()) continue;

			// Load tray image
			string tray_path = (fs::path(tray_dir) / ("tray" + to_string(tray_idx) + ".bmp")).string();
			if (!fs::exists(tray_path)) {
				// Try other extensions
				vector<string> exts = { ".BMP", ".png", ".PNG", ".jpg", ".JPG" };
				bool found = false;
				for (const auto& ext : exts) {
					string alt_path = (fs::path(tray_dir) / ("tray" + to_string(tray_idx) + ext)).string();
					if (fs::exists(alt_path)) {
						tray_path = alt_path;
						found = true;
						break;
					}
				}
				if (!found) {
					cerr << "Warning: Tray " << tray_idx << " image not found" << endl;
					continue;
				}
			}

			cv::Mat tray_img = cv::imread(tray_path);
			if (tray_img.empty()) {
				cerr << "Warning: Failed to load tray " << tray_idx << " image" << endl;
				continue;
			}

			cv::Mat annotated_img = tray_img.clone();
			int h_tray = tray_img.rows;
			int w_tray = tray_img.cols;
			int padding = 5;

			// Draw red rectangles for unmatched crops
			for (const auto& match : matches) {
				BBox bbox = ParseBBoxFromFilename(match.crop_name);
				if (bbox.x1 < 0) continue;

				int x1_expanded = max(0, bbox.x1 - padding);
				int y1_expanded = max(0, bbox.y1 - padding);
				int x2_expanded = min(w_tray, bbox.x2 + padding);
				int y2_expanded = min(h_tray, bbox.y2 + padding);

				// Draw red rectangle (thickness 3)
				cv::rectangle(annotated_img, cv::Point(x1_expanded, y1_expanded),
					cv::Point(x2_expanded, y2_expanded), cv::Scalar(0, 0, 255), 3);
			}

			tray_images.push_back({ tray_idx, annotated_img });
		}

		if (tray_images.empty()) {
			cout << "No trays to save for no_match_trays.bmp" << endl;
			return;
		}

		// Calculate grid layout
		int num_trays = static_cast<int>(tray_images.size());
		int cols = static_cast<int>(ceil(sqrt(static_cast<double>(num_trays))));
		int rows = static_cast<int>(ceil(static_cast<double>(num_trays) / cols));

		cout << "Grid layout: " << rows << " rows x " << cols << " cols (total " << num_trays << " trays)" << endl;

		// Get image size (assuming all are same size)
		int h_img = tray_images[0].second.rows;
		int w_img = tray_images[0].second.cols;

		// Spacing between images
		int spacing = 10;

		// Calculate total image size
		int total_h = rows * h_img + (rows - 1) * spacing;
		int total_w = cols * w_img + (cols - 1) * spacing;

		// Create combined image (white background)
		cv::Mat combined_img = cv::Mat::ones(total_h, total_w, CV_8UC3) * 255;

		// Place each tray image in grid
		for (size_t idx = 0; idx < tray_images.size(); idx++) {
			int row = static_cast<int>(idx) / cols;
			int col = static_cast<int>(idx) % cols;

			int y_start = row * (h_img + spacing);
			int x_start = col * (w_img + spacing);
			int y_end = y_start + h_img;
			int x_end = x_start + w_img;

			tray_images[idx].second.copyTo(combined_img(cv::Rect(x_start, y_start, w_img, h_img)));
		}

		// Save combined image
		fs::create_directories(fs::path(bmp_path).parent_path());
		if (!cv::imwrite(bmp_path, combined_img)) {
			cerr << "Failed to save image: " << bmp_path << endl;
			return;
		}

		cout << "Total " << num_trays << " tray images saved: " << bmp_path << endl;
		cout << "  Grid size: " << rows << " rows x " << cols << " cols" << endl;
		cout << "  Total image size: " << total_w << "x" << total_h << endl;
	}
};

namespace {
	unique_ptr<PredictionProcessing> g_prediction_processor;
	bool g_prediction_processor_initialized = false;
	string g_prediction_processor_cropped_dir;
	string g_prediction_processor_prescription_dir;
	string g_prediction_processor_result_dir;
	string g_prediction_processor_siamese_model_path;
	string g_prediction_processor_yolo_seg_model_path;
	float g_prediction_processor_threshold = 0.5f;

	PredictionProcessing& GetPredictionProcessor(
		const string& cropped_dir,
		const string& prescription_dir,
		const string& result_dir,
		const string& siamese_model_path,
		const string& yolo_seg_model_path,
		float siamese_threshold) {

		if (!g_prediction_processor) {
			g_prediction_processor_cropped_dir = cropped_dir;
			g_prediction_processor_prescription_dir = prescription_dir;
			g_prediction_processor_result_dir = result_dir;
			g_prediction_processor_siamese_model_path = siamese_model_path;
			g_prediction_processor_yolo_seg_model_path = yolo_seg_model_path;
			g_prediction_processor_threshold = siamese_threshold;

			g_prediction_processor = make_unique<PredictionProcessing>(
				cropped_dir,
				prescription_dir,
				result_dir,
				siamese_model_path,
				yolo_seg_model_path,
				siamese_threshold
			);
		}
		else {
			if (cropped_dir != g_prediction_processor_cropped_dir ||
				prescription_dir != g_prediction_processor_prescription_dir ||
				result_dir != g_prediction_processor_result_dir ||
				siamese_model_path != g_prediction_processor_siamese_model_path ||
				yolo_seg_model_path != g_prediction_processor_yolo_seg_model_path ||
				siamese_threshold != g_prediction_processor_threshold) {
				cout << "Warning: PredictionProcessing singleton already created. "
					<< "Reusing existing instance with previous paths/settings." << endl;
			}
		}

		return *g_prediction_processor;
	}

	bool EnsurePredictionProcessorInitialized(PredictionProcessing& processor) {
		if (!g_prediction_processor_initialized) {
			if (!processor.Initialize()) {
				return false;
			}
			g_prediction_processor_initialized = true;
		}
		return true;
	}
}

extern "C" {
	PILLDETECTIONKERNEL_API int PrewarmPredictionProcessing() {
		int crop_ret = PrewarmCropsModel();
		if (crop_ret != 0) {
			return crop_ret;
		}

		string cropped_dir_str = GetCroppedDir();
		string prescription_dir_str = GetPrescriptionDir();

		if (cropped_dir_str.empty() || prescription_dir_str.empty()) {
			cout << "Error: LOCALAPPDATA environment variable not found." << endl;
			return 1;
		}

		fs::path result_dir = GetResultDir();
		string siamese_model_path = GetSiameseModelPath();
		string yolo_seg_model_path = GetYoloSegModelPath();

		try {
			PredictionProcessing& processor = GetPredictionProcessor(
				cropped_dir_str,
				prescription_dir_str,
				result_dir.string(),
				siamese_model_path,
				yolo_seg_model_path,
				0.5f
			);

			if (!EnsurePredictionProcessorInitialized(processor)) {
				cerr << "Initialization failed" << endl;
				return 1;
			}
		}
		catch (const exception& e) {
			cerr << "Prewarm failed: " << e.what() << endl;
			return 1;
		}
		catch (...) {
			cerr << "Prewarm failed: unknown error" << endl;
			return 1;
		}

		return 0;
	}
}


// Read 'name' and '(number)' from txt file, return (subfolders, numbers)
pair<vector<string>, vector<int>> ReadSubfolderNamesWithNumbers(const string& txt_file) {
	// Parse text file to extract prescription names and pill counts
	// Format: "name(number)" or just "name" (default count is 1)
	// Returns pair of vectors: (subfolder_names, pill_counts)

	vector<string> subfolders;
	vector<int> numbers;

	ifstream file(txt_file);
	if (!file.is_open()) {
		cerr << "Failed to open file: " << txt_file << endl;
		return { subfolders, numbers };
	}

	string line;
	while (getline(file, line)) {
		// Trim whitespace from line
		line.erase(0, line.find_first_not_of(" \t\r\n"));
		line.erase(line.find_last_not_of(" \t\r\n") + 1);

		if (line.empty()) continue;

		string name;
		int num = 1;  // Default pill count is 1

		// Check if line contains number in parentheses: "name(number)"
		size_t open_paren = line.find('(');
		size_t close_paren = line.find(')');

		if (open_paren != string::npos && close_paren != string::npos && close_paren > open_paren) {
			// Extract name (before parentheses)
			name = line.substr(0, open_paren);
			name.erase(0, name.find_first_not_of(" \t"));
			name.erase(name.find_last_not_of(" \t") + 1);

			// Extract number (inside parentheses)
			string num_str = line.substr(open_paren + 1, close_paren - open_paren - 1);
			num_str.erase(0, num_str.find_first_not_of(" \t"));
			num_str.erase(num_str.find_last_not_of(" \t") + 1);

			// Convert string to integer
			try {
				num = stoi(num_str);
			}
			catch (...) {
				num = 1;  // Default to 1 if conversion fails
			}
		}
		else {
			// No parentheses found, use entire line as name
			name = line;
		}

		// Add to result vectors if name is not empty
		if (!name.empty()) {
			subfolders.push_back(name);
			numbers.push_back(num);
		}
	}

	return { subfolders, numbers };
}

// build_transposed_maps_with_prob
void BuildTransposedMapsWithProb(
	const vector<string>& cropped_paths,
	const vector<string>& subfolder_names,
	const vector<vector<float>>& probs_mxn,
	const vector<vector<uint8_t>>& preds_mxn,
	map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
	map<int, vector<CropMatch>>& unmatched_by_tray,
	vector<int>& tray_indices) {


	set<int> tray_indices_set;
	size_t m = cropped_paths.size();
	size_t n = subfolder_names.size();

	std::ostringstream oss;


	for (size_t i = 0; i < m; i++) {

	     string crop_name = fs::path(cropped_paths[i]).filename().string();


		// Extract tray index from filename: img_i_j_(x1,y1,x2,y2).bmp, extract i
		int tray_idx = -1;
		size_t first_underscore = crop_name.find('_');
		if (first_underscore != string::npos) {
			size_t second_underscore = crop_name.find('_', first_underscore + 1);

			if (second_underscore != string::npos) {
				string tray_str = crop_name.substr(first_underscore + 1, second_underscore - first_underscore - 1);
				try {
					tray_idx = stoi(tray_str);
				}
				catch (...) {
					cerr << "[Error] Failed to parse tray index, filename: " << crop_name << endl;
					continue;
				}
			}

		}

		//oss << "tray_idx: " << tray_idx;

		if (tray_idx < 0) {
			cerr << "[Error] Failed to parse tray index, filename: " << crop_name << endl;
			continue;
		}

		tray_indices_set.insert(tray_idx);

		bool matched_any = false;
		for (size_t j = 0; j < n; j++) {
			if (preds_mxn[i][j] == 1) {
				matched_any = true;
				//oss << i;
				//LogToFile("i: ", oss.str());
				//oss << j;
				//LogToFile("j: ", oss.str());
				string presc_name = subfolder_names[j];
				//LogToFile("presc_name: ", presc_name);

				float prob = probs_mxn[i][j];
				//oss << prob;
				//LogToFile("prob: ", oss.str());
				//LogToFile("crop_name: ", crop_name);
				presc_tray_matches[presc_name][tray_idx].push_back({ crop_name, prob });
			}
		}


		// No Match: find best matching presc name
		if (!matched_any) {
			if (n > 0) {
				size_t best_j = 0;
				float best_prob = probs_mxn[i][0];
				for (size_t j = 1; j < n; j++) {
					if (probs_mxn[i][j] > best_prob) {
						best_prob = probs_mxn[i][j];
						best_j = j;
					}
				}
				CropMatch match;
				match.crop_name = crop_name;
				match.prob = best_prob;
				match.best_name = subfolder_names[best_j];
				unmatched_by_tray[tray_idx].push_back(match);
			}
			else {
				CropMatch match;
				match.crop_name = crop_name;
				match.prob = 0.0f;
				match.best_name = "";
				unmatched_by_tray[tray_idx].push_back(match);
			}
		}
	}

	tray_indices.assign(tray_indices_set.begin(), tray_indices_set.end());
	sort(tray_indices.begin(), tray_indices.end());


}

// read_excel_and_extract_unmatched: Extract unmatched from presc_tray_matches (without Excel)
void ReadExcelAndExtractUnmatched(
	const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches,
	const vector<string>& subfolder_names,
	const vector<int>& numbers,
	const vector<int>& tray_indices,
	map<int, map<string, vector<int>>>& C,
	map<int, map<int, pair<int, int>>>& P) {

	cout << "\n=== Required count vs Actual count comparison (extract from presc_tray_matches without Excel) ===" << endl;

	for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
		//presc_name 위치 찾기
		auto presc_name_it = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
		if (presc_name_it == subfolder_names.end()) continue;

		int presc_j = distance(subfolder_names.begin(), presc_name_it);//인덱스 번호
		int required_count = (presc_j < static_cast<int>(numbers.size())) ? numbers[presc_j] : 1;//numbers:조제 갯수 벡터

		for (const auto& [tray_idx, matched_items] : tray_dict) {
			int actual_count = static_cast<int>(matched_items.size());

			// Case: If actual count differs from required count, add to C and P
			if (actual_count != required_count) {
				// Add to P
				P[tray_idx][presc_j] = { required_count, actual_count };

				if (actual_count > required_count) {
					// Over: All matched items in excess need re-evaluation
					for (const auto& [crop_name, prob] : matched_items) {
						C[tray_idx][crop_name].push_back(presc_j);
					}
				}
			}
		}
	}

	// Add crops matched to 2 or more prescriptions to C
	//중복되는 조제 알약은 1개로
	cout << "\n=== Add crops matched to 2 or more prescriptions ===" << endl;
	for (int tray_idx : tray_indices) {
		map<string, vector<int>> crop_to_prescs;

		for (const auto& [presc_name, tray_dict] : presc_tray_matches) {
			if (tray_dict.find(tray_idx) == tray_dict.end()) continue;

			auto presc_name_it = find(subfolder_names.begin(), subfolder_names.end(), presc_name);
			if (presc_name_it == subfolder_names.end()) continue;
			int presc_j = distance(subfolder_names.begin(), presc_name_it);

			for (const auto& [crop_name, prob] : tray_dict.at(tray_idx)) {
				crop_to_prescs[crop_name].push_back(presc_j);
			}
		}

		// Add crops matched to 2 or more prescriptions to C
		for (const auto& [crop_name, presc_j_list] : crop_to_prescs) {
			if (presc_j_list.size() >= 2) {
				if (C[tray_idx].find(crop_name) == C[tray_idx].end()) {
					// New entry
					set<int> unique_presc_j_set(presc_j_list.begin(), presc_j_list.end());
					C[tray_idx][crop_name].assign(unique_presc_j_set.begin(), unique_presc_j_set.end());
				}
				else {
					// Merge with existing C entry (remove duplicates)
					set<int> existing_set(C[tray_idx][crop_name].begin(), C[tray_idx][crop_name].end());
					existing_set.insert(presc_j_list.begin(), presc_j_list.end());
					C[tray_idx][crop_name].assign(existing_set.begin(), existing_set.end());
				}
			}
		}
	}
}

// GenerateMasksWithYOLO wrapper for C: Process only crops in C
void GenerateMasksWithYOLOForC(
	PredictionProcessing& processor,
	const map<int, map<string, vector<int>>>& C,
	const string& cropped_dir,
	const string& predict_dir,
	map<string, MaskInfo>& mask_info) {

	cout << "\n=== YOLO Segmentation Start (crops in C only) ===" << endl;

	fs::create_directories(predict_dir);

	// Collect all crop image names from C
	set<string> all_crop_names;
	for (const auto& [tray_idx, crop_dict] : C) {
		for (const auto& [crop_name, presc_j_list] : crop_dict) {
			all_crop_names.insert(crop_name);
		}
	}

	cout << "Total " << all_crop_names.size() << " crop images need mask generation" << endl;

	vector<string> crop_list(all_crop_names.begin(), all_crop_names.end());
	processor.GenerateMasksWithYOLO(crop_list, predict_dir, mask_info);

	cout << "YOLO Segmentation completed" << endl;
}

// ===================== Main Function =====================
int prediction_main(int start_tray, int end_tray) {
#ifdef _WIN32
	// 콘솔 코드 페이지를 UTF-8로 설정 (한글 및 이모지 표시를 위해)
	SetConsoleOutputCP(65001);  // UTF-8 코드 페이지
	SetConsoleCP(65001);        // 입력도 UTF-8로 설정
#endif

	// Optimize OpenCV for multi-threading
	cv::setNumThreads(0);  // 0 = use all available CPU cores

	// Start timing
	auto start_time = chrono::high_resolution_clock::now();

	// Path setup (경로는 myHeader.h에서 정의됨)
	string cropped_dir_str = GetCroppedDir();
	string prescription_dir_str = GetPrescriptionDir();
	string cropped_masks_path_str = GetCroppedMasksPath();

	if (cropped_dir_str.empty() || prescription_dir_str.empty() || cropped_masks_path_str.empty()) {
		cout << "Error: LOCALAPPDATA environment variable not found." << endl;
		return 1;
	}

	fs::path cropped_dir(cropped_dir_str);
	fs::path prescription_dir(prescription_dir_str);
	fs::path cropped_masks_path(cropped_masks_path_str);

	// Note: cropped_masks 폴더는 ProcessTrayRanges()에서 이미 삭제 및 생성됨

	// Path settings (경로는 myHeader.h에서 정의됨)
	fs::path result_dir = GetResultDir();

	string siamese_model_path = GetSiameseModelPath();  // Siamese model path
	string yolo_seg_model_path = GetYoloSegModelPath();  // YOLO segmentation model path
	string txt_path = GetPillListTxtPath();

	cout << string(60, '=') << endl;
	cout << "Prediction Processing System" << endl;
	if (start_tray >= 1 && end_tray >= start_tray) {
		cout << "Tray Range: [" << start_tray << ", " << end_tray << "]" << endl;
	}
	cout << string(60, '=') << endl;

	try {
		PredictionProcessing& processor = GetPredictionProcessor(
			cropped_dir.string(),
			prescription_dir.string(),
			result_dir.string(),
			siamese_model_path,
			yolo_seg_model_path,
			0.5f //Siamese threshold
		);


		if (!EnsurePredictionProcessorInitialized(processor)) {
			cerr << "Initialization failed" << endl;
			return 1;
		}


		//(1) Read subfolder names and pill counts
		pair<vector<string>, vector<int>> subfolders_and_counts = ReadSubfolderNamesWithNumbers(txt_path);
		const vector<string>& subfolder_names = subfolders_and_counts.first;
		const vector<int>& num_pills = subfolders_and_counts.second;
		cout << "Subfolder count (n): " << subfolder_names.size() << ", Pill count (num): "
			<< accumulate(num_pills.begin(), num_pills.end(), 0) << endl;



		// (2) 디렉토리 내의 모든 파일 목록을 미리 한 번만 가져온다

		vector<string> all_files = GetImageFiles(prescription_dir.string());
		vector<string> presc_paths;
		size_t missing_presc_count = 0;


		for (const auto& name : subfolder_names) {
			bool found = false; // 매칭되는 파일을 찾았는지 확인하는 플래그

			for (const auto& file : all_files) {
				// 조건 1: 파일명이 name으로 시작하는지 확인 (접두사 매칭)
				// 조건 2: 파일명에 "_mask_"가 포함되지 않았는지 확인 (원본 이미지 필터링)
				if (file.find(name) == 0 && file.find("_mask_") == string::npos) {
					presc_paths.push_back(file);
					found = true;
					break; // 해당 name에 대한 파일을 찾았으므로 안쪽 루프 탈출
				}
			}

			// (선택 사항) 만약 매칭되는 파일을 못 찾았을 때 로그를 남기고 싶다면:
			if (!found) {
				cout << "Warning: No match for " << name << endl;
				missing_presc_count++;
			}
		}


		//(3) cropped_dir 에서 m개 이미지 리스트 가져오기 (tray 범위 필터링 적용)
		vector<string> cropped_paths = GetImageFiles(cropped_dir.string(), start_tray, end_tray);
		cout << "Cropped image count: " << cropped_paths.size() << endl;


		//(4) SiameseNet 로드
		auto siamese_inference_start = chrono::high_resolution_clock::now();


		//(5) 확률/예측 행렬 계산
		vector<vector<float>> probs_mxn;
		vector<vector<uint8_t>> preds_mxn;
		processor.ComputeSiameseInference(presc_paths, cropped_paths, probs_mxn, preds_mxn);
		auto siamese_inference_end = chrono::high_resolution_clock::now();
		auto siamese_ms = chrono::duration_cast<chrono::milliseconds>(siamese_inference_end - siamese_inference_start);


		//(6) transposed: Group by presc/tray (with prob)
		map<string, map<int, vector<pair<string, float>>>> presc_tray_matches;
		map<int, vector<CropMatch>> unmatched_by_tray;
		vector<int> tray_indices;

		BuildTransposedMapsWithProb(cropped_paths, subfolder_names, probs_mxn, preds_mxn,
			presc_tray_matches, unmatched_by_tray, tray_indices);


		cout << "presc_tray_matches size: " << presc_tray_matches.size() << endl;
		cout << "unmatched_by_tray size: " << unmatched_by_tray.size() << endl;


		// (7) prepare the Fine-tuning based on Elongation
		// Extract C and P (masks will be saved to cropped_masks_path)
		map<int, map<string, vector<int>>> C;
		map<int, map<int, pair<int, int>>> P;//P[tray_idx][presc_j] = { required_count, actual_count };//제조 개수!= 크롭 개수
		ReadExcelAndExtractUnmatched(presc_tray_matches, subfolder_names, num_pills, tray_indices, C, P);


		//(8) Fine-tuning based on Elongation
		//(8-1)Collect all crop images in C
		set<string> all_crop_names;
		for (const auto& [tray_idx, crop_dict] : C) {
			for (const auto& [crop_name, presc_j_list] : crop_dict) {
				all_crop_names.insert(crop_name);
			}
		}

		if (all_crop_names.empty()) {
			cout << "No crop images need matching." << endl;
		}
		else {
			cout << "\nTotal " << all_crop_names.size() << " crop images need mask generation" << endl;

			// (8-2) Generate masks for images in C using YOLOv8-seg
			// Save masks to cropped_masks_path instead of predict_dir
			map<string, MaskInfo> mask_info;
			vector<string> crop_list(all_crop_names.begin(), all_crop_names.end());
			processor.GenerateMasksWithYOLO(crop_list, cropped_masks_path.string(), mask_info);

			cout << "Mask generation completed: " << mask_info.size() << " masks" << endl;

			// (8-3) Load prescription pill masks (from prescription_dir)
			auto presc_mask_info = processor.LoadPrescriptionMasks(subfolder_names);

			// (8-4) match_with_bipartite_matching()
			processor.MatchWithBipartiteMatching(
				C, mask_info, presc_mask_info,
				subfolder_names, presc_paths, num_pills, tray_indices,
				presc_tray_matches, unmatched_by_tray);

			// (9) validate_no_match_crops()
			processor.ValidateNoMatchCrops(presc_tray_matches, unmatched_by_tray, subfolder_names, num_pills, tray_indices);

			// (9-1) filter_invalid_crops_from_no_match()
			// Use existing masks from cropped_masks_path instead of generating new ones
			processor.FilterInvalidCropsFromNoMatch(unmatched_by_tray, cropped_masks_path.string(), 4, 15.0f);

			// (9-2) save_trays_with_no_match_annotations()
			string tray_dir = (fs::path(result_dir).parent_path() / "trays").string();
			processor.SaveTraysWithNoMatchAnnotations(unmatched_by_tray, presc_tray_matches, subfolder_names, num_pills,
				tray_indices, tray_dir, result_dir.string(), 5, true);
		}

		// (10) Save results or accumulate based on mode
		// If start_tray >= 1, we're in range mode - accumulate results instead of saving
		if (start_tray >= 1 && end_tray >= start_tray) {
			// Range mode: accumulate results to global storage
			extern GlobalResultStorage g_global_results;
			g_global_results.AccumulateResults(presc_tray_matches, unmatched_by_tray, tray_indices);
			// Store subfolder_names and numbers on first call
			if (g_global_results.subfolder_names.empty()) {
				g_global_results.subfolder_names = subfolder_names;
				g_global_results.numbers = num_pills;
			}
			cout << "\nResults accumulated for tray range [" << start_tray << ", " << end_tray << "]" << endl;
		}
		else {
			// Normal mode: save results directly to files
			processor.SaveResultsToFiles(presc_tray_matches, unmatched_by_tray, subfolder_names,
				num_pills, tray_indices, result_dir.string());

			// (11) Print final result statistics (Python print_final_result_statistics equivalent)
			processor.PrintFinalResultStatistics(presc_tray_matches, unmatched_by_tray, subfolder_names,
				num_pills, tray_indices, result_dir.string());
		}

		// Calculate and display execution time
		auto end_time = chrono::high_resolution_clock::now();
		auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);
		auto duration_seconds = chrono::duration_cast<chrono::seconds>(end_time - start_time);
		auto duration_minutes = chrono::duration_cast<chrono::minutes>(end_time - start_time);

		cout << "\n" << string(60, '=') << endl;
		cout << "Prediction processing completed successfully!" << endl;
		cout << string(60, '=') << endl;

		// Display execution time in multiple formats
		if (duration_minutes.count() > 0) {
			auto remaining_seconds = duration_seconds.count() % 60;
			cout << "\nTotal execution time: " << duration_minutes.count() << " minutes "
				<< remaining_seconds << " seconds (" << duration.count() << " ms)" << endl;
		}
		else if (duration_seconds.count() > 0) {
			auto remaining_ms = duration.count() % 1000;
			cout << "\nTotal execution time: " << duration_seconds.count() << "."
				<< setfill('0') << setw(3) << remaining_ms << " seconds (" << duration.count() << " ms)" << endl;
		}
		else {
			cout << "\nTotal execution time: " << duration.count() << " ms" << endl;
		}
		cout << string(60, '=') << endl;


	}
	catch (const exception& e) {
		// Calculate time even on error
		auto end_time = chrono::high_resolution_clock::now();
		auto duration = chrono::duration_cast<chrono::milliseconds>(end_time - start_time);

		cerr << "Error occurred: " << e.what() << endl;
		cerr << "Execution time before error: " << duration.count() << " ms" << endl;
		return 1;
	}

	return 0;
}

// ============================================
// GlobalResultStorage 구현
// ============================================

GlobalResultStorage g_global_results;

void GlobalResultStorage::Clear() {
	presc_tray_matches.clear();
	unmatched_by_tray.clear();
	tray_indices.clear();
	subfolder_names.clear();
	numbers.clear();
}

void GlobalResultStorage::AccumulateResults(
	const map<string, map<int, vector<pair<string, float>>>>& presc_tray_matches_new,
	const map<int, vector<CropMatch>>& unmatched_by_tray_new,
	const vector<int>& tray_indices_new) {

	// Merge presc_tray_matches
	for (const auto& [presc_name, tray_map] : presc_tray_matches_new) {
		for (const auto& [tray_idx, matches] : tray_map) {
			// Append matches to existing ones
			presc_tray_matches[presc_name][tray_idx].insert(
				presc_tray_matches[presc_name][tray_idx].end(),
				matches.begin(), matches.end());
		}
	}

	// Merge unmatched_by_tray
	for (const auto& [tray_idx, matches] : unmatched_by_tray_new) {
		// Append matches to existing ones
		unmatched_by_tray[tray_idx].insert(
			unmatched_by_tray[tray_idx].end(),
			matches.begin(), matches.end());
	}

	// Merge tray_indices (avoid duplicates)
	set<int> tray_set(tray_indices.begin(), tray_indices.end());
	for (int tray_idx : tray_indices_new) {
		tray_set.insert(tray_idx);
	}
	tray_indices.assign(tray_set.begin(), tray_set.end());
	sort(tray_indices.begin(), tray_indices.end());
}

void GlobalResultStorage::SaveFinalResults(const string& result_dir) {
	if (subfolder_names.empty()) {
		cout << "Warning: No results to save. subfolder_names is empty." << endl;
		return;
	}

	cout << "\n" << string(60, '=') << endl;
	cout << "Saving Final Results (All Tray Ranges Combined)" << endl;
	cout << string(60, '=') << endl;

	// Reuse PredictionProcessing singleton for saving
	PredictionProcessing& processor = GetPredictionProcessor(
		GetCroppedDir(),
		GetPrescriptionDir(),
		result_dir,
		GetSiameseModelPath(),
		GetYoloSegModelPath(),
		0.5f
	);

	if (!EnsurePredictionProcessorInitialized(processor)) {
		cerr << "Failed to initialize processor for saving results" << endl;
		return;
	}

	// Save results
	processor.SaveResultsToFiles(presc_tray_matches, unmatched_by_tray, subfolder_names,
		numbers, tray_indices, result_dir);

	// Print statistics
	processor.PrintFinalResultStatistics(presc_tray_matches, unmatched_by_tray, subfolder_names,
		numbers, tray_indices, result_dir);

	cout << "\n" << string(60, '=') << endl;
	cout << "Final Results Saved Successfully!" << endl;
	cout << string(60, '=') << endl;
}
