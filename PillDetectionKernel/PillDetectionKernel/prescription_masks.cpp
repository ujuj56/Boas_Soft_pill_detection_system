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
#include <random>
#include <regex>
#include <cmath>
#include <fstream>
#include <numeric>
#include <atomic>
#include <array>

#include <opencv2/opencv.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "myHeader.h"

namespace fs = std::filesystem;

// BBox structure
struct BBox {
    int x1, y1, x2, y2;
    BBox(int x1, int y1, int x2, int y2) : x1(x1), y1(y1), x2(x2), y2(y2) {}
};

// Tray info structure
struct TrayInfo {
    cv::Point center;
    int R;
    cv::Vec3b C1_bgr;
    cv::Vec3f C1_lab;
    cv::Vec3f C2_lab;
    cv::Size img_shape;
};

static cv::Vec3f compute_lab_median_masked(const cv::Mat& lab8u, const cv::Mat& mask_u8) {
    if (lab8u.empty() || mask_u8.empty()) {
        return cv::Vec3f(0, 128, 128);
    }

    std::array<int, 256> histL{};
    std::array<int, 256> histA{};
    std::array<int, 256> histB{};
    int total = 0;

    for (int y = 0; y < lab8u.rows; y++) {
        const cv::Vec3b* lab_row = lab8u.ptr<cv::Vec3b>(y);
        const uchar* mask_row = mask_u8.ptr<uchar>(y);
        for (int x = 0; x < lab8u.cols; x++) {
            if (mask_row[x] == 0) {
                continue;
            }
            const cv::Vec3b& v = lab_row[x];
            histL[v[0]]++;
            histA[v[1]]++;
            histB[v[2]]++;
            total++;
        }
    }

    if (total == 0) {
        return cv::Vec3f(0, 128, 128);
    }

    auto median_from_hist = [total](const std::array<int, 256>& hist) -> float {
        int acc = 0;
        int target = (total - 1) / 2;
        for (int i = 0; i < 256; i++) {
            acc += hist[i];
            if (acc > target) {
                return static_cast<float>(i);
            }
        }
        return 128.0f;
        };

    return cv::Vec3f(
        median_from_hist(histL),
        median_from_hist(histA),
        median_from_hist(histB)
    );
}

// Helper function: Keep largest connected component
cv::Mat keep_largest_cc(const cv::Mat& mask01) {
    cv::Mat mask = (mask01 > 0);
    mask.convertTo(mask, CV_8U);

    cv::Mat labels, stats, centroids;
    int num = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);

    if (num <= 1) {
        return mask;
    }

    // Find largest component
    int largest_label = 1;
    int largest_area = stats.at<int>(1, cv::CC_STAT_AREA);
    for (int i = 2; i < num; i++) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area > largest_area) {
            largest_area = area;
            largest_label = i;
        }
    }

    cv::Mat result = (labels == largest_label);
    result.convertTo(result, CV_8U);
    return result;
}

// Helper function: Fill holes
cv::Mat fill_holes(const cv::Mat& mask01) {
    cv::Mat m = (mask01 > 0);
    m.convertTo(m, CV_8U, 255);

    int h = m.rows;
    int w = m.cols;

    cv::Mat inv;
    cv::bitwise_not(m, inv);

    cv::Mat ff = inv.clone();
    cv::Mat mask_ff = cv::Mat::zeros(h + 2, w + 2, CV_8U);
    cv::floodFill(ff, mask_ff, cv::Point(0, 0), 0);

    cv::Mat holes;
    cv::bitwise_not(ff, holes);

    cv::Mat filled;
    cv::bitwise_or(m, holes, filled);

    cv::Mat result = (filled > 0);
    result.convertTo(result, CV_8U);
    return result;
}

// Estimate mask from border
cv::Mat estimate_mask_from_border(const cv::Mat& img_bgr, int border = 8) {
    int h = img_bgr.rows;
    int w = img_bgr.cols;

    cv::Mat lab;
    cv::cvtColor(img_bgr, lab, cv::COLOR_BGR2Lab);
    lab.convertTo(lab, CV_32F);

    int b = std::max(2, std::min(border, std::min(h, w) / 6));

    // Collect border pixels
    std::vector<cv::Vec3f> border_pixels;
    for (int y = 0; y < b; y++) {
        for (int x = 0; x < w; x++) {
            border_pixels.push_back(lab.at<cv::Vec3f>(y, x));
        }
    }
    for (int y = h - b; y < h; y++) {
        for (int x = 0; x < w; x++) {
            border_pixels.push_back(lab.at<cv::Vec3f>(y, x));
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < b; x++) {
            border_pixels.push_back(lab.at<cv::Vec3f>(y, x));
        }
    }
    for (int y = 0; y < h; y++) {
        for (int x = w - b; x < w; x++) {
            border_pixels.push_back(lab.at<cv::Vec3f>(y, x));
        }
    }

    // Calculate median
    cv::Vec3f bg_lab(0, 128, 128);
    if (!border_pixels.empty()) {
        std::vector<float> l_vals, a_vals, b_vals;
        for (const auto& pix : border_pixels) {
            l_vals.push_back(pix[0]);
            a_vals.push_back(pix[1]);
            b_vals.push_back(pix[2]);
        }
        std::sort(l_vals.begin(), l_vals.end());
        std::sort(a_vals.begin(), a_vals.end());
        std::sort(b_vals.begin(), b_vals.end());
        bg_lab[0] = l_vals[l_vals.size() / 2];
        bg_lab[1] = a_vals[a_vals.size() / 2];
        bg_lab[2] = b_vals[b_vals.size() / 2];
    }

    // Calculate distance
    cv::Mat dist(h, w, CV_32F);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            cv::Vec3f diff = lab.at<cv::Vec3f>(y, x) - bg_lab;
            float d = std::sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
            dist.at<float>(y, x) = d;
        }
    }

    // Normalize and threshold
    double min_val, max_val;
    cv::minMaxLoc(dist, &min_val, &max_val);
    cv::Mat dist_u8;
    dist.convertTo(dist_u8, CV_8U, 255.0 / (max_val + 1e-6), 0);

    cv::Mat fg;
    cv::threshold(dist_u8, fg, 0, 255, cv::THRESH_BINARY + cv::THRESH_OTSU);

    cv::Mat mask01 = (fg > 0);
    mask01.convertTo(mask01, CV_8U);

    // Morphology
    int k = std::max(3, (std::min(h, w) / 120) * 2 + 1);
    if (k % 2 == 0) k++;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::morphologyEx(mask01, mask01, cv::MORPH_OPEN, kernel, cv::Point(-1, -1), 1);
    cv::morphologyEx(mask01, mask01, cv::MORPH_CLOSE, kernel, cv::Point(-1, -1), 2);

    mask01 = fill_holes(mask01);
    mask01 = keep_largest_cc(mask01);

    return mask01;
}

// Clip bbox
BBox clip_bbox(const BBox& bb, int W, int H) {
    int x1 = std::max(0, std::min(bb.x1, W - 1));
    int y1 = std::max(0, std::min(bb.y1, H - 1));
    int x2 = std::max(1, std::min(bb.x2, W));
    int y2 = std::max(1, std::min(bb.y2, H));

    if (x2 <= x1 + 1) {
        x2 = std::min(W, x1 + 2);
    }
    if (y2 <= y1 + 1) {
        y2 = std::min(H, y1 + 2);
    }

    return BBox(x1, y1, x2, y2);
}

// Get tray info and C1, C2
TrayInfo get_tray_info_and_C1_C2(const std::string& image_path, int border = 5) {
    TrayInfo info;
    info.center = cv::Point(-1, -1);
    info.R = -1;

    cv::Mat img = cv::imread(image_path);
    if (img.empty()) {
        return info;
    }

    cv::Mat gray;
    cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    cv::Mat blurred;
    cv::medianBlur(gray, blurred, 5);

    // Speed: run HoughCircles on a downscaled image, then scale back.
    double scale = 1.0;
    const int max_dim = std::max(blurred.cols, blurred.rows);
    if (max_dim > 800) {
        scale = 800.0 / static_cast<double>(max_dim);
    }
    cv::Mat hough_input = blurred;
    if (scale < 1.0) {
        cv::resize(blurred, hough_input, cv::Size(), scale, scale, cv::INTER_AREA);
    }

    int rows = hough_input.rows;

    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(hough_input, circles, cv::HOUGH_GRADIENT, 1, rows / 8.0, 70, 15, 10, rows / 2 - 5);

    if (circles.empty()) {
        return info;
    }

    cv::Vec3f circle = circles[0];
    int x_center = static_cast<int>(std::round(circle[0] / scale));
    int y_center = static_cast<int>(std::round(circle[1] / scale));
    int R = static_cast<int>(std::round(circle[2] / scale));

    info.center = cv::Point(x_center, y_center);
    info.R = R;
    info.img_shape = img.size();

    int H = img.rows;
    int W = img.cols;

    // Create masks
    cv::Mat mask_R_inner = cv::Mat::zeros(H, W, CV_8U);
    cv::circle(mask_R_inner, info.center, R, 255, -1);

    cv::Mat mask_R_outer;
    cv::bitwise_not(mask_R_inner, mask_R_outer);

    cv::Mat mask_outer_valid = mask_R_outer.clone();
    mask_outer_valid(cv::Rect(0, 0, W, border)) = 0;
    mask_outer_valid(cv::Rect(0, H - border, W, border)) = 0;
    mask_outer_valid(cv::Rect(0, 0, border, H)) = 0;
    mask_outer_valid(cv::Rect(W - border, 0, border, H)) = 0;

    // C1: BGR mean (only required value for mask generation)
    cv::Scalar c1_bgr_mean = cv::mean(img, mask_R_inner);
    info.C1_bgr = cv::Vec3b(
        static_cast<uchar>(c1_bgr_mean[0]),
        static_cast<uchar>(c1_bgr_mean[1]),
        static_cast<uchar>(c1_bgr_mean[2])
    );
    info.C1_lab = cv::Vec3f(0, 128, 128);
    info.C2_lab = cv::Vec3f(0, 128, 128);

    std::cout << "center: (" << info.center.x << ", " << info.center.y << ") R: " << info.R << std::endl;

    return info;
}

// Make circle mask
cv::Mat make_circle_mask(int H, int W, const cv::Point& center, int R) {
    cv::Mat m = cv::Mat::zeros(H, W, CV_8U);
    cv::circle(m, center, R, 255, -1);
    return m;
}

// Lab distance
cv::Mat lab_distance(const cv::Mat& lab_img, const cv::Vec3f& lab_color) {
    cv::Mat lab_f;
    lab_img.convertTo(lab_f, CV_32F);
    cv::Mat dist(lab_f.rows, lab_f.cols, CV_32F);

    for (int y = 0; y < lab_f.rows; y++) {
        for (int x = 0; x < lab_f.cols; x++) {
            cv::Vec3f diff = lab_f.at<cv::Vec3f>(y, x) - lab_color;
            float d = std::sqrt(diff[0] * diff[0] + diff[1] * diff[1] + diff[2] * diff[2]);
            dist.at<float>(y, x) = d;
        }
    }

    return dist;
}

// Get image files
std::vector<std::string> get_image_files(const std::string& folder) {
    std::vector<std::string> files;

    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        return files;
    }

    std::vector<std::string> valid_exts = { ".bmp", ".BMP", ".png", ".PNG", ".jpg", ".JPG", ".jpeg", ".JPEG" };

    for (const auto& entry : fs::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            std::string ext = entry.path().extension().string();
            if (std::find(valid_exts.begin(), valid_exts.end(), ext) != valid_exts.end()) {
                files.push_back(entry.path().string());
            }
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

// Parse bbox from filename
std::tuple<int, int, int, int> parse_bbox_from_filename(const std::string& filename) {
    std::regex pattern(R"(\((\d+),(\d+),(\d+),(\d+)\))");
    std::smatch match;

    if (std::regex_search(filename, match, pattern)) {
        return std::make_tuple(
            std::stoi(match[1].str()),
            std::stoi(match[2].str()),
            std::stoi(match[3].str()),
            std::stoi(match[4].str())
        );
    }

    return std::make_tuple(-1, -1, -1, -1);
}

// Create pill mask
cv::Mat create_pill_mask(const cv::Vec3b& background_color_bgr, const cv::Mat& pill_image_bgr,
    int tolerance = 30, int erode_pixels = 0, bool debug = false) {
    if (pill_image_bgr.empty()) {
        throw std::runtime_error("pill_image_bgr is empty");
    }

    cv::Mat pill_bgr_f;
    pill_image_bgr.convertTo(pill_bgr_f, CV_32F);

    cv::Vec3f bg_bgr(background_color_bgr[0], background_color_bgr[1], background_color_bgr[2]);
    cv::Vec3f black_bgr(0, 0, 0);
    cv::Vec3f white_bgr(255, 255, 255);

    int H = pill_image_bgr.rows;
    int W = pill_image_bgr.cols;

    cv::Mat bg_diff(H, W, CV_32F);
    cv::Mat black_diff(H, W, CV_32F);
    cv::Mat white_diff(H, W, CV_32F);

    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            cv::Vec3f pix = pill_bgr_f.at<cv::Vec3f>(y, x);
            cv::Vec3f d1 = pix - bg_bgr;
            cv::Vec3f d2 = pix - black_bgr;
            cv::Vec3f d3 = pix - white_bgr;
            bg_diff.at<float>(y, x) = std::sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]);
            black_diff.at<float>(y, x) = std::sqrt(d2[0] * d2[0] + d2[1] * d2[1] + d2[2] * d2[2]);
            white_diff.at<float>(y, x) = std::sqrt(d3[0] * d3[0] + d3[1] * d3[1] + d3[2] * d3[2]);
        }
    }

    cv::Mat mask;
    cv::Mat mask1 = (bg_diff > tolerance);
    cv::Mat mask2 = (black_diff > tolerance);
    cv::Mat mask3 = (white_diff > tolerance);
    cv::bitwise_and(mask1, mask2, mask);
    cv::bitwise_and(mask, mask3, mask);

    cv::Mat pill_gray;
    cv::cvtColor(pill_image_bgr, pill_gray, cv::COLOR_BGR2GRAY);
    cv::Mat dark_mask = (pill_gray > 30);
    cv::bitwise_and(mask, dark_mask, mask);

    mask.convertTo(mask, CV_8U, 255);

    cv::Mat kernel_clean = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel_clean);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel_clean);

    // Keep largest component
    cv::Mat labels, stats, centroids;
    int num_labels = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);

    if (num_labels > 1) {
        int largest_label = 1;
        int largest_area = stats.at<int>(1, cv::CC_STAT_AREA);
        for (int i = 2; i < num_labels; i++) {
            int area = stats.at<int>(i, cv::CC_STAT_AREA);
            if (area > largest_area) {
                largest_area = area;
                largest_label = i;
            }
        }
        mask = (labels == largest_label);
        mask.convertTo(mask, CV_8U, 255);
    }

    if (erode_pixels > 0) {
        int k = erode_pixels * 2 + 1;
        cv::Mat erode_kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
        cv::erode(mask, mask, erode_kernel);
    }

    cv::Mat final_mask = (mask > 0);

    if (debug) {
        // Debug prints (ASCII only to avoid codepage/encoding issues on Windows).
        std::cout << "Original image size: " << pill_image_bgr.size() << std::endl;
        int true_count = cv::countNonZero(final_mask);
        std::cout << "True pixel count: " << true_count << std::endl;
        std::cout << "Pill ratio: " << (true_count * 100.0 / static_cast<double>(final_mask.total())) << "%" << std::endl;
    }

    return final_mask;
}

// Compute elongation from mask
double compute_elongation_from_mask(const cv::Mat& mask_u8, int min_fg_pixels = 20) {
    cv::Mat fg = (mask_u8 > 0);
    std::vector<cv::Point> points;
    cv::findNonZero(fg, points);

    int n = static_cast<int>(points.size());
    if (n < min_fg_pixels) {
        return std::numeric_limits<double>::infinity();
    }

    // Calculate mean
    cv::Point2f mean(0, 0);
    for (const auto& pt : points) {
        mean.x += pt.x;
        mean.y += pt.y;
    }
    mean.x /= n;
    mean.y /= n;

    // Calculate covariance
    double cov_xx = 0, cov_xy = 0, cov_yy = 0;
    for (const auto& pt : points) {
        float dx = pt.x - mean.x;
        float dy = pt.y - mean.y;
        cov_xx += dx * dx;
        cov_xy += dx * dy;
        cov_yy += dy * dy;
    }
    cov_xx /= (n - 1);
    cov_xy /= (n - 1);
    cov_yy /= (n - 1);

    // Calculate eigenvalues
    double trace = cov_xx + cov_yy;
    double det = cov_xx * cov_yy - cov_xy * cov_xy;
    double discriminant = trace * trace - 4 * det;

    if (discriminant < 0) {
        return std::numeric_limits<double>::infinity();
    }

    double lambda1 = (trace + std::sqrt(discriminant)) / 2.0;
    double lambda2 = (trace - std::sqrt(discriminant)) / 2.0;

    double major = std::sqrt(std::max(lambda1, 1e-12));
    double minor = std::sqrt(std::max(lambda2, 1e-12));

    if (minor < 1e-12) {
        return std::numeric_limits<double>::infinity();
    }

    return major / minor;
}

// Compute min area rect area
double compute_minarearect_area(const cv::Mat& mask_u8, int min_fg_pixels = 20) {
    cv::Mat fg = (mask_u8 > 0);
    fg.convertTo(fg, CV_8U, 255);

    if (cv::countNonZero(fg) < min_fg_pixels) {
        return std::numeric_limits<double>::infinity();
    }

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fg, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) {
        return std::numeric_limits<double>::infinity();
    }

    // Find largest contour
    double max_area = 0;
    int max_idx = 0;
    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area > max_area) {
            max_area = area;
            max_idx = static_cast<int>(i);
        }
    }

    cv::RotatedRect rect = cv::minAreaRect(contours[max_idx]);
    cv::Size2f size = rect.size;
    double area = size.width * size.height;

    if (area <= 0) {
        return std::numeric_limits<double>::infinity();
    }

    return area;
}

// Read subfolder names from txt file
std::pair<std::vector<std::string>, std::vector<int>> read_subfolder_names(const std::string& txt_file) {
    std::vector<std::string> subfolders;
    std::vector<int> numbers;

    std::ifstream file(txt_file);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open file: " + txt_file);
    }

    std::regex pattern(R"(^\s*(.*?)\s*(?:\(\s*(\d+)\s*\))?\s*$)");
    std::string line;

    auto strip_leading_utf8_invisible = [](std::string& s) {
        // Remove UTF-8 BOM and common zero-width characters only at the start.
        const std::vector<std::string> prefixes = {
            "\xEF\xBB\xBF", // U+FEFF BOM
            "\xE2\x80\x8B", // U+200B zero-width space
            "\xE2\x80\x8C", // U+200C zero-width non-joiner
            "\xE2\x80\x8D"  // U+200D zero-width joiner
        };

        bool removed = true;
        while (removed && !s.empty()) {
            removed = false;
            for (const auto& p : prefixes) {
                if (s.rfind(p, 0) == 0) {
                    s.erase(0, p.size());
                    removed = true;
                    break;
                }
            }
        }
        };

    while (std::getline(file, line)) {
        if (line.empty() || line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }

        strip_leading_utf8_invisible(line);

        std::smatch match;
        if (std::regex_match(line, match, pattern)) {
            std::string name = match[1].str();
            // Trim whitespace
            name.erase(0, name.find_first_not_of(" \t"));
            name.erase(name.find_last_not_of(" \t") + 1);
            strip_leading_utf8_invisible(name);

            std::string num_str = match[2].str();
            int num = num_str.empty() ? 1 : std::stoi(num_str);

            subfolders.push_back(name);
            numbers.push_back(num);
        }
        else {
            // No match, use whole line as name
            subfolders.push_back(line);
            numbers.push_back(1);
        }
    }

    return std::make_pair(subfolders, numbers);
}

// Copy prescription images from origin
void copy_prescription_images_from_origin(const std::string& txt_path, const std::string& origin_dir,
    const std::string& prescription_dir) {
    std::vector<std::string> pill_folders;
    std::vector<int> num_pills;

    try {
        auto result = read_subfolder_names(txt_path);
        pill_folders = result.first;
        num_pills = result.second;
    }
    catch (const std::exception& e) {
        std::cout << "Error: Cannot find file: " << txt_path << std::endl;
        return;
    }

    fs::create_directories(prescription_dir);

    std::vector<std::string> ext_patterns = { ".bmp", ".png", ".jpg", ".jpeg", ".tif", ".tiff" };

    std::random_device rd;
    std::mt19937 gen(rd());

    int copied_count = 0;

    for (const auto& pill_name : pill_folders) {
        std::string pill_name_clean = pill_name;

        if (pill_name.find('(') != std::string::npos) {
            pill_name_clean = pill_name.substr(0, pill_name.find('('));
            // Trim whitespace
            pill_name_clean.erase(0, pill_name_clean.find_first_not_of(" \t"));
            pill_name_clean.erase(pill_name_clean.find_last_not_of(" \t") + 1);
        }

        fs::path current_pill_folder = fs::u8path(origin_dir) / fs::u8path(pill_name_clean);

        if (!fs::exists(current_pill_folder) || !fs::is_directory(current_pill_folder)) {

            std::cout << "  Warning: Cannot find folder -> " << current_pill_folder.u8string() << std::endl;
            continue;
        }

        std::vector<fs::path> image_paths;
        for (const auto& entry : fs::directory_iterator(current_pill_folder)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().u8string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                for (const auto& pattern : ext_patterns) {
                    if (ext == pattern) {
                        image_paths.push_back(entry.path());
                        break;
                    }
                }
            }
        }

        //std::cout << "3" << std::endl;
        std::sort(image_paths.begin(), image_paths.end());


        if (image_paths.empty()) {
            std::cout << "  Warning: No image files found -> " << current_pill_folder.u8string() << std::endl;
            continue;
        }

        // Random selection
        std::uniform_int_distribution<> dis(0, static_cast<int>(image_paths.size() - 1));
        fs::path src_path = image_paths[dis(gen)];

        std::string ext = src_path.extension().u8string();
        std::string dst_name = pill_name_clean + ext;
        fs::path dst_path = fs::path(prescription_dir) / dst_name;

        try {
            fs::copy_file(src_path, dst_path, fs::copy_options::overwrite_existing);
            copied_count++;
            // ASCII-only logs to avoid Windows codepage/encoding issues.
            std::cout << "  Copy OK: " << dst_name << " (from " << src_path.filename().u8string() << ")" << std::endl;
        }
        catch (const std::exception& e) {
            std::cout << "  Warning: copy failed -> " << dst_path.u8string() << ", error: " << e.what() << std::endl;
        }
    }

    std::cout << "\nTotal " << copied_count << " images copied to " << prescription_dir << "." << std::endl;
}

// Create pill masks with C1
void create_pill_masks_with_C1_HSV(const cv::Vec3b& C1_bgr, const std::string& folder_txt_path,
    const std::string& folder_ORIGIN_path, const std::string& mask_path) {
    const auto func_start = std::chrono::high_resolution_clock::now();
    std::vector<std::string> pill_folders;
    std::vector<int> num_pills;

    try {
        auto result = read_subfolder_names(folder_txt_path);
        pill_folders = result.first;
        num_pills = result.second;
    }
    catch (const std::exception& e) {
        std::cout << "Error: failed to read txt file: " << folder_txt_path << std::endl;
        return;
    }

    fs::create_directories(mask_path);
    std::atomic<int> total_mask_count{ 0 };

    std::vector<std::string> ext_patterns = { ".bmp", ".png", ".jpg", ".jpeg", ".tif", ".tiff" };

    for (const auto& pill_name : pill_folders) {
        const auto folder_start = std::chrono::high_resolution_clock::now();
        std::string pill_name_clean = pill_name;
        if (pill_name.find('(') != std::string::npos) {
            pill_name_clean = pill_name.substr(0, pill_name.find('('));
            pill_name_clean.erase(0, pill_name_clean.find_first_not_of(" \t"));
            pill_name_clean.erase(pill_name_clean.find_last_not_of(" \t") + 1);
        }

        fs::path current_pill_folder = fs::u8path(folder_ORIGIN_path) / fs::u8path(pill_name_clean);
        if (!fs::exists(current_pill_folder) || !fs::is_directory(current_pill_folder)) {
            std::cout << "Warning: folder not found -> " << current_pill_folder.u8string() << std::endl;
            continue;
        }

        std::vector<fs::path> image_paths;
        for (const auto& entry : fs::directory_iterator(current_pill_folder)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().u8string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                for (const auto& pattern : ext_patterns) {
                    if (ext == pattern) {
                        image_paths.push_back(entry.path());
                        break;
                    }
                }
            }
        }

        std::sort(image_paths.begin(), image_paths.end());

        if (image_paths.empty()) {
            continue;
        }

        std::cout << "\n-> Processing folder: " << pill_name << " -> " << pill_name_clean
            << " (total " << image_paths.size() << " images)" << std::endl;

        const std::string pill_name_for_file = pill_name;
        const std::string mask_dir = mask_path;
        std::atomic<int> folder_saved_count{ 0 };
        std::atomic<int> folder_failed_count{ 0 };

        const auto work_start = std::chrono::high_resolution_clock::now();
        cv::parallel_for_(cv::Range(0, static_cast<int>(image_paths.size())),
            [&](const cv::Range& range) {
                for (int i = range.start; i < range.end; ++i) {
                    const auto& image_path = image_paths[i];
                    cv::Mat img = cv::imread(image_path.u8string(), cv::IMREAD_COLOR);
                    if (img.empty()) {
                        folder_failed_count++;
                        continue;
                    }

                    cv::Mat pill_mask_bool = create_pill_mask(C1_bgr, img, 30, 0, false);
                    cv::Mat pill_mask_u8;
                    pill_mask_bool.convertTo(pill_mask_u8, CV_8U, 255);

                    double elong = compute_elongation_from_mask(pill_mask_u8);
                    double area = compute_minarearect_area(pill_mask_u8);

                    int k = i + 1;
                    std::ostringstream oss;
                    if (std::isfinite(elong) && std::isfinite(area)) {
                        oss << pill_name_for_file << "_mask_" << k << "_("
                            << std::fixed << std::setprecision(2) << elong << ", "
                            << std::setprecision(1) << area << ").bmp";
                    }
                    else {
                        oss << pill_name_for_file << "_mask_" << k << "_(inf, inf).bmp";
                    }

                    fs::path output_path = fs::path(mask_dir) / oss.str();
                    if (cv::imwrite(output_path.string(), pill_mask_u8)) {
                        folder_saved_count++;
                        total_mask_count++;
                    }
                    else {
                        folder_failed_count++;
                    }
                }
            });
        const auto work_end = std::chrono::high_resolution_clock::now();

        const auto folder_end = std::chrono::high_resolution_clock::now();
        auto work_ms = std::chrono::duration_cast<std::chrono::milliseconds>(work_end - work_start).count();
        auto folder_ms = std::chrono::duration_cast<std::chrono::milliseconds>(folder_end - folder_start).count();
        std::cout << "  Saved masks: " << folder_saved_count.load()
            << ", failed: " << folder_failed_count.load()
            << ", work ms: " << work_ms
            << ", total ms: " << folder_ms << std::endl;
    }

    std::cout << "\n--- Done ---" << std::endl;
    std::cout << "Total " << total_mask_count << " mask images saved to " << mask_path << "." << std::endl;
    const auto func_end = std::chrono::high_resolution_clock::now();
    auto func_ms = std::chrono::duration_cast<std::chrono::milliseconds>(func_end - func_start).count();
    {
        std::ostringstream oss;
        oss << "create_pill_masks_with_C1_HSV total ms: " << func_ms;
        //LogToFile("PROFILE", oss.str());
    }
}


// ===================== Simple File Logger for prediction_main =====================

int prescription_masks_main() {

    //InitLog();

#ifdef _WIN32
    // Set console code page to UTF-8 (for Korean and emoji display)
    SetConsoleOutputCP(65001);  // UTF-8 code page
    SetConsoleCP(65001);        // Input also UTF-8
#endif


    std::string empty_tray_path = GetEmptyTrayPath();
    std::string txt_path = GetPillListTxtPath();
    std::string datasets_origin_dir = GetDatasetsOriginDir();
    std::string prescription_dir_str = GetPrescriptionDir();

    //fs::path result_dir = GetResultDir();
    //InitPredictionLog(result_dir);
    //LogToFile("START", "prescription_masks_main()");

    if (prescription_dir_str.empty()) {
        std::cout << "Error: Cannot find LOCALAPPDATA environment variable." << std::endl;
        return 1;
    }

    fs::path prescription_dir(prescription_dir_str);

    // Delete folder if exists
    if (fs::exists(prescription_dir)) {
        fs::remove_all(prescription_dir);
    }

    // Create clean folder
    fs::create_directories(prescription_dir);
    //LogToFile(">", "create_directories");

    const auto total_start = std::chrono::high_resolution_clock::now();
    std::cout << std::string(60, '=') << std::endl;
    std::cout << "Prescription mask generation" << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    // Read subfolder names
    std::vector<std::string> subfolder_names;
    std::vector<int> num_pills;
    try {
         //LogToFile(">", "read_subfolder_names 1");

        auto result = read_subfolder_names(txt_path);

         //LogToFile(">", "read_subfolder_names 2");

        subfolder_names = result.first;
        num_pills = result.second;
    }
    catch (const std::exception& e) {
        std::cout << "Error: failed to read txt file: " << txt_path << std::endl;
        return 1;
    }

    std::cout << "\nSubfolder count: " << subfolder_names.size() << std::endl;

    // Get tray info
    std::cout << "\n[Step 1] Generate prescription images and masks..." << std::endl;

    const auto tray_start = std::chrono::high_resolution_clock::now();
    TrayInfo tray_info = get_tray_info_and_C1_C2(empty_tray_path);
    const auto tray_end = std::chrono::high_resolution_clock::now();
    if (tray_info.R < 0) {
        std::cout << "Error: failed to extract info from empty_tray image." << std::endl;
        return 1;
    }

    std::cout << "--- Extracted ---" << std::endl;
    std::cout << "R (pixels): " << tray_info.R << std::endl;
    std::cout << "C1 (BGR): (" << static_cast<int>(tray_info.C1_bgr[0]) << ", "
        << static_cast<int>(tray_info.C1_bgr[1]) << ", " << static_cast<int>(tray_info.C1_bgr[2]) << ")" << std::endl;
    std::cout << "C1_lab: (" << tray_info.C1_lab[0] << ", " << tray_info.C1_lab[1] << ", " << tray_info.C1_lab[2] << ")" << std::endl;
    std::cout << "C2_lab: (" << tray_info.C2_lab[0] << ", " << tray_info.C2_lab[1] << ", " << tray_info.C2_lab[2] << ")" << std::endl;

    try {
        // Copy prescription images
        std::cout << "\n[Step 1-1] Copy prescription images (origin_dir -> prescription_dir)..." << std::endl;
        const auto copy_start = std::chrono::high_resolution_clock::now();
        copy_prescription_images_from_origin(txt_path, datasets_origin_dir, prescription_dir.string());
        const auto copy_end = std::chrono::high_resolution_clock::now();

        // Create masks
        std::cout << "\n[Step 1-2] Create prescription masks..." << std::endl;
        const auto mask_start = std::chrono::high_resolution_clock::now();
        create_pill_masks_with_C1_HSV(tray_info.C1_bgr, txt_path, datasets_origin_dir, prescription_dir.string());
        const auto mask_end = std::chrono::high_resolution_clock::now();

        auto tray_ms = std::chrono::duration_cast<std::chrono::milliseconds>(tray_end - tray_start).count();
        auto copy_ms = std::chrono::duration_cast<std::chrono::milliseconds>(copy_end - copy_start).count();
        auto mask_ms = std::chrono::duration_cast<std::chrono::milliseconds>(mask_end - mask_start).count();
        {
            std::ostringstream oss;
            oss << "tray_info ms: " << tray_ms
                << ", copy ms: " << copy_ms
                << ", masks ms: " << mask_ms;
            //LogToFile("PROFILE", oss.str());
        }

    }
    catch (const std::exception& e) {
        std::cout << "  Error: " << e.what() << std::endl;
    }

    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "Mask generation done." << std::endl;
    std::cout << std::string(60, '=') << std::endl;

    const auto total_end = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_end - total_start).count();
    {
        std::ostringstream oss;
        oss << "prescription_masks_main total ms: " << total_ms;
        //LogToFile("PROFILE", oss.str());
    }
    return 0;
}

extern "C" {
    PILLDETECTIONKERNEL_API int RunPrescriptionMasksMain() {
        return prescription_masks_main();
    }
}
