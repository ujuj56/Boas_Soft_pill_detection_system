// preprocessing.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#define _CRT_SECURE_NO_WARNINGS

#include <iostream>
#include <vector>
#include <string>
#include <utility>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <filesystem>
#ifdef _WIN32
#include <windows.h>
#endif
#include "myHeader.h"

namespace fs = std::filesystem;
using namespace std;
using namespace std::chrono;

//Log ##############################
ofstream g_prediction_log;

string GetLogTimeString() {
	time_t now = time(nullptr);
	tm local_tm{};
#ifdef _WIN32
	localtime_s(&local_tm, &now);
#else
	localtime_r(&now, &local_tm);
#endif
	ostringstream oss;
	oss << put_time(&local_tm, "%Y-%m-%d %H:%M:%S");
	return oss.str();
}

void InitPredictionLog(const fs::path& result_dir) {
	fs::create_directories(result_dir);
	fs::path log_path = result_dir / "myLog.log";
	g_prediction_log.open(log_path, std::ios::out | std::ios::app);
	if (g_prediction_log.is_open()) {
		g_prediction_log << "==== Log start: " << GetLogTimeString() << " ====\n";
		g_prediction_log.flush();
	}
	else {
		cerr << "Warning: Failed to open log file: " << log_path << endl;
	}
}

void LogToFile(const std::string& tag, const std::string& message) {
	if (!g_prediction_log.is_open()) return;
	g_prediction_log << "[" << GetLogTimeString() << "][" << tag << "] " << message << "\n";
	g_prediction_log.flush();
}

void InitLog() {

	fs::path result_dir = GetResultDir();
	InitPredictionLog(result_dir);

}
// 트레이 범위별 연속 처리 함수
int ProcessTrayRanges(const vector<pair<int, int>>& ranges) {
	if (ranges.empty()) {
		cout << "Error: No tray ranges specified." << endl;
		return 1;
	}

	InitLog();

	// 전역 결과 저장소 초기화
	extern GlobalResultStorage g_global_results;
	g_global_results.Clear();

	// cropped, cropped_masks 폴더 삭제 및 생성 (프로그램 시작 시마다 새로 시작)
	string cropped_dir = GetCroppedDir();
	string cropped_masks_path = GetCroppedMasksPath();



	bool test = true;
	if (test == true) {
		if (!cropped_dir.empty()) {
			fs::path cropped_path(cropped_dir);
			if (fs::exists(cropped_path)) {
				fs::remove_all(cropped_path);
				cout << "Cleared cropped folder: " << cropped_path << endl;
			}
			fs::create_directories(cropped_path);
		}
	}



	if (!cropped_masks_path.empty()) {
		fs::path cropped_masks_dir(cropped_masks_path);
		if (fs::exists(cropped_masks_dir)) {
			fs::remove_all(cropped_masks_dir);
			cout << "Cleared cropped_masks folder: " << cropped_masks_dir << endl;
		}
		fs::create_directories(cropped_masks_dir);
	}



	// 전체 시작 시간 측정
	auto total_start_time = chrono::high_resolution_clock::now();

	cout << "\n" << string(60, '=') << endl;
	cout << "Processing Multiple Tray Ranges" << endl;
	cout << string(60, '=') << endl;

	int range_count = 0;
	vector<pair<pair<int, int>, long long>> range_times; // 범위별 시간 저장

	for (const auto& range : ranges) {
		int start = range.first;
		int end = range.second;

		if (start < 1 || end < start) {
			cout << "Warning: Invalid range [" << start << ", " << end << "], skipping..." << endl;
			continue;
		}

		range_count++;
		cout << "\n" << string(60, '-') << endl;
		cout << "Processing Range " << range_count << "/" << ranges.size()
			<< ": [" << start << ", " << end << "]" << endl;
		cout << string(60, '-') << endl;

		std::ostringstream oss;
		//oss << "Processing Range " << range_count << "/" << ranges.size()
		//	<< ": [" << start << ", " << end << "]";
		//LogToFile("PROFILE", oss.str());

		// 구간 시작 시간 측정
		auto range_start_time = chrono::high_resolution_clock::now();

		// Step 1: Crop pills from trays
		auto crop_start_time = chrono::high_resolution_clock::now();
		int ret = crops_main(start, end);
		auto crop_end_time = chrono::high_resolution_clock::now();
		auto crop_duration = chrono::duration_cast<chrono::milliseconds>(crop_end_time - crop_start_time);

		if (ret != 0) {
			cout << "Error: crops_main failed for range [" << start << ", " << end << "]" << endl;
			return ret;
		}
		cout << "  Crop processing time: " << crop_duration.count() << " ms ("
			<< fixed << setprecision(2) << (crop_duration.count() / 1000.0) << " seconds)" << endl;

		//oss << "  Crop processing time: " << crop_duration.count() << " ms ("
		//	<< fixed << setprecision(2) << (crop_duration.count() / 1000.0) << " seconds)";


		// Step 2: Prediction processing (results will be accumulated)
		auto pred_start_time = chrono::high_resolution_clock::now();
		ret = prediction_main(start, end);


		auto pred_end_time = chrono::high_resolution_clock::now();
		auto pred_duration = chrono::duration_cast<chrono::milliseconds>(pred_end_time - pred_start_time);

		if (ret != 0) {
			cout << "Error: prediction_main failed for range [" << start << ", " << end << "]" << endl;
			return ret;
		}
		cout << "  Prediction processing time: " << pred_duration.count() << " ms ("
			<< fixed << setprecision(2) << (pred_duration.count() / 1000.0) << " seconds)" << endl;

		// 구간 종료 시간 측정
		auto range_end_time = chrono::high_resolution_clock::now();
		auto range_duration = chrono::duration_cast<chrono::milliseconds>(range_end_time - range_start_time);
		range_times.push_back({ range, range_duration.count() });

		cout << "\n  Range [" << start << ", " << end << "] Total Time: "
			<< range_duration.count() << " ms ("
			<< fixed << setprecision(2) << (range_duration.count() / 1000.0) << " seconds)" << endl;

		// 분:초 형식으로도 출력
		auto range_seconds = chrono::duration_cast<chrono::seconds>(range_duration);
		auto range_minutes = chrono::duration_cast<chrono::minutes>(range_duration);
		if (range_minutes.count() > 0) {
			auto remaining_seconds = range_seconds.count() % 60;
			cout << "  (" << range_minutes.count() << " minutes " << remaining_seconds << " seconds)" << endl;
		}
	}

	// 모든 범위 처리 완료 후 최종 결과 저장
	cout << "\n" << string(60, '=') << endl;
	cout << "All Tray Ranges Processed. Saving Final Results..." << endl;
	cout << string(60, '=') << endl;

	auto save_start_time = chrono::high_resolution_clock::now();

	string result_dir = GetResultDir();
	g_global_results.SaveFinalResults(result_dir);
	auto save_end_time = chrono::high_resolution_clock::now();
	auto save_duration = chrono::duration_cast<chrono::milliseconds>(save_end_time - save_start_time);

	cout << "Final results saving time: " << save_duration.count() << " ms ("
		<< fixed << setprecision(2) << (save_duration.count() / 1000.0) << " seconds)" << endl;

	// 전체 종료 시간 측정
	auto total_end_time = chrono::high_resolution_clock::now();
	auto total_duration = chrono::duration_cast<chrono::milliseconds>(total_end_time - total_start_time);
	auto total_seconds = chrono::duration_cast<chrono::seconds>(total_duration);
	auto total_minutes = chrono::duration_cast<chrono::minutes>(total_duration);

	// 시간 요약 출력
	cout << "\n" << string(60, '=') << endl;
	cout << "Time Summary" << endl;
	cout << string(60, '=') << endl;

	for (const auto& [range, time_ms] : range_times) {
		cout << "  Range [" << range.first << ", " << range.second << "]: "
			<< time_ms << " ms (" << fixed << setprecision(2) << (time_ms / 1000.0) << " seconds)" << endl;
	}

	cout << "\n  Final Results Saving: " << save_duration.count() << " ms ("
		<< fixed << setprecision(2) << (save_duration.count() / 1000.0) << " seconds)" << endl;

	cout << "\n  TOTAL TIME: " << total_duration.count() << " ms ("
		<< fixed << setprecision(2) << (total_duration.count() / 1000.0) << " seconds)";

	if (total_minutes.count() > 0) {
		auto remaining_seconds = total_seconds.count() % 60;
		cout << " (" << total_minutes.count() << " minutes " << remaining_seconds << " seconds)";
	}
	cout << endl;

	cout << string(60, '=') << endl;
	cout << "All Processing Completed Successfully!" << endl;
	cout << string(60, '=') << endl;

	return 0;
}

int main()
{
#ifdef _WIN32
	// 콘솔 코드 페이지를 UTF-8로 설정 (한글 및 이모지 표시를 위해)
	SetConsoleOutputCP(65001);  // UTF-8 코드 페이지
	SetConsoleCP(65001);        // 입력도 UTF-8로 설정

	// 콘솔 색상 초기화: stdout은 흰색, stderr는 빨간색
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hConsole != INVALID_HANDLE_VALUE) {
		CONSOLE_SCREEN_BUFFER_INFO csbi;
		if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
			// 기본 색상으로 설정 (흰색 텍스트)
			SetConsoleTextAttribute(hConsole, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
		}
	}
#endif

	int ret = 0;
	//ret = masks_main();//마스크 테스트용

	//ret = crops_main();
	//ret = prescription_masks_main();

	// 기존 방식: 모든 트레이 처리
	//ret = prediction_main();

	// 새로운 방식: 트레이 범위별 처리
	// 예: {{1,5}, {6,10}, {11,12}}
	vector<pair<int, int>> ranges = {
		//{1, 12}
		{1, 5},
		{6, 10},
		{11, 12}
	};
	ret = ProcessTrayRanges(ranges);

	return ret;
}



// ============================================
// C#에서 호출하기 위한 C 스타일 인터페이스
// ============================================

extern "C" {
	// main() 함수 대체 - 범위를 외부에서 전달
	PILLDETECTIONKERNEL_API int RunPillDetectionMain(int* ranges, int count) {
		if (!ranges || count <= 0) {
			return 1;
		}

		vector<pair<int, int>> range_vec;
		range_vec.reserve(count);

		for (int i = 0; i < count; i++) {
			int start = ranges[i * 2];
			int end = ranges[i * 2 + 1];
			range_vec.push_back({ start, end });
		}

		return ProcessTrayRanges(range_vec);
	}
}



