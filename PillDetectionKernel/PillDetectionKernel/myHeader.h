#ifndef MY_HEADER_H
#define MY_HEADER_H

#ifdef _WIN32
#ifdef PILLDETECTIONKERNEL_EXPORTS
#define PILLDETECTIONKERNEL_API __declspec(dllexport)
#else
#define PILLDETECTIONKERNEL_API __declspec(dllimport)
#endif
#else
#define PILLDETECTIONKERNEL_API
#endif

#include <vector>
#include <string>
#include <fstream>
#include <utility>
#include <map>
#include <filesystem>

// GPU 설정
// AMD = 0: MX150 (CUDA GPU 가속 사용) - CUDA provider DLL 필요
// AMD = 1: AMD 780M (DirectML GPU 가속 사용) - DirectML 패키지 사용
#define AMD 1
#define doPrint 1
#define BATCH_SIZE 64 //128  // Increased from 64 for better GPU utilization

// ============================================
// 경로 설정 - 공통 경로만 수정하면 모든 경로에 적용됨
// ============================================
#define BASE_PATH R"(C:\jhnt\pill_detection_system\)"

// crops_main()에서 사용하는 경로들
#define PATH_TRAYS BASE_PATH R"(trays)"
#define PATH_EMPTY_TRAY BASE_PATH R"(empty_tray\empty_tray.bmp)"
#define PATH_DATASETS_ORIGIN BASE_PATH R"(datasets_ORIGIN_checked_bbox)"
//#define PATH_YOLO_MODEL R"(../onnx/yolo_best.onnx)"

// prediction_main()에서 사용하는 경로들
#define PATH_PILL_LIST_TXT BASE_PATH R"(trays\pill_list.txt)"
#define PATH_RESULTS BASE_PATH R"(results)"

//onnx
#define BASE_PATH1 R"(C:\jhnt\Test_JHNT\PillDetectionKernel\onnx\)"
#define PATH_YOLO_MODEL BASE_PATH1 R"(yolo_best.onnx)"
#define PATH_SIAMESE_MODEL BASE_PATH1 R"(siamese_best.onnx)"
#define PATH_YOLO_SEG_MODEL BASE_PATH1 R"(yolo_seg_best.onnx)"

// 경로 헬퍼 함수들
// 각 파일에서 자체적으로 namespace fs를 정의하므로 여기서는 정의하지 않음

inline std::string GetTrayPath() {
	return std::string(PATH_TRAYS);
}

inline std::string GetEmptyTrayPath() {
	return std::string(PATH_EMPTY_TRAY);
}

inline std::string GetYoloModelPath() {
	return std::string(PATH_YOLO_MODEL);
}

inline std::string GetResultDir() {
	return std::string(PATH_RESULTS);
}

inline std::string GetSiameseModelPath() {
	return std::string(PATH_SIAMESE_MODEL);
}

inline std::string GetYoloSegModelPath() {
	return std::string(PATH_YOLO_SEG_MODEL);
}

inline std::string GetPillListTxtPath() {
	return std::string(PATH_PILL_LIST_TXT);
}

inline std::string GetDatasetsOriginDir() {
	return std::string(PATH_DATASETS_ORIGIN);
}

// LOCALAPPDATA 기반 경로 헬퍼 함수들
inline std::string GetCroppedDir() {
	const char* app_data_local = std::getenv("LOCALAPPDATA");
	if (!app_data_local) {
		return "";
	}
	return (std::filesystem::path(app_data_local) / "PDS" / "cropped").string();
}

inline std::string GetPrescriptionDir() {
	const char* app_data_local = std::getenv("LOCALAPPDATA");
	if (!app_data_local) {
		return "";
	}
	return (std::filesystem::path(app_data_local) / "PDS" / "prescription").string();
}

inline std::string GetCroppedMasksPath() {
	const char* app_data_local = std::getenv("LOCALAPPDATA");
	if (!app_data_local) {
		return "";
	}
	return (std::filesystem::path(app_data_local) / "PDS" / "cropped_masks").string();
}

extern std::ofstream g_prediction_log;
void InitLog();
void LogToFile(const std::string& tag, const std::string& message);

// 함수 선언 (기본: 모든 트레이 처리)
int crops_main(int start_tray = -1, int end_tray = -1);
int prescription_masks_main();
int prediction_main(int start_tray = -1, int end_tray = -1);
//int masks_main();

// 트레이 범위별 연속 처리 함수
// ranges: {{1,5}, {6,10}, {11,12}} 형태의 벡터
PILLDETECTIONKERNEL_API int ProcessTrayRanges(const std::vector<std::pair<int, int>>& ranges);

// C#에서 호출하기 위한 C 스타일 인터페이스
// ranges: [start1, end1, start2, end2, ...] 형태의 배열, count는 범위의 개수 (쌍의 개수)
extern "C" {
	PILLDETECTIONKERNEL_API int RunPillDetectionMain(int* ranges, int count);  // main() 함수 대체
	PILLDETECTIONKERNEL_API int RunPrescriptionMasksMain();//
	PILLDETECTIONKERNEL_API int PrewarmPredictionProcessing();
	PILLDETECTIONKERNEL_API int PrewarmCropsModel();
}

std::pair<std::vector<std::string>, std::vector<int>> ReadSubfolderNamesWithNumbers(const std::string& txt_file);

// CropMatch 구조체 정의 (prediction_processing_ex.cpp에서 사용)
struct CropMatch {
	std::string crop_name;
	float prob;
	std::string best_name;  // No Match: best matching presc name
};

// 전역 결과 저장소 (여러 범위의 결과를 누적)
struct GlobalResultStorage {
	std::map<std::string, std::map<int, std::vector<std::pair<std::string, float>>>> presc_tray_matches;
	std::map<int, std::vector<CropMatch>> unmatched_by_tray;
	std::vector<int> tray_indices;
	std::vector<std::string> subfolder_names;
	std::vector<int> numbers;

	// 결과 누적 함수
	void AccumulateResults(
		const std::map<std::string, std::map<int, std::vector<std::pair<std::string, float>>>>& presc_tray_matches_new,
		const std::map<int, std::vector<CropMatch>>& unmatched_by_tray_new,
		const std::vector<int>& tray_indices_new);

	// 결과 초기화
	void Clear();

	// 최종 결과 저장
	void SaveFinalResults(const std::string& result_dir);
};

// 전역 결과 저장소 인스턴스 (외부에서 접근 가능)
extern GlobalResultStorage g_global_results;

// Function declarations
void BuildTransposedMapsWithProb(
	const std::vector<std::string>& cropped_paths,
	const std::vector<std::string>& subfolder_names,
	const std::vector<std::vector<float>>& probs_mxn,
	const std::vector<std::vector<uint8_t>>& preds_mxn,
	std::map<std::string, std::map<int, std::vector<std::pair<std::string, float>>>>& presc_tray_matches,
	std::map<int, std::vector<CropMatch>>& unmatched_by_tray,
	std::vector<int>& tray_indices);

void ReadExcelAndExtractUnmatched(
	const std::map<std::string, std::map<int, std::vector<std::pair<std::string, float>>>>& presc_tray_matches,
	const std::vector<std::string>& subfolder_names,
	const std::vector<int>& numbers,
	const std::vector<int>& tray_indices,
	std::map<int, std::map<std::string, std::vector<int>>>& C,
	std::map<int, std::map<int, std::pair<int, int>>>& P);

#endif // MY_HEADER_H
