using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using OfficeOpenXml;
using OfficeOpenXml.Style;
using Newtonsoft.Json;
using Newtonsoft.Json.Linq;

namespace Test_JHNT
{
    public class ResultExporter
    {
        public class MatchingResult
        {
            public Dictionary<string, Dictionary<int, List<Tuple<string, float>>>> PrescTrayMatches { get; set; }
            public Dictionary<int, List<Tuple<string, float, string>>> UnmatchedByTray { get; set; }
            public List<int> TrayIndices { get; set; }
            public List<string> SubfolderNames { get; set; }
            public List<int> Numbers { get; set; }
            public string PrescriptionDir { get; set; }
            public string CroppedDir { get; set; }
            public string ResultDir { get; set; }
        }

        public static void ExportResults(string jsonResult, string resultDir)
        {
            try
            {
                var result = JsonConvert.DeserializeObject<MatchingResult>(jsonResult);
                
                // 디렉토리 생성
                Directory.CreateDirectory(resultDir);

                // 1. final_result.xlsx 저장
                SaveFinalResultExcel(result, Path.Combine(resultDir, "final_result.xlsx"));

                // 2. final_result.txt 저장
                SaveFinalResultText(result, Path.Combine(resultDir, "final_result.txt"));

                // 3. summary.txt 저장
                SaveSummaryText(result, Path.Combine(resultDir, "summary.txt"));

                // 4. no_match_trays 그리기
                DrawNoMatchTrays(result, resultDir);
            }
            catch (Exception ex)
            {
                throw new Exception($"결과 저장 중 오류 발생: {ex.Message}", ex);
            }
        }

        private static void SaveFinalResultExcel(MatchingResult result, string excelPath)
        {
            ExcelPackage.LicenseContext = LicenseContext.NonCommercial;
            using (var package = new ExcelPackage())
            {
                var worksheet = package.Workbook.Worksheets.Add("Results");

                // 헤더 작성
                worksheet.Cells[1, 1].Value = "prescription_name";
                worksheet.Cells[1, 2].Value = "prescription_image";
                int col = 3;
                var trayColMap = new Dictionary<int, int>();
                foreach (var trayIdx in result.TrayIndices.OrderBy(t => t))
                {
                    trayColMap[trayIdx] = col;
                    worksheet.Cells[1, col].Value = $"tray{trayIdx}";
                    col++;
                }

                // 열 너비 설정
                worksheet.Column(1).Width = 25;
                worksheet.Column(2).Width = 12;
                foreach (var trayIdx in result.TrayIndices)
                {
                    worksheet.Column(trayColMap[trayIdx]).Width = 20;
                }

                int row = 2;

                // 조제 알약별 행 작성
                for (int i = 0; i < result.SubfolderNames.Count; i++)
                {
                    var prescName = result.SubfolderNames[i];
                    worksheet.Row(row).Height = 80;

                    // 조제 알약 이름
                    var displayName = result.Numbers[i] > 1 ? $"{prescName}({result.Numbers[i]})" : prescName;
                    worksheet.Cells[row, 1].Value = displayName;

                    // 조제 알약 이미지
                    var prescImgPath = Directory.GetFiles(result.PrescriptionDir, $"{prescName}.*")
                        .FirstOrDefault(f => IsImageFile(f));
                    if (prescImgPath != null && File.Exists(prescImgPath))
                    {
                        try
                        {
                            using (var image = System.Drawing.Image.FromFile(prescImgPath))
                            {
                                var picture = worksheet.Drawings.AddPicture($"presc_{i}", new FileInfo(prescImgPath));
                                picture.SetPosition(row - 1, 0, 1, 0);
                                picture.SetSize(80);
                            }
                        }
                        catch { }
                    }

                    // 각 tray별 매칭 정보
                    if (result.PrescTrayMatches.ContainsKey(prescName))
                    {
                        var trayDict = result.PrescTrayMatches[prescName];
                        foreach (var kvp in trayColMap)
                        {
                            var trayIdx = kvp.Key;
                            var colIdx = kvp.Value;
                            if (trayDict.ContainsKey(trayIdx))
                            {
                                var matches = trayDict[trayIdx];
                                var cellValue = string.Join("\n", matches.Select(m => 
                                    $"{Path.GetFileNameWithoutExtension(m.Item1)} ({m.Item2:F2})"));
                                worksheet.Cells[row, colIdx].Value = cellValue;
                                worksheet.Cells[row, colIdx].Style.WrapText = true;

                                // 크롭 이미지 삽입
                                foreach (var match in matches)
                                {
                                    var cropPath = Path.Combine(result.CroppedDir, match.Item1);
                                    if (File.Exists(cropPath))
                                    {
                                        try
                                        {
                                            var picture = worksheet.Drawings.AddPicture($"crop_{row}_{colIdx}_{match.Item1}", new FileInfo(cropPath));
                                            picture.SetPosition(row - 1, colIdx - 1, 0, 0);
                                            picture.SetSize(60);
                                        }
                                        catch { }
                                    }
                                }
                            }
                        }
                    }

                    row++;
                }

                // No Match 행
                worksheet.Row(row).Height = 80;
                worksheet.Cells[row, 1].Value = "No Match";
                foreach (var kvp in trayColMap)
                {
                    var trayIdx = kvp.Key;
                    var colIdx = kvp.Value;
                    if (result.UnmatchedByTray.ContainsKey(trayIdx) && result.UnmatchedByTray[trayIdx].Count > 0)
                    {
                        var unmatched = result.UnmatchedByTray[trayIdx];
                        var cellValue = string.Join("\n", unmatched.Select(u => 
                            $"{Path.GetFileNameWithoutExtension(u.Item1)} ({u.Item2:F2}, {(string.IsNullOrEmpty(u.Item3) ? "Unknown" : u.Item3)})"));
                        worksheet.Cells[row, colIdx].Value = cellValue;
                        worksheet.Cells[row, colIdx].Style.WrapText = true;

                        // 크롭 이미지 삽입
                        foreach (var item in unmatched)
                        {
                            var cropPath = Path.Combine(result.CroppedDir, item.Item1);
                            if (File.Exists(cropPath))
                            {
                                try
                                {
                                    var picture = worksheet.Drawings.AddPicture($"nomatch_{row}_{colIdx}_{item.Item1}", new FileInfo(cropPath));
                                    picture.SetPosition(row - 1, colIdx - 1, 0, 0);
                                    picture.SetSize(60);
                                }
                                catch { }
                            }
                        }
                    }
                }

                package.SaveAs(new FileInfo(excelPath));
            }
        }

        private static void SaveFinalResultText(MatchingResult result, string txtPath)
        {
            var sb = new StringBuilder();
            sb.AppendLine("=== Final Result ===");
            sb.AppendLine();

            foreach (var trayIdx in result.TrayIndices.OrderBy(t => t))
            {
                sb.AppendLine($"Tray {trayIdx}:");
                foreach (var prescName in result.SubfolderNames)
                {
                    if (result.PrescTrayMatches.ContainsKey(prescName) &&
                        result.PrescTrayMatches[prescName].ContainsKey(trayIdx))
                    {
                        var matches = result.PrescTrayMatches[prescName][trayIdx];
                        sb.AppendLine($"  {prescName}: {matches.Count}개");
                        foreach (var match in matches)
                        {
                            sb.AppendLine($"    - {match.Item1} (확률: {match.Item2:F2})");
                        }
                    }
                }

                if (result.UnmatchedByTray.ContainsKey(trayIdx) && result.UnmatchedByTray[trayIdx].Count > 0)
                {
                    sb.AppendLine($"  No Match: {result.UnmatchedByTray[trayIdx].Count}개");
                    foreach (var unmatched in result.UnmatchedByTray[trayIdx])
                    {
                        sb.AppendLine($"    - {unmatched.Item1} (확률: {unmatched.Item2:F2}, 최적: {unmatched.Item3})");
                    }
                }
                sb.AppendLine();
            }

            File.WriteAllText(txtPath, sb.ToString(), Encoding.UTF8);
        }

        private static void SaveSummaryText(MatchingResult result, string txtPath)
        {
            var sb = new StringBuilder();
            sb.AppendLine("=== Summary ===");
            sb.AppendLine();

            int totalPresc = result.SubfolderNames.Count;
            int totalRequired = result.Numbers.Sum();
            int totalMatched = 0;
            int totalUnmatched = 0;

            foreach (var trayIdx in result.TrayIndices)
            {
                int trayMatched = 0;
                int trayUnmatched = 0;

                foreach (var prescName in result.SubfolderNames)
                {
                    if (result.PrescTrayMatches.ContainsKey(prescName) &&
                        result.PrescTrayMatches[prescName].ContainsKey(trayIdx))
                    {
                        trayMatched += result.PrescTrayMatches[prescName][trayIdx].Count;
                    }
                }

                if (result.UnmatchedByTray.ContainsKey(trayIdx))
                {
                    trayUnmatched = result.UnmatchedByTray[trayIdx].Count;
                }

                totalMatched += trayMatched;
                totalUnmatched += trayUnmatched;

                sb.AppendLine($"Tray {trayIdx}: 매칭 {trayMatched}개, No Match {trayUnmatched}개");
            }

            sb.AppendLine();
            sb.AppendLine($"전체 요구 개수: {totalRequired}개");
            sb.AppendLine($"전체 매칭 개수: {totalMatched}개");
            sb.AppendLine($"전체 No Match: {totalUnmatched}개");
            sb.AppendLine($"매칭률: {(totalMatched * 100.0 / totalRequired):F2}%");

            File.WriteAllText(txtPath, sb.ToString(), Encoding.UTF8);
        }

        private static void DrawNoMatchTrays(MatchingResult result, string resultDir)
        {
            var noMatchDir = Path.Combine(resultDir, "no_match_trays");
            Directory.CreateDirectory(noMatchDir);

            foreach (var trayIdx in result.TrayIndices)
            {
                if (result.UnmatchedByTray.ContainsKey(trayIdx) && result.UnmatchedByTray[trayIdx].Count > 0)
                {
                    // 트레이 이미지 찾기 (예: tray_{trayIdx}.jpg 등)
                    var trayImagePath = FindTrayImage(trayIdx);
                    if (trayImagePath != null && File.Exists(trayImagePath))
                    {
                        using (var bitmap = new Bitmap(trayImagePath))
                        using (var graphics = Graphics.FromImage(bitmap))
                        {
                            var pen = new Pen(Color.Red, 3);
                            var unmatched = result.UnmatchedByTray[trayIdx];

                            foreach (var item in unmatched)
                            {
                                // 크롭 이미지에서 좌표 추출 (파일명에서)
                                var cropName = item.Item1;
                                var rect = ExtractBBoxFromCropName(cropName);
                                if (rect.HasValue)
                                {
                                    graphics.DrawRectangle(pen, rect.Value);
                                }
                            }

                            var outputPath = Path.Combine(noMatchDir, $"tray_{trayIdx}_no_match.png");
                            bitmap.Save(outputPath, ImageFormat.Png);
                        }
                    }
                }
            }
        }

        private static string FindTrayImage(int trayIdx)
        {
            // 트레이 이미지 찾기 로직 (실제 경로에 맞게 수정 필요)
            var possiblePaths = new[]
            {
                $"tray_{trayIdx}.jpg",
                $"tray_{trayIdx}.png",
                $"tray_{trayIdx}.bmp",
                $"img_*_{trayIdx}_*.jpg"
            };
            // 실제 구현 필요
            return null;
        }

        private static Rectangle? ExtractBBoxFromCropName(string cropName)
        {
            // 파일명에서 bbox 추출 (예: img_1_5_(100,200,300,400).bmp)
            var match = System.Text.RegularExpressions.Regex.Match(cropName, @"\((\d+),(\d+),(\d+),(\d+)\)");
            if (match.Success)
            {
                int x1 = int.Parse(match.Groups[1].Value);
                int y1 = int.Parse(match.Groups[2].Value);
                int x2 = int.Parse(match.Groups[3].Value);
                int y2 = int.Parse(match.Groups[4].Value);
                return new Rectangle(x1, y1, x2 - x1, y2 - y1);
            }
            return null;
        }

        private static bool IsImageFile(string path)
        {
            var ext = Path.GetExtension(path).ToLower();
            return ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".bmp";
        }
    }
}
