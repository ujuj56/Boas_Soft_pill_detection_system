
using System;
using System.Windows.Forms;
using System.Runtime.InteropServices;
using System.Text;
using System.IO;
using System.Diagnostics;
using System.Collections.Generic;
using System.Linq;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using OfficeOpenXml;
using OfficeOpenXml.Drawing;
using System.Drawing;
using OpenCvSharp;
using System.Drawing.Imaging;

namespace Test_JHNT
{


    public partial class Form1 : Form
    {
        string emptyTrayPath = @"C:\jhnt\pill_detection_system\empty_tray\empty_tray.bmp";
        string traysDir = @"C:\jhnt\pill_detection_system\trays";
        string datasetsDir = @"C:\jhnt\pill_detection_system\datasets_ORIGIN_checked_bbox";
        string resultDir = @"C:\jhnt\pill_detection_system\results";

        int num_Pills = 5;
        int num_Trays = 12;
        int multi_Pills = 5;

        int[] ranges_Execution = { 1, 5, 6, 10, 11, 12 };  // [1,5], [6,10], [11,12]

        // Tray1 Polygon 방식: 0 = YOLO 마스크, 1 = 색상 기반(prescription) 마스크, 2 = 마스크 없이 복사만(크롭 -> tray1_polygon)
        private const int Tray1PolygonMode = 0;  // 0, 1, 2 중 수동 선택
        // Tray1 Polygon 저장 이름: null/빈 문자열이면 크롭 이름 그대로(*_polygon.bmp 또는 img_1_*.bmp), 값 지정 시 {pill_name}_1.bmp, _2.bmp, ...
        private const string Tray1PolygonPillName = "P1234"; //null;  // 예: "P1234"

        // PillDetectionKernel DLL 함수 선언
        [DllImport("PillDetectionKernel.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int RunPillDetectionMain(int[] ranges, int count);

        [DllImport("PillDetectionKernel.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int RunPrescriptionMasksMain();

        [DllImport("PillDetectionKernel.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int PrewarmPredictionProcessing();

        [DllImport("PillDetectionKernel.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int GenerateTray1PolygonImagesForUI();

        [DllImport("PillDetectionKernel.dll", CallingConvention = CallingConvention.Cdecl)]
        private static extern int GenerateTray1PolygonImagesPrescriptionMaskForUI();

        public Form1()
        {
            InitializeComponent();
        }

        private void AppendOutput(string text)
        {
            if (textBoxOutput.InvokeRequired)
            {
                textBoxOutput.BeginInvoke(new Action(() => textBoxOutput.AppendText(text)));
            }
            else
            {
                textBoxOutput.AppendText(text);
            }
        }

        //tray 이미지 코드
        private void buttonTrayImage_Click(object sender, EventArgs e)
        {
            string tray1Path = Path.Combine(traysDir, "tray1.bmp");
            string tray2Path = Path.Combine(traysDir, "tray2.bmp");

            try
            {
                if (File.Exists(tray1Path))
                {
                    using (var bmp = new Bitmap(tray1Path))
                    {
                        pictureBoxTray1.Image?.Dispose();
                        pictureBoxTray1.Image = new Bitmap(bmp);
                    }
                    AppendOutput($"tray1.bmp 로드 완료: {tray1Path}\r\n");
                }
                else
                {
                    AppendOutput($"오류: tray1.bmp를 찾을 수 없습니다: {tray1Path}\r\n");
                }

                if (File.Exists(tray2Path))
                {
                    using (var bmp = new Bitmap(tray2Path))
                    {
                        pictureBoxTray2.Image?.Dispose();
                        pictureBoxTray2.Image = new Bitmap(bmp);
                    }
                    AppendOutput($"tray2.bmp 로드 완료: {tray2Path}\r\n");
                }
                else
                {
                    AppendOutput($"오류: tray2.bmp를 찾을 수 없습니다: {tray2Path}\r\n");
                }
            }
            catch (Exception ex)
            {
                AppendOutput($"예외 발생: {ex.Message}\r\n");
            }
        }

        //tray1 polygons 코드: 마스크 생성 후 배경(empty_tray 색) 적용 이미지를 DLL에서 생성하고 표시
        private void buttonTray1Polygon_Click(object sender, EventArgs e)
        {
            string tray1PolygonDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "tray1_polygon");
            Stopwatch stopwatch = Stopwatch.StartNew();

            try
            {
                flowLayoutPanelTray1Polygons.Controls.Clear();
                string croppedDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "cropped");

                if (Tray1PolygonMode == 2)
                {
                    // 제3방식: 마스크 없이 크롭 폴더에서 tray1_polygon으로 복사만
                    AppendOutput("\r\nTray1 polygon: 마스크 없이 크롭 이미지 복사 중...\r\n");
                    if (!Directory.Exists(croppedDir))
                    {
                        stopwatch.Stop();
                        AppendOutput($"오류: cropped 폴더를 찾을 수 없습니다: {croppedDir}\r\n");
                        return;
                    }
                    var srcFiles = Directory.GetFiles(croppedDir, "img_1_*.bmp").OrderBy(f => f).ToList();
                    if (srcFiles.Count == 0)
                    {
                        stopwatch.Stop();
                        AppendOutput($"cropped 폴더에 img_1_*.bmp 파일이 없습니다.\r\n");
                        return;
                    }
                    if (!Directory.Exists(tray1PolygonDir))
                        Directory.CreateDirectory(tray1PolygonDir);
                    for (int j = 0; j < srcFiles.Count; j++)
                    {
                        string destName = string.IsNullOrEmpty(Tray1PolygonPillName)
                            ? Path.GetFileName(srcFiles[j])
                            : $"{Tray1PolygonPillName}_{j + 1}.bmp";
                        string destPath = Path.Combine(tray1PolygonDir, destName);
                        try
                        {
                            File.Copy(srcFiles[j], destPath, true);
                        }
                        catch (Exception ex)
                        {
                            AppendOutput($"경고: 복사 실패 {Path.GetFileName(srcFiles[j])} - {ex.Message}\r\n");
                        }
                    }
                }
                else
                {
                    bool usePrescriptionMask = (Tray1PolygonMode == 1);
                    if (usePrescriptionMask)
                        AppendOutput("\r\nTray1 polygon: 색상 기반 마스크(prescription) 생성 및 배경 적용 중...\r\n");
                    else
                        AppendOutput("\r\nTray1 polygon: YOLO 마스크 생성 및 배경 적용 중...\r\n");

                    int ret = usePrescriptionMask
                        ? GenerateTray1PolygonImagesPrescriptionMaskForUI()
                        : GenerateTray1PolygonImagesForUI();
                    if (ret != 0)
                    {
                        stopwatch.Stop();
                        AppendOutput($"오류: Tray1 polygon 생성 실패 (반환값 {ret}). 소요 시간: {stopwatch.ElapsedMilliseconds} ms\r\n");
                        return;
                    }
                }

                if (!Directory.Exists(tray1PolygonDir))
                {
                    stopwatch.Stop();
                    AppendOutput($"오류: tray1_polygon 폴더를 찾을 수 없습니다: {tray1PolygonDir}. 소요 시간: {stopwatch.ElapsedMilliseconds} ms\r\n");
                    return;
                }

                List<string> polygonFiles;
                if (Tray1PolygonMode == 2)
                {
                    polygonFiles = !string.IsNullOrEmpty(Tray1PolygonPillName)
                        ? Directory.GetFiles(tray1PolygonDir, Tray1PolygonPillName + "_*.bmp").OrderBy(f => f).ToList()
                        : Directory.GetFiles(tray1PolygonDir, "img_1_*.bmp").OrderBy(f => f).ToList();
                }
                else
                {
                    var polygonFilesOrdered = Directory.GetFiles(tray1PolygonDir, "*_polygon.bmp").OrderBy(f => f).ToList();
                    if (!string.IsNullOrEmpty(Tray1PolygonPillName) && polygonFilesOrdered.Count > 0)
                    {
                        for (int j = 0; j < polygonFilesOrdered.Count; j++)
                        {
                            string newName = Path.Combine(tray1PolygonDir, $"{Tray1PolygonPillName}_{j + 1}.bmp");
                            try
                            {
                                if (File.Exists(newName)) File.Delete(newName);
                                File.Move(polygonFilesOrdered[j], newName);
                                polygonFilesOrdered[j] = newName;
                            }
                            catch (Exception ex)
                            {
                                AppendOutput($"경고: 이름 변경 실패 - {ex.Message}\r\n");
                            }
                        }
                    }
                    polygonFiles = polygonFilesOrdered;
                }

                if (polygonFiles.Count == 0)
                {
                    stopwatch.Stop();
                    AppendOutput($"tray1_polygon 폴더에 표시할 파일이 없습니다. 소요 시간: {stopwatch.ElapsedMilliseconds} ms\r\n");
                    return;
                }

                AppendOutput($"Tray1 polygon 로드: {polygonFiles.Count}개\r\n");

                // 표시되는 이미지들을 datasets_ORIGIN_checked_bbox\crop_imgs 에 crop_img_1.bmp, crop_img_2.bmp, ... 로 저장
                string saveToDir = Path.Combine(datasetsDir, "crop_img");
                try
                {
                    if (!Directory.Exists(saveToDir))
                        Directory.CreateDirectory(saveToDir);
                    for (int j = 0; j < polygonFiles.Count; j++)
                    {
                        string destPath = Path.Combine(saveToDir, $"crop_img_{j + 1}.bmp");
                        File.Copy(polygonFiles[j], destPath, overwrite: true);
                    }
                    AppendOutput($"표시 이미지 {polygonFiles.Count}개 저장: {saveToDir} (crop_img_1.bmp ~ crop_img_{polygonFiles.Count}.bmp)\r\n");
                }
                catch (Exception ex)
                {
                    AppendOutput($"경고: datasets 폴더 저장 실패 - {ex.Message}\r\n");
                }

                foreach (string fullPath in polygonFiles)
                {
                    string displayName = Path.GetFileNameWithoutExtension(fullPath);
                    var panel = new Panel { Size = new System.Drawing.Size(200, 120), Margin = new Padding(2) };
                    var label = new Label
                    {
                        Text = displayName,
                        AutoSize = false,
                        Size = new System.Drawing.Size(196, 20),
                        Location = new System.Drawing.Point(2, 0),
                        Font = new Font("Consolas", 8F)
                    };
                    var pb = new PictureBox
                    {
                        Size = new System.Drawing.Size(196, 90),
                        Location = new System.Drawing.Point(2, 22),
                        SizeMode = PictureBoxSizeMode.Zoom,
                        BorderStyle = BorderStyle.FixedSingle
                    };

                    try
                    {
                        using (var bmp = new Bitmap(fullPath))
                        {
                            pb.Image = new Bitmap(bmp);
                        }
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"경고: {displayName} 로드 실패 - {ex.Message}\r\n");
                        continue;
                    }

                    panel.Controls.Add(label);
                    panel.Controls.Add(pb);
                    flowLayoutPanelTray1Polygons.Controls.Add(panel);
                }

                stopwatch.Stop();
                double sec = stopwatch.ElapsedMilliseconds / 1000.0;
                AppendOutput($"총 소요 시간: {sec:F2} 초 ({stopwatch.ElapsedMilliseconds} ms) — 알약 {polygonFiles.Count}개 표시 완료\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"예외 발생: {ex.Message}\r\n");
            }
        }

        private async void buttonGenerateTrays_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonGenerateTrays.Enabled = false;
                AppendOutput("\r\n=== 트레이 생성 시작 ===\r\n");

                stopwatch.Start();
                AppendOutput($"시작 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");

                //string emptyTrayPath = @"C:\jhnt\pill_detection_system\empty_tray\empty_tray.bmp";
                //string traysDir = @"C:\jhnt\pill_detection_system\trays";
                //string datasetsDir = @"C:\jhnt\pill_detection_system\datasets_ORIGIN_checked_bbox";

                if (!File.Exists(emptyTrayPath))
                {
                    AppendOutput($"오류: 빈 트레이 이미지를 찾을 수 없습니다: {emptyTrayPath}\r\n");
                    return;
                }

                if (!Directory.Exists(datasetsDir))
                {
                    AppendOutput($"오류: datasets 폴더를 찾을 수 없습니다: {datasetsDir}\r\n");
                    return;
                }

                if (!Directory.Exists(traysDir))
                {
                    Directory.CreateDirectory(traysDir);
                    AppendOutput($"트레이 폴더 생성: {traysDir}\r\n");
                }

                AppendOutput($"빈 트레이 이미지: {emptyTrayPath}\r\n");
                AppendOutput($"알약 소스 폴더: {datasetsDir}\r\n");
                AppendOutput($"트레이 저장 폴더: {traysDir}\r\n");

                int nPills = num_Pills;
                int mTrays = num_Trays;
                int multi = multi_Pills;

                AppendOutput($"\r\n설정값: n_pills={nPills}, m_trays={mTrays}, multi={multi}\r\n");

                await Task.Run(() => GenerateRandomTrays(emptyTrayPath, datasetsDir, traysDir, nPills, mTrays, multi));

                stopwatch.Stop();
                TimeSpan elapsed = stopwatch.Elapsed;

                AppendOutput($"\r\n트레이 생성 완료!\r\n");
                AppendOutput($"총 실행 시간: {elapsed.TotalSeconds:F3} 초\r\n");
                AppendOutput("=== 트레이 생성 완료 ===\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
                if (stopwatch.IsRunning == false)
                {
                    AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalSeconds:F3} 초\r\n");
                }
            }
            finally
            {
                buttonGenerateTrays.Enabled = true;
            }
        }

        private async void buttonPrewarm_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonPrewarm.Enabled = false;
                AppendOutput("\r\n=== 사전 준비 시작 ===\r\n");

                string tray1PolygonDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "tray1_polygon");
                if (Directory.Exists(tray1PolygonDir))
                {
                    int deleted = 0;
                    foreach (string file in Directory.GetFiles(tray1PolygonDir))
                    {
                        try
                        {
                            File.Delete(file);
                            deleted++;
                        }
                        catch (Exception ex)
                        {
                            AppendOutput($"경고: 삭제 실패 {Path.GetFileName(file)} - {ex.Message}\r\n");
                        }
                    }
                    if (deleted > 0)
                        AppendOutput($"tray1_polygon 폴더 내 파일 {deleted}개 삭제됨.\r\n");
                }

                stopwatch.Start();
                AppendOutput($"시작 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");

                int ret = await Task.Run(() => PrewarmPredictionProcessing());

                stopwatch.Stop();
                TimeSpan elapsed = stopwatch.Elapsed;

                if (ret != 0)
                {
                    AppendOutput($"오류 발생: 반환 코드 {ret}\r\n");
                    AppendOutput($"준비 시간: {elapsed.TotalSeconds:F3} 초\r\n");
                    return;
                }

                AppendOutput("사전 준비 완료!\r\n");
                AppendOutput($"준비 시간: {elapsed.TotalSeconds:F3} 초\r\n");
                AppendOutput("=== 사전 준비 완료 ===\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
            }
            finally
            {
                buttonPrewarm.Enabled = true;
            }
        }

        private async void buttonPrescriptionMasks_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonPrescriptionMasks.Enabled = false;
                AppendOutput("\r\n=== 처방 마스크 생성 시작 ===\r\n");

                stopwatch.Start();
                AppendOutput($"시작 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");

                int ret = await Task.Run(() => RunPrescriptionMasksMain());

                stopwatch.Stop();

                if (ret != 0)
                {
                    AppendOutput($"오류 발생: 반환 코드 {ret}\r\n");
                    AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalSeconds:F3} 초 ({stopwatch.ElapsedMilliseconds} ms)\r\n");
                    return;
                }

                AppendOutput("처방 마스크 생성 완료!\r\n");
                AppendOutput($"총 실행 시간: {stopwatch.Elapsed.TotalSeconds:F3} 초\r\n");
                AppendOutput("=== 처방 마스크 생성 완료 ===\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
            }
            finally
            {
                buttonPrescriptionMasks.Enabled = true;
            }
        }

        private void GenerateRandomTrays(string emptyTrayPath, string datasetsDir, string traysDir, int nPills, int mTrays, int multi)
        {
            // 빈 트레이 이미지 로드
            Mat trayImage = Cv2.ImRead(emptyTrayPath);
            if (trayImage.Empty())
            {
                throw new FileNotFoundException($"빈 트레이 이미지 로드 실패: {emptyTrayPath}");
            }

            // 배경색 및 원판 반지름 추출
            var (backgroundColor, diskRadius) = ExtractBackgroundColor(trayImage);
            AppendOutput($"추출된 배경색: ({backgroundColor.Item1}, {backgroundColor.Item2}, {backgroundColor.Item3})\r\n");
            AppendOutput($"원판 반지름: {diskRadius} pixels\r\n");

            // 서브폴더 수집
            List<string> subfolders = new List<string>();
            foreach (var item in Directory.GetDirectories(datasetsDir))
            {
                subfolders.Add(item);
            }
            AppendOutput($"서브폴더 개수: {subfolders.Count}\r\n");

            if (subfolders.Count == 0)
            {
                throw new Exception("사용할 서브폴더가 없습니다.");
            }

            // multi 정규화
            if (multi < 1) multi = 1;
            if (nPills < 1) nPills = 1;
            if (multi > nPills) multi = nPills;

            int uniqueNeeded = nPills - (multi - 1);
            if (subfolders.Count < uniqueNeeded)
            {
                AppendOutput($"경고: 서브폴더 개수({subfolders.Count})가 필요 유니크 개수({uniqueNeeded})보다 적습니다.\r\n");
                uniqueNeeded = subfolders.Count;
                nPills = uniqueNeeded + (multi - 1);
                if (nPills <= 0)
                {
                    throw new Exception("사용할 서브폴더가 없습니다.");
                }
                if (multi > nPills) multi = nPills;
            }

            // 랜덤으로 유니크 서브폴더 선택
            Random random = new Random();
            var selectedUniqueSubfolders = subfolders.OrderBy(x => random.Next()).Take(uniqueNeeded).ToList();

            // 중복시킬 알약 선택
            string dupSubfolder = null;
            if (multi > 1 && selectedUniqueSubfolders.Count > 0)
            {
                dupSubfolder = selectedUniqueSubfolders[random.Next(selectedUniqueSubfolders.Count)];
            }

            // pill_list.txt 저장
            string pillListPath = Path.Combine(traysDir, "pill_list.txt");
            using (var writer = new StreamWriter(pillListPath, false, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false)))
            {
                foreach (var subfolderPath in selectedUniqueSubfolders)
                {
                    string subfolderName = Path.GetFileName(subfolderPath);
                    if (dupSubfolder != null && subfolderPath == dupSubfolder)
                    {
                        writer.WriteLine($"{subfolderName}({multi})");
                    }
                    else
                    {
                        writer.WriteLine(subfolderName);
                    }
                }
            }
            AppendOutput($"\r\n선택된 알약 리스트가 {pillListPath}에 저장되었습니다. (multi={multi})\r\n");

            // m개의 트레이 생성
            for (int trayIdx = 1; trayIdx <= mTrays; trayIdx++)
            {
                AppendOutput($"\r\n트레이 {trayIdx}/{mTrays} 생성 중...\r\n");

                Mat currentTray = trayImage.Clone();

                // 이번 트레이에서 실제로 뿌릴 서브폴더 목록
                List<string> expandedSubfolders = new List<string>(selectedUniqueSubfolders);
                if (dupSubfolder != null)
                {
                    for (int i = 0; i < multi - 1; i++)
                    {
                        expandedSubfolders.Add(dupSubfolder);
                    }
                }

                // 랜덤하게 섞기
                expandedSubfolders = expandedSubfolders.OrderBy(x => random.Next()).ToList();

                List<Mat> pillImages = new List<Mat>();
                List<Mat> pillMasks = new List<Mat>();

                // n-1개는 겹치지 않게 배치할 대상으로 마스크 준비
                int pillsToPlace = Math.Min(nPills - 1, expandedSubfolders.Count);
                for (int i = 0; i < pillsToPlace; i++)
                {
                    string subfolderPath = expandedSubfolders[i];
                    var pillFiles = Directory.GetFiles(subfolderPath)
                        .Where(f => f.ToLower().EndsWith(".bmp") || f.ToLower().EndsWith(".jpg") || 
                                   f.ToLower().EndsWith(".jpeg") || f.ToLower().EndsWith(".png"))
                        .ToList();

                    if (pillFiles.Count == 0) continue;

                    string randomPillFile = pillFiles[random.Next(pillFiles.Count)];
                    Mat pillImage = Cv2.ImRead(randomPillFile);

                    if (pillImage.Empty()) continue;

                    Mat pillMask = CreatePillMask(pillImage, backgroundColor);
                    pillImages.Add(pillImage);
                    pillMasks.Add(pillMask);
                }

                if (pillMasks.Count > 0)
                {
                    try
                    {
                        var positions = PlaceMasksRandomInDisk(diskRadius, pillMasks, random);

                        int trayH = currentTray.Height;
                        int trayW = currentTray.Width;
                        int centerOffsetY = (trayH - 2 * diskRadius) / 2;
                        int centerOffsetX = (trayW - 2 * diskRadius) / 2;

                        // 계산된 위치에 알약들 배치
                        for (int i = 0; i < pillImages.Count && i < positions.Count; i++)
                        {
                            int y0 = positions[i].Item1;
                            int x0 = positions[i].Item2;
                            int actualY = y0 + centerOffsetY;
                            int actualX = x0 + centerOffsetX;

                            Mat pillImage = pillImages[i];
                            Mat pillMask = pillMasks[i];

                            // 그림자 추가
                            AddRealisticShadow(currentTray, pillImage, actualX, actualY, pillMask, random);

                            // 알약 배치
                            int pillH = pillImage.Height;
                            int pillW = pillImage.Width;
                            int endY = Math.Min(actualY + pillH, trayH);
                            int endX = Math.Min(actualX + pillW, trayW);
                            int actualH = endY - actualY;
                            int actualW = endX - actualX;

                            if (actualH > 0 && actualW > 0)
                            {
                                Rect trayRect = new Rect(actualX, actualY, actualW, actualH);
                                Rect pillRect = new Rect(0, 0, actualW, actualH);
                                Rect maskRect = new Rect(0, 0, actualW, actualH);

                                Mat trayRegion = new Mat(currentTray, trayRect);
                                Mat pillPart = new Mat(pillImage, pillRect);
                                Mat maskPart = new Mat(pillMask, maskRect);

                                pillPart.CopyTo(trayRegion, maskPart);
                            }
                        }

                        // 마지막 1개 알약은 기존 알약 중 하나와 겹치게 배치
                        if (nPills > 0 && expandedSubfolders.Count == nPills && positions.Count > 0)
                        {
                            string lastSubfolder = expandedSubfolders[expandedSubfolders.Count - 1];
                            var pillFiles = Directory.GetFiles(lastSubfolder)
                                .Where(f => f.ToLower().EndsWith(".bmp") || f.ToLower().EndsWith(".jpg") || 
                                           f.ToLower().EndsWith(".jpeg") || f.ToLower().EndsWith(".png"))
                                .ToList();

                            if (pillFiles.Count > 0)
                            {
                                string randomPillFile = pillFiles[random.Next(pillFiles.Count)];
                                Mat overlapPill = Cv2.ImRead(randomPillFile);

                                if (!overlapPill.Empty())
                                {
                                    int refIdx = random.Next(positions.Count);
                                    int refY0 = positions[refIdx].Item1;
                                    int refX0 = positions[refIdx].Item2;
                                    int refY = refY0 + centerOffsetY;
                                    int refX = refX0 + centerOffsetX;

                                    double overlapRatio = random.NextDouble() * 0.2 + 0.1; // 0.1 ~ 0.3
                                    int pillH = overlapPill.Height;
                                    int pillW = overlapPill.Width;
                                    int offsetY = (int)(pillH * (1 - overlapRatio) * (random.Next(2) == 0 ? -1 : 1));
                                    int offsetX = (int)(pillW * (1 - overlapRatio) * (random.Next(2) == 0 ? -1 : 1));

                                    int overlapY = Math.Max(0, Math.Min(refY + offsetY, trayH - pillH));
                                    int overlapX = Math.Max(0, Math.Min(refX + offsetX, trayW - pillW));

                                    Mat overlapMask = CreatePillMask(overlapPill, backgroundColor);
                                    AddRealisticShadow(currentTray, overlapPill, overlapX, overlapY, overlapMask, random);

                                    int endY = Math.Min(overlapY + pillH, trayH);
                                    int endX = Math.Min(overlapX + pillW, trayW);
                                    int actualH = endY - overlapY;
                                    int actualW = endX - overlapX;

                                    if (actualH > 0 && actualW > 0)
                                    {
                                        Rect trayRect = new Rect(overlapX, overlapY, actualW, actualH);
                                        Rect pillRect = new Rect(0, 0, actualW, actualH);
                                        Rect maskRect = new Rect(0, 0, actualW, actualH);

                                        Mat trayRegion = new Mat(currentTray, trayRect);
                                        Mat pillPart = new Mat(overlapPill, pillRect);
                                        Mat maskPart = new Mat(overlapMask, maskRect);

                                        pillPart.CopyTo(trayRegion, maskPart);
                                    }

                                    AppendOutput($"  마지막 알약을 {refIdx + 1}번째 알약과 {overlapRatio * 100:F0}% 겹치게 배치\r\n");
                                }
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"  경고: 마스크 배치 실패 - {ex.Message}\r\n");
                    }
                }

                string trayFilename = $"tray{trayIdx}.bmp";
                string trayPath = Path.Combine(traysDir, trayFilename);
                Cv2.ImWrite(trayPath, currentTray);
                AppendOutput($"  트레이 저장 완료: {trayFilename}\r\n");
            }
        }

        private (System.ValueTuple<int, int, int>, int) ExtractBackgroundColor(Mat trayImage)
        {
            int h = trayImage.Height;
            int w = trayImage.Width;
            int centerY = h / 2;
            int centerX = w / 2;

            // 중앙 픽셀의 색상
            Vec3b centerColorBgr = trayImage.At<Vec3b>(centerY, centerX);
            var centerColor = (centerColorBgr.Item2, centerColorBgr.Item1, centerColorBgr.Item0); // BGR -> RGB

            // 반지름을 증가시키며 원판 경계 찾기
            int maxRadius = Math.Min(centerX, centerY);
            int diskRadius = maxRadius;

            for (int r = 10; r < maxRadius; r += 5)
            {
                // 원 둘레상의 점들 샘플링
                var edgeColors = new List<System.ValueTuple<int, int, int>>();
                for (int angleIdx = 0; angleIdx < 36; angleIdx++)
                {
                    double angle = 2.0 * Math.PI * angleIdx / 36.0;
                    int x = (int)(centerX + r * Math.Cos(angle));
                    int y = (int)(centerY + r * Math.Sin(angle));
                    if (x >= 0 && x < w && y >= 0 && y < h)
                    {
                        Vec3b colorBgr = trayImage.At<Vec3b>(y, x);
                        edgeColors.Add((colorBgr.Item2, colorBgr.Item1, colorBgr.Item0)); // BGR -> RGB
                    }
                }

                if (edgeColors.Count > 0)
                {
                    // 가장 많이 나타나는 색상
                    var mostCommon = edgeColors.GroupBy(c => c).OrderByDescending(g => g.Count()).First().Key;

                    // 중앙 색상과 다르면 경계를 찾은 것
                    double colorDiff = Math.Sqrt(
                        Math.Pow(mostCommon.Item1 - centerColor.Item1, 2) +
                        Math.Pow(mostCommon.Item2 - centerColor.Item2, 2) +
                        Math.Pow(mostCommon.Item3 - centerColor.Item3, 2));
                    if (colorDiff > 30)
                    {
                        diskRadius = r - 10;
                        break;
                    }
                }
            }

            return (centerColor, diskRadius);
        }

        private Mat CreatePillMask(Mat pillImage, System.ValueTuple<int, int, int> backgroundColor)
        {
            Mat pillRgb = new Mat();
            Cv2.CvtColor(pillImage, pillRgb, ColorConversionCodes.BGR2RGB);

            int bgR = backgroundColor.Item1;
            int bgG = backgroundColor.Item2;
            int bgB = backgroundColor.Item3;

            // 배경색과의 차이 계산
            Mat[] channels = new Mat[3];
            Cv2.Split(pillRgb, out channels);
            Mat rChannel = channels[0];
            Mat gChannel = channels[1];
            Mat bChannel = channels[2];

            Mat bgDiffR = new Mat();
            Mat bgDiffG = new Mat();
            Mat bgDiffB = new Mat();
            Cv2.Absdiff(rChannel, new Scalar(bgR), bgDiffR);
            Cv2.Absdiff(gChannel, new Scalar(bgG), bgDiffG);
            Cv2.Absdiff(bChannel, new Scalar(bgB), bgDiffB);

            Mat bgDiff = new Mat();
            Cv2.Add(bgDiffR, bgDiffG, bgDiff);
            Cv2.Add(bgDiff, bgDiffB, bgDiff);

            // 검은색, 흰색과의 차이
            Mat blackDiff = new Mat();
            Mat whiteDiff = new Mat();
            Cv2.Absdiff(rChannel, new Scalar(0), blackDiff);
            Mat temp = new Mat();
            Cv2.Absdiff(gChannel, new Scalar(0), temp);
            Cv2.Add(blackDiff, temp, blackDiff);
            Cv2.Absdiff(bChannel, new Scalar(0), temp);
            Cv2.Add(blackDiff, temp, blackDiff);

            Cv2.Absdiff(rChannel, new Scalar(255), whiteDiff);
            Cv2.Absdiff(gChannel, new Scalar(255), temp);
            Cv2.Add(whiteDiff, temp, whiteDiff);
            Cv2.Absdiff(bChannel, new Scalar(255), temp);
            Cv2.Add(whiteDiff, temp, whiteDiff);

            // 배경색, 검은색, 흰색과의 차이가 30보다 큰 영역 찾기
            Mat bgMask = new Mat();
            Mat blackMask = new Mat();
            Mat whiteMask = new Mat();
            Cv2.Threshold(bgDiff, bgMask, 30, 255, ThresholdTypes.Binary);
            Cv2.Threshold(blackDiff, blackMask, 30, 255, ThresholdTypes.Binary);
            Cv2.Threshold(whiteDiff, whiteMask, 30, 255, ThresholdTypes.Binary);

            Mat mask = new Mat();
            Cv2.BitwiseAnd(bgMask, blackMask, mask);
            Cv2.BitwiseAnd(mask, whiteMask, mask);

            Mat pillGray = new Mat();
            Cv2.CvtColor(pillImage, pillGray, ColorConversionCodes.BGR2GRAY);
            Mat darkMask = new Mat();
            Cv2.Threshold(pillGray, darkMask, 30, 255, ThresholdTypes.Binary);
            Cv2.BitwiseAnd(mask, darkMask, mask);

            Mat kernel = Cv2.GetStructuringElement(MorphShapes.Ellipse, new OpenCvSharp.Size(3, 3));
            Mat maskUint8 = new Mat();
            mask.ConvertTo(maskUint8, MatType.CV_8U, 255);

            Cv2.MorphologyEx(maskUint8, maskUint8, MorphTypes.Close, kernel);
            Cv2.MorphologyEx(maskUint8, maskUint8, MorphTypes.Open, kernel);

            // Connected Components
            Mat labels = new Mat();
            Mat stats = new Mat();
            Mat centroids = new Mat();
            int numLabels = Cv2.ConnectedComponentsWithStats(maskUint8, labels, stats, centroids, PixelConnectivity.Connectivity8);

            if (numLabels > 1)
            {
                // 가장 큰 컴포넌트 찾기
                int largestLabel = 1;
                int largestArea = 0;
                for (int i = 1; i < numLabels; i++)
                {
                    int area = stats.At<int>(i, (int)ConnectedComponentsTypes.Area);
                    if (area > largestArea)
                    {
                        largestArea = area;
                        largestLabel = i;
                    }
                }

                // 가장 큰 컴포넌트만 남기기
                Mat largestMask = new Mat();
                Cv2.InRange(labels, new Scalar(largestLabel), new Scalar(largestLabel), largestMask);
                maskUint8 = largestMask;
            }

            // 0보다 큰 값만 남기기
            Mat finalMask = new Mat();
            Cv2.Threshold(maskUint8, finalMask, 0, 255, ThresholdTypes.Binary);
            return finalMask;
        }

        private List<System.ValueTuple<int, int>> PlaceMasksRandomInDisk(int R, List<Mat> masks, Random random, int maxTrialsPerMask = 1000, int margin = 50)
        {
            int H = 2 * R;
            int W = 2 * R;
            Mat diskOccupied = Mat.Zeros(H, W, MatType.CV_8UC1);
            int cy = R;
            int cx = R;

            int Reff = Math.Max(0, R - margin);
            int R2 = Reff * Reff;

            Mat circleMask = Mat.Zeros(H, W, MatType.CV_8UC1);
            for (int y = 0; y < H; y++)
            {
                for (int x = 0; x < W; x++)
                {
                    int dx = x - cx;
                    int dy = y - cy;
                    if (dx * dx + dy * dy <= R2)
                    {
                        circleMask.Set<byte>(y, x, 255);
                    }
                }
            }

            List<System.ValueTuple<int, int>> positions = new List<System.ValueTuple<int, int>>();

            foreach (Mat mask in masks)
            {
                if (mask.Empty()) continue;

                int mh = mask.Height;
                int mw = mask.Width;

                // 마스크의 픽셀 위치 수집
                List<OpenCvSharp.Point> maskPoints = new List<OpenCvSharp.Point>();
                for (int y = 0; y < mh; y++)
                {
                    for (int x = 0; x < mw; x++)
                    {
                        if (mask.At<byte>(y, x) > 0)
                        {
                            maskPoints.Add(new OpenCvSharp.Point(x, y));
                        }
                    }
                }

                if (maskPoints.Count == 0)
                {
                    positions.Add((0, 0));
                    continue;
                }

                bool placed = false;
                for (int trial = 0; trial < maxTrialsPerMask; trial++)
                {
                    int y0 = random.Next(0, Math.Max(1, H - mh + 1));
                    int x0 = random.Next(0, Math.Max(1, W - mw + 1));

                    bool canPlace = true;
                    foreach (OpenCvSharp.Point pt in maskPoints)
                    {
                        int yy = pt.Y + y0;
                        int xx = pt.X + x0;

                        if (yy >= H || xx >= W) { canPlace = false; break; }
                        if (circleMask.At<byte>(yy, xx) == 0) { canPlace = false; break; }
                        if (diskOccupied.At<byte>(yy, xx) != 0) { canPlace = false; break; }
                    }

                    if (canPlace)
                    {
                        foreach (OpenCvSharp.Point pt in maskPoints)
                        {
                            int yy = pt.Y + y0;
                            int xx = pt.X + x0;
                            diskOccupied.Set<byte>(yy, xx, 255);
                        }
                        positions.Add((y0, x0));
                        placed = true;
                        break;
                    }
                }

                if (!placed)
                {
                    throw new Exception($"마스크를 배치할 수 있는 위치를 찾지 못했습니다. (maxTrialsPerMask={maxTrialsPerMask}, R={R})");
                }
            }

            return positions;
        }

        private void AddRealisticShadow(Mat baseImage, Mat pillImage, int x, int y, Mat pillMask, Random random)
        {
            int shadowOffsetX = random.Next(-3, 4);
            int shadowOffsetY = random.Next(-3, 4);
            int shadowBlur = random.Next(2, 6);
            double shadowOpacity = random.NextDouble() * 0.2 + 0.1;

            Mat shadow = Mat.Zeros(baseImage.Size(), MatType.CV_8UC3);
            int sx = x + shadowOffsetX;
            int sy = y + shadowOffsetY;

            try
            {
                int h = pillImage.Height;
                int w = pillImage.Width;

                if (sx >= 0 && sy >= 0 && sx + w < shadow.Width && sy + h < shadow.Height)
                {
                    Rect shadowRect = new Rect(sx, sy, w, h);
                    Mat shadowRegion = new Mat(shadow, shadowRect);
                    Mat maskRegion = new Mat(pillMask, new Rect(0, 0, w, h));

                    shadowRegion.SetTo(new Scalar(50, 50, 50), maskRegion);

                    Mat shadowBlurred = new Mat();
                    Cv2.GaussianBlur(shadow, shadowBlurred, new OpenCvSharp.Size(shadowBlur * 2 + 1, shadowBlur * 2 + 1), 0);
                    Cv2.AddWeighted(baseImage, 1.0, shadowBlurred, shadowOpacity, 0, baseImage);
                }
            }
            catch
            {
                // 무시
            }
        }

        private async void buttonRun_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonRun.Enabled = false;
                textBoxOutput.Clear();
                AppendOutput("처리 시작...\r\n");

                stopwatch.Start();
                AppendOutput($"시작 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");

                AppendOutput("PillDetectionKernel DLL 호출 중...\r\n");

                // 방법 1: 기본 설정으로 실행 (main() 함수와 동일)
                //int ret = await Task.Run(() => RunPillDetectionMain());

                // 방법 2: 커스텀 범위로 실행하려면 아래 코드 사용
                //int[] ranges = { 1, 5, 6, 10, 11, 12 };  // [1,5], [6,10], [11,12]
                int[] ranges = ranges_Execution;
                int ret = await Task.Run(() => RunPillDetectionMain(ranges, 3));  // 3개의 범위

                // 시간 측정 중지
                stopwatch.Stop();

                if (ret != 0)
                {
                    AppendOutput($"오류 발생: 반환 코드 {ret}\r\n");
                    AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalSeconds:F3} 초 ({stopwatch.ElapsedMilliseconds} ms)\r\n");
                    buttonRun.Enabled = true;
                    return;
                }

                AppendOutput("처리 완료!\r\n");
                AppendOutput("\r\n결과 파일:\r\n");
                //string resultDir = @"C:\jhnt\pill_detection_system\results";
                AppendOutput($"- {resultDir}\\final_result.csv\r\n");
                AppendOutput($"- {resultDir}\\final_result.txt\r\n");
                AppendOutput($"- {resultDir}\\summary.txt\r\n");

                // 실행 시간 출력
                TimeSpan elapsed = stopwatch.Elapsed;
                AppendOutput("\r\n=== 실행 시간 ===\r\n");
                AppendOutput($"종료 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");
                AppendOutput($"총 실행 시간: {elapsed.Hours:D2}:{elapsed.Minutes:D2}:{elapsed.Seconds:D2}.{elapsed.Milliseconds:D3}\r\n");
                AppendOutput($"총 실행 시간: {elapsed.TotalSeconds:F3} 초\r\n");
                AppendOutput($"총 실행 시간: {elapsed.TotalMilliseconds:F0} 밀리초\r\n");
                AppendOutput("\r\n=== 처리 완료 ===\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
                if (stopwatch.IsRunning == false)
                {
                    AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalSeconds:F3} 초 ({stopwatch.ElapsedMilliseconds} ms)\r\n");
                }
            }
            finally
            {
                buttonRun.Enabled = true;
            }
        }

        private async void buttonCreateExcel_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonCreateExcel.Enabled = false;
                AppendOutput("\r\n=== Excel 파일 생성 시작 ===\r\n");

                stopwatch.Start();
                AppendOutput($"시작 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");

                //string resultDir = @"E:\JHNT\JHNT_20250914_part\pill_detection_system\results";
                string csvPath = Path.Combine(resultDir, "final_result.csv");
                string excelPath = Path.Combine(resultDir, "final_result_ex.xlsx");
                string croppedDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "cropped");
                string prescriptionDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "prescription");

                if (!File.Exists(csvPath))
                {
                    AppendOutput($"오류: CSV 파일을 찾을 수 없습니다: {csvPath}\r\n");
                    return;
                }

                if (!Directory.Exists(croppedDir))
                {
                    AppendOutput($"오류: cropped 폴더를 찾을 수 없습니다: {croppedDir}\r\n");
                    return;
                }

                if (!Directory.Exists(prescriptionDir))
                {
                    AppendOutput($"오류: prescription 폴더를 찾을 수 없습니다: {prescriptionDir}\r\n");
                    return;
                }

                AppendOutput($"CSV 파일 읽기: {csvPath}\r\n");

                bool excelSuccess = false;
                await Task.Run(() =>
                {
                    // CSV 파일 읽기
                    List<string[]> csvData = ReadCsvFile(csvPath);
                    if (csvData.Count == 0)
                    {
                        AppendOutput("오류: CSV 파일이 비어있습니다.\r\n");
                        return;
                    }

                    AppendOutput($"Excel 파일 생성 중: {excelPath}\r\n");

                    // EPPlus 라이선스 설정 (비상업적 사용)
                    ExcelPackage.LicenseContext = LicenseContext.NonCommercial;

                    // Excel 파일 생성
                    using (var package = new ExcelPackage())
                    {
                        var worksheet = package.Workbook.Worksheets.Add("Results_Transposed");

                    // 열 너비 설정 (Python 코드와 동일)
                    worksheet.Column(1).Width = 25; // prescription_name
                    worksheet.Column(2).Width = 12; // prescription_image
                    
                    // Step 1: CSV의 모든 셀 내용을 정확히 그대로 Excel에 복사
                    int totalRows = csvData.Count;
                    int maxCols = 0;
                    
                    // 최대 열 수 확인
                    for (int row = 0; row < totalRows; row++)
                    {
                        if (csvData[row].Length > maxCols)
                            maxCols = csvData[row].Length;
                    }
                    
                    // 모든 행과 열을 정확히 복사
                    for (int row = 0; row < totalRows; row++)
                    {
                        string[] rowData = csvData[row];
                        
                        // 행 높이 설정 (Python 코드와 동일: 80)
                        worksheet.Row(row + 1).Height = 80;
                        
                        // 모든 열의 텍스트 데이터를 정확히 그대로 복사 (수정 없이)
                        for (int col = 0; col < rowData.Length; col++)
                        {
                            var cell = worksheet.Cells[row + 1, col + 1];
                            
                            // CSV의 셀 값을 그대로 복사 (어떤 수정도 하지 않음)
                            cell.Value = rowData[col];
                            
                            // 셀 형식 설정
                            cell.Style.WrapText = true;
                            cell.Style.VerticalAlignment = OfficeOpenXml.Style.ExcelVerticalAlignment.Top;
                            
                            // 헤더 행은 굵게
                            if (row == 0)
                            {
                                cell.Style.Font.Bold = true;
                            }
                            
                            // tray 열들의 너비 설정 (Python 코드와 동일: 30)
                            if (col >= 2)
                            {
                                worksheet.Column(col + 1).Width = 30;
                            }
                        }
                        
                        // 빈 열도 처리 (행마다 열 수가 다를 수 있음)
                        for (int col = rowData.Length; col < maxCols; col++)
                        {
                            var cell = worksheet.Cells[row + 1, col + 1];
                            cell.Value = "";
                            cell.Style.WrapText = true;
                            cell.Style.VerticalAlignment = OfficeOpenXml.Style.ExcelVerticalAlignment.Top;
                            
                            if (row == 0)
                            {
                                cell.Style.Font.Bold = true;
                            }
                            
                            if (col >= 2)
                            {
                                worksheet.Column(col + 1).Width = 30;
                            }
                        }
                    }
                    
                    AppendOutput($"CSV 데이터 복사 완료: {totalRows}행, {maxCols}열\r\n");

                    // Step 2: 2열(prescription_image)에 prescription 폴더에서 이미지 삽입
                    int prescriptionImageCount = 0;
                    for (int row = 1; row < totalRows; row++) // 헤더 제외
                    {
                        string[] rowData = csvData[row];
                        if (rowData.Length < 1) continue;
                        
                        // 1열에서 prescription 이름 추출 (괄호, 작은따옴표 제거)
                        string prescriptionName = rowData[0];
                        string cleanPrescriptionName = ExtractPrescriptionName(prescriptionName);
                        
                        if (!string.IsNullOrEmpty(cleanPrescriptionName))
                        {
                            string prescriptionImagePath = FindPrescriptionImage(prescriptionDir, cleanPrescriptionName);
                            if (!string.IsNullOrEmpty(prescriptionImagePath) && File.Exists(prescriptionImagePath))
                            {
                                try
                                {
                                    using (var originalImage = new Bitmap(prescriptionImagePath))
                                    {
                                        int originalWidth = originalImage.Width;
                                        int originalHeight = originalImage.Height;
                                        int newWidth = originalWidth / 4;
                                        int newHeight = originalHeight / 4;
                                        
                                        FileInfo imageFile = new FileInfo(prescriptionImagePath);
                                        var excelImage = worksheet.Drawings.AddPicture(
                                            $"presc_img_{row}",
                                            imageFile);
                                        
                                        excelImage.SetPosition(row, 0, 1, 0);
                                        excelImage.SetSize(newWidth, newHeight);
                                        prescriptionImageCount++;
                                    }
                                }
                                catch (Exception ex)
                                {
                                    AppendOutput($"경고: prescription 이미지 삽입 실패 ({cleanPrescriptionName}): {ex.Message}\r\n");
                                }
                            }
                            else
                            {
                                AppendOutput($"경고: prescription 이미지를 찾을 수 없음: {cleanPrescriptionName}\r\n");
                            }
                        }
                    }
                    AppendOutput($"Prescription 이미지 삽입 완료: {prescriptionImageCount}개\r\n");

                    // Step 3: 3열부터(tray 열들)에 cropped 폴더에서 이미지 삽입 (1/4 크기)
                    int totalCropImages = 0;
                    int missingCropImages = 0;
                    for (int row = 1; row < totalRows; row++) // 헤더 제외
                    {
                        string[] rowData = csvData[row];
                        
                        // 3열부터 처리 (col = 2는 3열)
                        for (int col = 2; col < rowData.Length; col++)
                        {
                            string cellValue = rowData[col];
                            if (string.IsNullOrWhiteSpace(cellValue))
                                continue;

                            // 셀에서 크롭 이미지 이름 추출
                            List<string> cropNames = ExtractCropNames(cellValue);
                            
                            if (cropNames.Count > 0)
                            {
                                var cell = worksheet.Cells[row + 1, col + 1];
                                int imageIndex = 0;
                                int totalImageHeight = 0;
                                int imageSpacing = 5;
                                
                                foreach (string cropName in cropNames)
                                {
                                    string imagePath = FindCropImage(croppedDir, cropName);
                                    if (!string.IsNullOrEmpty(imagePath) && File.Exists(imagePath))
                                    {
                                        try
                                        {
                                            using (var originalImage = new Bitmap(imagePath))
                                            {
                                                int originalWidth = originalImage.Width;
                                                int originalHeight = originalImage.Height;
                                                int newWidth = originalWidth / 4;
                                                int newHeight = originalHeight / 4;
                                                
                                                FileInfo imageFile = new FileInfo(imagePath);
                                                var excelImage = worksheet.Drawings.AddPicture(
                                                    $"img_{row}_{col}_{imageIndex}",
                                                    imageFile);

                                                // 이미지를 셀 위쪽에 세로로 배치
                                                excelImage.SetPosition(row, totalImageHeight, col, 0);
                                                excelImage.SetSize(newWidth, newHeight);
                                                
                                                totalImageHeight += newHeight + imageSpacing;
                                                imageIndex++;
                                                totalCropImages++;
                                            }
                                        }
                                        catch (Exception ex)
                                        {
                                            AppendOutput($"경고: 이미지 삽입 실패 ({cropName}): {ex.Message}\r\n");
                                            missingCropImages++;
                                        }
                                    }
                                    else
                                    {
                                        AppendOutput($"경고: cropped 이미지를 찾을 수 없음: {cropName}\r\n");
                                        missingCropImages++;
                                    }
                                }
                                
                                // 셀 높이를 이미지 높이에 맞게 조정
                                if (totalImageHeight > 0)
                                {
                                    worksheet.Row(row + 1).Height = Math.Max(worksheet.Row(row + 1).Height, totalImageHeight + 40);
                                }
                            }
                        }

                        // 진행 상황 표시
                        if (row % 10 == 0)
                        {
                            AppendOutput($"진행 중: {row}/{totalRows - 1} 행 처리 완료\r\n");
                        }
                    }
                    AppendOutput($"Cropped 이미지 삽입 완료: {totalCropImages}개, 누락: {missingCropImages}개\r\n");

                    // 파일 저장
                    FileInfo excelFile = new FileInfo(excelPath);
                    package.SaveAs(excelFile);//<-
                    excelSuccess = true;
                    }
                });

                stopwatch.Stop();
                TimeSpan elapsed = stopwatch.Elapsed;

                if (!excelSuccess)
                {
                    return;
                }

                AppendOutput($"\r\nExcel 파일 생성 완료!\r\n");
                AppendOutput($"파일 경로: {excelPath}\r\n");
                AppendOutput($"총 실행 시간: {elapsed.TotalSeconds:F3} 초\r\n");
                AppendOutput("=== Excel 파일 생성 완료 ===\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
                if (stopwatch.IsRunning == false)
                {
                    AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalSeconds:F3} 초\r\n");
                }
            }
            finally
            {
                buttonCreateExcel.Enabled = true;
            }
        }

        private List<string[]> ReadCsvFile(string csvPath)
        {
            List<string[]> data = new List<string[]>();
            
            using (var reader = new StreamReader(csvPath, Encoding.UTF8))
            {
                string? line;
                StringBuilder recordBuilder = new StringBuilder();
                int logicalLineNumber = 0;

                while ((line = reader.ReadLine()) != null)
                {
                    // ReadLine()은 물리적 한 줄만 읽음.
                    // 셀 안에 줄바꿈이 있는 경우, 따옴표가 닫힐 때까지 계속 이어 붙인다.
                    if (recordBuilder.Length > 0)
                    {
                        // 이전 줄의 연속
                        recordBuilder.Append("\n");
                        recordBuilder.Append(line);
                    }
                    else
                    {
                        // 새 레코드 시작
                        recordBuilder.Append(line);
                    }

                    // 따옴표 개수가 짝수면 하나의 CSV 레코드로 간주
                    if (!IsCompleteCsvRecord(recordBuilder.ToString()))
                    {
                        // 아직 레코드가 끝나지 않았으므로 다음 줄을 계속 읽는다.
                        continue;
                    }

                    string record = recordBuilder.ToString();
                    recordBuilder.Clear();

                    if (string.IsNullOrWhiteSpace(record))
                        continue;

                    logicalLineNumber++;

                    // CSV 파싱 (따옴표/쉼표/내부 줄바꿈 처리)
                    List<string> fields = ParseCsvLine(record);
                    data.Add(fields.ToArray());
                }

                // 파일 끝에서 따옴표가 안 맞는 레코드가 남아 있는 경우
                if (recordBuilder.Length > 0)
                {
                    string record = recordBuilder.ToString();
                    if (!string.IsNullOrWhiteSpace(record))
                    {
                        List<string> fields = ParseCsvLine(record);
                        data.Add(fields.ToArray());
                    }
                }
            }

            return data;
        }

        // 한 문자열이 "완전한" CSV 레코드인지 확인 (따옴표 개수가 짝수인지 검사)
        // 내부 줄바꿈이 포함된 셀을 지원하기 위해 사용.
        private bool IsCompleteCsvRecord(string text)
        {
            int quoteCount = 0;
            for (int i = 0; i < text.Length; i++)
            {
                if (text[i] == '"')
                {
                    // 이스케이프된 따옴표 ("")는 두 개가 하나의 실제 따옴표이므로
                    // 개수 측면에서는 그대로 2개로 세도 짝수/홀수 판별에는 문제가 없다.
                    quoteCount++;
                }
            }

            // 따옴표 개수가 짝수면 레코드가 끝난 것으로 간주
            return (quoteCount % 2) == 0;
        }

        private List<string> ParseCsvLine(string line)
        {
            List<string> fields = new List<string>();
            bool inQuotes = false;
            StringBuilder currentField = new StringBuilder();

            for (int i = 0; i < line.Length; i++)
            {
                char c = line[i];

                if (c == '"')
                {
                    if (inQuotes && i + 1 < line.Length && line[i + 1] == '"')
                    {
                        // 이스케이프된 따옴표
                        currentField.Append('"');
                        i++; // 다음 따옴표 건너뛰기
                    }
                    else
                    {
                        // 따옴표 시작/끝
                        inQuotes = !inQuotes;
                    }
                }
                else if (c == ',' && !inQuotes)
                {
                    // 필드 구분자
                    fields.Add(currentField.ToString());
                    currentField.Clear();
                }
                else
                {
                    currentField.Append(c);
                }
            }

            // 마지막 필드 추가
            fields.Add(currentField.ToString());

            return fields;
        }

        private string ExtractPrescriptionName(string prescriptionName)
        {
            // prescription_name에서 괄호, 줄바꿈, 작은따옴표, 큰따옴표, 백틱 제거
            // 예: "'3400931813736" -> "3400931813736"
            // 예: "'3400935329219(2)" -> "3400935329219"
            // 예: "`12345" -> "12345"
            // 예: "a(2)" -> "a", "a\n이미지이름" -> "a"
            
            if (string.IsNullOrEmpty(prescriptionName))
                return "";
            
            // 줄바꿈으로 분리하여 첫 번째 부분만 사용
            string[] nameParts = prescriptionName.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
            string cleanName = nameParts.Length > 0 ? nameParts[0].Trim() : prescriptionName.Trim();
            
            // 작은따옴표(') 제거
            cleanName = cleanName.TrimStart('\'');
            
            // 백틱(`) 제거
            cleanName = cleanName.TrimStart('`');
            
            // 큰따옴표(") 제거
            cleanName = cleanName.Trim('"');
            
            // 괄호와 그 이후 제거
            int parenIndex = cleanName.IndexOf('(');
            if (parenIndex > 0)
            {
                cleanName = cleanName.Substring(0, parenIndex).Trim();
            }
            
            return cleanName;
        }

        private List<string> ExtractCropNames(string cellValue)
        {
            List<string> cropNames = new List<string>();
            
            // 셀 값 형식: "img_1_10 (0.70)" 또는 "img_1_10 (0.70, best_name)"
            // 여러 개는 줄바꿈으로 구분
            string[] lines = cellValue.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
            
            foreach (string line in lines)
            {
                string trimmedLine = line.Trim();
                if (string.IsNullOrEmpty(trimmedLine))
                    continue;
                
                // "crop_prefix (prob)" 또는 "crop_prefix (prob, best_name)" 형식에서 crop_prefix 추출
                // 정규식: 시작부터 첫 번째 '(' 전까지 (공백 제거)
                Match match = Regex.Match(trimmedLine, @"^([^(]+)");
                if (match.Success)
                {
                    string cropPrefix = match.Groups[1].Value.Trim();
                    if (!string.IsNullOrEmpty(cropPrefix))
                    {
                        cropNames.Add(cropPrefix);
                    }
                }
            }

            return cropNames;
        }

        private string FindPrescriptionImage(string prescriptionDir, string prescriptionName)
        {
            if (string.IsNullOrEmpty(prescriptionName))
                return null;
            
            // prescriptionName으로 시작하는 이미지 파일 찾기
            // 가능한 확장자: .bmp, .jpg, .jpeg, .png
            string[] extensions = { ".bmp", ".jpg", ".jpeg", ".png" };

            // 먼저 정확히 일치하는 파일 검색
            foreach (string ext in extensions)
            {
                string fullPath = Path.Combine(prescriptionDir, prescriptionName + ext);
                if (File.Exists(fullPath))
                {
                    return fullPath;
                }
            }

            // 정확히 일치하지 않으면 부분 일치 검색
            try
            {
                // prescriptionName으로 시작하는 모든 파일 검색
                var allFiles = Directory.GetFiles(prescriptionDir);
                foreach (string file in allFiles)
                {
                    string fileName = Path.GetFileNameWithoutExtension(file);
                    string ext = Path.GetExtension(file).ToLower();
                    
                    // 확장자가 이미지인지 확인
                    if (!extensions.Contains(ext))
                        continue;
                    
                    // 파일명이 prescriptionName으로 시작하거나 정확히 일치하는지 확인
                    if (fileName.Equals(prescriptionName, StringComparison.OrdinalIgnoreCase) ||
                        fileName.StartsWith(prescriptionName, StringComparison.OrdinalIgnoreCase))
                    {
                        return file;
                    }
                }
            }
            catch (Exception ex)
            {
                // 파일 검색 실패 시 무시
                AppendOutput($"경고: prescription 이미지 검색 중 오류 ({prescriptionName}): {ex.Message}\r\n");
            }

            return null;
        }

        private string FindCropImage(string croppedDir, string cropPrefix)
        {
            if (string.IsNullOrEmpty(cropPrefix))
                return null;
            
            // cropPrefix로 시작하는 이미지 파일 찾기
            // 가능한 확장자: .bmp, .jpg, .jpeg, .png
            string[] extensions = { ".bmp", ".jpg", ".jpeg", ".png" };

            // 먼저 정확히 일치하는 파일 검색
            foreach (string ext in extensions)
            {
                string fullPath = Path.Combine(croppedDir, cropPrefix + ext);
                if (File.Exists(fullPath))
                {
                    return fullPath;
                }
            }

            // 정확히 일치하지 않으면 부분 일치 검색
            try
            {
                // cropPrefix로 시작하는 모든 파일 검색
                var allFiles = Directory.GetFiles(croppedDir);
                
                // 정확히 일치하는 파일 우선 검색
                foreach (string file in allFiles)
                {
                    string fileName = Path.GetFileNameWithoutExtension(file);
                    string ext = Path.GetExtension(file).ToLower();
                    
                    // 확장자가 이미지인지 확인
                    if (!extensions.Contains(ext))
                        continue;
                    
                    // 파일명이 정확히 일치하는지 확인
                    if (fileName.Equals(cropPrefix, StringComparison.OrdinalIgnoreCase))
                    {
                        return file;
                    }
                }
                
                // 정확히 일치하지 않으면 cropPrefix로 시작하는 파일 검색
                foreach (string file in allFiles)
                {
                    string fileName = Path.GetFileNameWithoutExtension(file);
                    string ext = Path.GetExtension(file).ToLower();
                    
                    // 확장자가 이미지인지 확인
                    if (!extensions.Contains(ext))
                        continue;
                    
                    // 파일명이 cropPrefix로 시작하는지 확인
                    if (fileName.StartsWith(cropPrefix, StringComparison.OrdinalIgnoreCase))
                    {
                        // 예: cropPrefix가 "img_1_10"이고 파일이 "img_1_10_(292,640,399,747)"인 경우
                        // 다음 문자가 '_' 또는 '.' 또는 파일 끝이면 매칭
                        if (fileName.Length == cropPrefix.Length || 
                            fileName[cropPrefix.Length] == '_' || 
                            fileName[cropPrefix.Length] == '.')
                        {
                            return file;
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                // 파일 검색 실패 시 무시
                AppendOutput($"경고: cropped 이미지 검색 중 오류 ({cropPrefix}): {ex.Message}\r\n");
            }

            return null;
        }


        private async void buttonRegisterPill_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonRegisterPill.Enabled = false;
                AppendOutput("\r\n=== 약 등록 시작 ===\r\n");

                // 1. BMP 이미지 선택
                using (OpenFileDialog openFileDialog = new OpenFileDialog())
                {
                    openFileDialog.Title = "등록할 약 이미지 선택";
                    openFileDialog.Filter = "BMP 파일 (*.bmp)|*.bmp";
                    openFileDialog.Multiselect = false;

                    if (openFileDialog.ShowDialog() != DialogResult.OK)
                    {
                        AppendOutput("약 등록이 취소되었습니다.\r\n");
                        return;
                    }

                    string selectedImagePath = openFileDialog.FileName;
                    AppendOutput($"선택된 이미지: {selectedImagePath}\r\n");

                    // 2. trays 폴더에 tray1.bmp로 복사
                    string tray1Path = Path.Combine(traysDir, "tray1.bmp");
                    try
                    {
                        File.Copy(selectedImagePath, tray1Path, overwrite: true);
                        AppendOutput($"이미지 복사 완료: {tray1Path}\r\n");
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"오류: 이미지 복사 실패 - {ex.Message}\r\n");
                        return;
                    }

                    stopwatch.Start();

                    // 3. 사전 준비 실행
                    AppendOutput("\r\n[1/3] 사전 준비 중...\r\n");
                    int retPrewarm = await Task.Run(() => PrewarmPredictionProcessing());
                    if (retPrewarm != 0)
                    {
                        AppendOutput($"오류: 사전 준비 실패 (반환 코드 {retPrewarm})\r\n");
                        return;
                    }
                    AppendOutput("사전 준비 완료!\r\n");

                    // 4. 처방 마스크 생성
                    AppendOutput("\r\n[2/3] 처방 마스크 생성 중...\r\n");
                    int retMask = await Task.Run(() => RunPrescriptionMasksMain());
                    if (retMask != 0)
                    {
                        AppendOutput($"오류: 처방 마스크 생성 실패 (반환 코드 {retMask})\r\n");
                        return;
                    }
                    AppendOutput("처방 마스크 생성 완료!\r\n");

                    // 5. 추론 실행 (tray1만 처리)
                    AppendOutput("\r\n[3/3] 추론 실행 중...\r\n");
                    int[] singleTrayRange = { 1, 1 }; // tray1만 처리
                    int retRun = await Task.Run(() => RunPillDetectionMain(singleTrayRange, 1));
                    if (retRun != 0)
                    {
                        AppendOutput($"오류: 추론 실행 실패 (반환 코드 {retRun})\r\n");
                        return;
                    }
                    AppendOutput("추론 실행 완료!\r\n");

                    stopwatch.Stop();
                    AppendOutput($"\r\n자동 처리 완료! (소요 시간: {stopwatch.Elapsed.TotalSeconds:F2}초)\r\n");

                    // 6. 크롭된 이미지 확인
                    string croppedDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "cropped");
                    if (!Directory.Exists(croppedDir))
                    {
                        AppendOutput($"오류: cropped 폴더를 찾을 수 없습니다: {croppedDir}\r\n");
                        return;
                    }

                    var croppedFiles = Directory.GetFiles(croppedDir, "img_1_*.bmp").OrderBy(f => f).ToList();
                    if (croppedFiles.Count == 0)
                    {
                        AppendOutput("오류: 크롭된 이미지가 없습니다.\r\n");
                        return;
                    }

                    AppendOutput($"\r\n크롭된 이미지 {croppedFiles.Count}개 발견!\r\n");

                    // 7. 약 이름 입력 다이얼로그
                    string pillName = ShowPillNameInputDialog(croppedFiles.Count);
                    if (string.IsNullOrWhiteSpace(pillName))
                    {
                        AppendOutput("약 이름이 입력되지 않았습니다. 등록이 취소되었습니다.\r\n");
                        return;
                    }

                    // 8. datasets 폴더에 저장
                    string pillFolderPath = Path.Combine(datasetsDir, pillName);
                    if (!Directory.Exists(pillFolderPath))
                    {
                        Directory.CreateDirectory(pillFolderPath);
                        AppendOutput($"폴더 생성: {pillFolderPath}\r\n");
                    }

                    int savedCount = 0;
                    for (int i = 0; i < croppedFiles.Count; i++)
                    {
                        string destFileName = $"{pillName}_{i}.bmp";
                        string destPath = Path.Combine(pillFolderPath, destFileName);
                        try
                        {
                            File.Copy(croppedFiles[i], destPath, overwrite: true);
                            savedCount++;
                        }
                        catch (Exception ex)
                        {
                            AppendOutput($"경고: {destFileName} 저장 실패 - {ex.Message}\r\n");
                        }
                    }

                    AppendOutput($"\r\n약 등록 완료!\r\n");
                    AppendOutput($"약 이름: {pillName}\r\n");
                    AppendOutput($"저장 경로: {pillFolderPath}\r\n");
                    AppendOutput($"저장된 이미지: {savedCount}개\r\n");
                    AppendOutput($"약 등록 완료\r\n");

                    // 9. 등록된 이미지 미리보기 표시 (선택사항)
                    ShowRegisteredPillsPreview(croppedFiles, pillName);
                }
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
            }
            finally
            {
                buttonRegisterPill.Enabled = true;
            }
        }


        // 약 이름 입력 다이얼로그
        private string ShowPillNameInputDialog(int imageCount)
        {
            using (Form inputForm = new Form())
            {
                inputForm.Text = "약 이름 입력";
                inputForm.Width = 400;
                inputForm.Height = 180;
                inputForm.FormBorderStyle = FormBorderStyle.FixedDialog;
                inputForm.StartPosition = FormStartPosition.CenterParent;
                inputForm.MaximizeBox = false;
                inputForm.MinimizeBox = false;

                Label label = new Label
                {
                    Text = $"크롭된 이미지 {imageCount}개가 발견되었습니다.\r\n등록할 약의 이름을 입력하세요:",
                    Left = 20,
                    Top = 20,
                    Width = 350,
                    Height = 40
                };

                TextBox textBox = new TextBox
                {
                    Left = 20,
                    Top = 70,
                    Width = 340,
                    MaxLength = 100
                };

                Button buttonOk = new Button
                {
                    Text = "확인",
                    Left = 200,
                    Width = 80,
                    Top = 105,
                    DialogResult = DialogResult.OK
                };

                Button buttonCancel = new Button
                {
                    Text = "취소",
                    Left = 285,
                    Width = 75,
                    Top = 105,
                    DialogResult = DialogResult.Cancel
                };

                buttonOk.Click += (sender, e) => { inputForm.Close(); };
                buttonCancel.Click += (sender, e) => { inputForm.Close(); };

                inputForm.Controls.Add(label);
                inputForm.Controls.Add(textBox);
                inputForm.Controls.Add(buttonOk);
                inputForm.Controls.Add(buttonCancel);
                inputForm.AcceptButton = buttonOk;
                inputForm.CancelButton = buttonCancel;

                DialogResult result = inputForm.ShowDialog();
                return result == DialogResult.OK ? textBox.Text.Trim() : null;
            }
        }

        // 등록된 약 미리보기 표시 (선택사항)
        private void ShowRegisteredPillsPreview(List<string> imagePaths, string pillName)
        {
            try
            {
                flowLayoutPanelTray1Polygons.Controls.Clear();
                AppendOutput($"\r\n등록된 약 '{pillName}' 미리보기 표시 중...\r\n");

                foreach (string imagePath in imagePaths)
                {
                    string displayName = Path.GetFileNameWithoutExtension(imagePath);
                    var panel = new Panel { Size = new System.Drawing.Size(200, 120), Margin = new Padding(2) };
                    var label = new Label
                    {
                        Text = $"{pillName}",
                        AutoSize = false,
                        Size = new System.Drawing.Size(196, 20),
                        Location = new System.Drawing.Point(2, 0),
                        Font = new Font("Consolas", 8F, FontStyle.Bold),
                        ForeColor = Color.DarkGreen
                    };
                    var pb = new PictureBox
                    {
                        Size = new System.Drawing.Size(196, 90),
                        Location = new System.Drawing.Point(2, 22),
                        SizeMode = PictureBoxSizeMode.Zoom,
                        BorderStyle = BorderStyle.FixedSingle
                    };

                    try
                    {
                        using (var bmp = new Bitmap(imagePath))
                        {
                            pb.Image = new Bitmap(bmp);
                        }
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"경고: {displayName} 로드 실패 - {ex.Message}\r\n");
                        continue;
                    }

                    panel.Controls.Add(label);
                    panel.Controls.Add(pb);
                    flowLayoutPanelTray1Polygons.Controls.Add(panel);
                }

                AppendOutput($"미리보기 표시 완료: {imagePaths.Count}개\r\n");
            }
            catch (Exception ex)
            {
                AppendOutput($"경고: 미리보기 표시 중 오류 - {ex.Message}\r\n");
            }
        }

        private async void buttonDetectPill_Click(object sender, EventArgs e)
        {
            Stopwatch stopwatch = new Stopwatch();

            try
            {
                buttonDetectPill.Enabled = false;
                AppendOutput("약 검출 프로세스 시작\r\n");

                // 1. BMP 이미지 선택
                using (OpenFileDialog openFileDialog = new OpenFileDialog())
                {
                    openFileDialog.Title = "검출할 약 이미지 선택";
                    openFileDialog.Filter = "BMP 파일 (*.bmp)|*.bmp|모든 파일 (*.*)|*.*";
                    openFileDialog.FilterIndex = 1;
                    openFileDialog.Multiselect = false;
                    openFileDialog.InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.MyPictures);

                    if (openFileDialog.ShowDialog() != DialogResult.OK)
                    {
                        AppendOutput("약 검출이 취소되었습니다.\r\n");
                        return;
                    }

                    string selectedImagePath = openFileDialog.FileName;
                    AppendOutput($"선택된 이미지: {Path.GetFileName(selectedImagePath)}\r\n");
                    AppendOutput($"경로: {selectedImagePath}\r\n");

                    // 2. 이미지를 tray1.bmp로 복사
                    string tray1Path = Path.Combine(traysDir, "tray1.bmp");
                    try
                    {
                        // 원본 이미지 정보 표시
                        using (var img = Image.FromFile(selectedImagePath))
                        {
                            AppendOutput($"이미지 크기: {img.Width} x {img.Height} px\r\n");
                            AppendOutput($"파일 크기: {new FileInfo(selectedImagePath).Length / 1024} KB\r\n");
                        }

                        File.Copy(selectedImagePath, tray1Path, overwrite: true);
                        AppendOutput($"이미지 준비 완료\r\n");

                        // 선택한 이미지를 pictureBoxTray1에 표시
                        DisplaySelectedImage(selectedImagePath);
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"오류: 이미지 복사 실패 - {ex.Message}\r\n");
                        return;
                    }

                    stopwatch.Start();
                    AppendOutput($"\r\n처리 시작 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");

                    // 3. 자동 파이프라인 실행
                    bool success = await ExecuteDetectionPipeline();

                    stopwatch.Stop();

                    if (!success)
                    {
                        AppendOutput($"\r\n약 검출 실패\r\n");
                        AppendOutput($"소요 시간: {stopwatch.Elapsed.TotalSeconds:F2}초\r\n");
                        return;
                    }

                    // 4. 결과 표시
                    await DisplayDetectionResults();

                    AppendOutput($"약 검출 프로세스 완료!\r\n");
                    AppendOutput($"총 소요 시간: {stopwatch.Elapsed.TotalSeconds:F2}초 ({stopwatch.ElapsedMilliseconds}ms)\r\n");
                    AppendOutput($"종료 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");
                }
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
                if (!stopwatch.IsRunning)
                {
                    AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalSeconds:F2}초\r\n");
                }
            }
            finally
            {
                buttonDetectPill.Enabled = true;
            }
        }

        private async Task<bool> ExecuteDetectionPipeline()
        {
            try
            {
                // Step 1: 사전 준비 (Prewarm)
                AppendOutput("[1/3] 사전 준비 실행 중...\r\n");

                var swPrewarm = Stopwatch.StartNew();
                int retPrewarm = await Task.Run(() => PrewarmPredictionProcessing());
                swPrewarm.Stop();

                if (retPrewarm != 0)
                {
                    AppendOutput($"사전 준비 실패 (반환 코드: {retPrewarm})\r\n");
                    AppendOutput($"소요 시간: {swPrewarm.Elapsed.TotalSeconds:F2}초\r\n");
                    return false;
                }
                AppendOutput($"사전 준비 완료! (소요 시간: {swPrewarm.Elapsed.TotalSeconds:F2}초)\r\n");

                // Step 2: 처방 마스크 생성
                AppendOutput("[2/3] 처방 마스크 생성 중...\r\n");

                var swMask = Stopwatch.StartNew();
                int retMask = await Task.Run(() => RunPrescriptionMasksMain());
                swMask.Stop();

                if (retMask != 0)
                {
                    AppendOutput($"처방 마스크 생성 실패 (반환 코드: {retMask})\r\n");
                    AppendOutput($"소요 시간: {swMask.Elapsed.TotalSeconds:F2}초\r\n");
                    return false;
                }
                AppendOutput($"처방 마스크 생성 완료! (소요 시간: {swMask.Elapsed.TotalSeconds:F2}초)\r\n");

                // Step 3: 추론 실행 (tray1만 처리)
                AppendOutput("[3/3] 추론 실행 중...\r\n");

                var swRun = Stopwatch.StartNew();
                int[] singleTrayRange = { 1, 1 }; // tray1만 처리
                int retRun = await Task.Run(() => RunPillDetectionMain(singleTrayRange, 1));
                swRun.Stop();

                if (retRun != 0)
                {
                    AppendOutput($"추론 실행 실패 (반환 코드: {retRun})\r\n");
                    AppendOutput($"소요 시간: {swRun.Elapsed.TotalSeconds:F2}초\r\n");
                    return false;
                }
                AppendOutput($"추론 실행 완료! (소요 시간: {swRun.Elapsed.TotalSeconds:F2}초)\r\n");

                return true;
            }
            catch (Exception ex)
            {
                AppendOutput($"파이프라인 실행 중 오류: {ex.Message}\r\n");
                return false;
            }
        }

        private async Task DisplayDetectionResults()
        {
            await Task.Run(() =>
            {
                AppendOutput("\r\n" + "═".PadRight(50, '═') + "\r\n");
                AppendOutput("검출 결과 분석\r\n");
                AppendOutput("═".PadRight(50, '═') + "\r\n");

                // 1. 크롭된 이미지 확인
                string croppedDir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "PDS", "cropped");
                if (Directory.Exists(croppedDir))
                {
                    var croppedFiles = Directory.GetFiles(croppedDir, "img_1_*.bmp").OrderBy(f => f).ToList();
                    AppendOutput($"검출된 알약 이미지: {croppedFiles.Count}개\r\n");

                    if (croppedFiles.Count > 0)
                    {
                        AppendOutput($"\r\n크롭된 이미지 목록:\r\n");
                        foreach (var file in croppedFiles)
                        {
                            var fileInfo = new FileInfo(file);
                            AppendOutput($"   • {Path.GetFileName(file)} ({fileInfo.Length / 1024} KB)\r\n");
                        }

                        // UI 스레드에서 미리보기 표시
                        Invoke(new Action(() => ShowDetectionPreview(croppedFiles)));
                    }
                    else
                    {
                        AppendOutput($"검출된 알약이 없습니다.\r\n");
                    }
                }
                else
                {
                    AppendOutput($"cropped 폴더를 찾을 수 없습니다: {croppedDir}\r\n");
                }

                // 2. CSV 결과 파일 읽기
                string csvPath = Path.Combine(resultDir, "final_result.csv");
                if (File.Exists(csvPath))
                {
                    try
                    {
                        var csvLines = File.ReadAllLines(csvPath, Encoding.UTF8);
                        AppendOutput($"\r\n검출 결과 (final_result.csv):\r\n");
                        AppendOutput($"─".PadRight(50, '─') + "\r\n");

                        int displayLines = Math.Min(10, csvLines.Length);
                        for (int i = 0; i < displayLines; i++)
                        {
                            AppendOutput($"{csvLines[i]}\r\n");
                        }

                        if (csvLines.Length > displayLines)
                        {
                            AppendOutput($"... (총 {csvLines.Length}줄 중 {displayLines}줄 표시)\r\n");
                        }
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"CSV 파일 읽기 실패: {ex.Message}\r\n");
                    }
                }

                // 3. Summary 정보
                string summaryPath = Path.Combine(resultDir, "summary.txt");
                if (File.Exists(summaryPath))
                {
                    try
                    {
                        var summaryText = File.ReadAllText(summaryPath, Encoding.UTF8);
                        AppendOutput($"\r\n요약 정보 (summary.txt):\r\n");
                        AppendOutput($"─".PadRight(50, '─') + "\r\n");
                        AppendOutput(summaryText);
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"Summary 파일 읽기 실패: {ex.Message}\r\n");
                    }
                }
            });
        }

        private void ShowDetectionPreview(List<string> imagePaths)
        {
            try
            {
                flowLayoutPanelTray1Polygons.Controls.Clear();
                AppendOutput($"\r\n미리보기 패널에 {imagePaths.Count}개 이미지 표시 중...\r\n");

                foreach (string imagePath in imagePaths)
                {
                    string displayName = Path.GetFileNameWithoutExtension(imagePath);

                    var panel = new Panel
                    {
                        Size = new System.Drawing.Size(200, 120),
                        Margin = new Padding(2),
                        BorderStyle = BorderStyle.FixedSingle
                    };

                    var label = new Label
                    {
                        Text = displayName,
                        AutoSize = false,
                        Size = new System.Drawing.Size(196, 20),
                        Location = new System.Drawing.Point(2, 0),
                        Font = new Font("Consolas", 8F),
                        BackColor = Color.LightYellow,
                        TextAlign = ContentAlignment.MiddleCenter
                    };

                    var pb = new PictureBox
                    {
                        Size = new System.Drawing.Size(196, 90),
                        Location = new System.Drawing.Point(2, 22),
                        SizeMode = PictureBoxSizeMode.Zoom,
                        BorderStyle = BorderStyle.None
                    };

                    try
                    {
                        using (var bmp = new Bitmap(imagePath))
                        {
                            pb.Image = new Bitmap(bmp);
                        }
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"{displayName} 로드 실패: {ex.Message}\r\n");
                        continue;
                    }

                    panel.Controls.Add(label);
                    panel.Controls.Add(pb);
                    flowLayoutPanelTray1Polygons.Controls.Add(panel);
                }

                AppendOutput($"미리보기 표시 완료!\r\n");
            }
            catch (Exception ex)
            {
                AppendOutput($"미리보기 표시 중 오류: {ex.Message}\r\n");
            }
        }

        private void DisplaySelectedImage(string imagePath)
        {
            try
            {
                using (var bmp = new Bitmap(imagePath))
                {
                    pictureBoxTray1.Image?.Dispose();
                    pictureBoxTray1.Image = new Bitmap(bmp);
                }
                AppendOutput($"선택한 이미지를 화면에 표시했습니다.\r\n");
            }
            catch (Exception ex)
            {
                AppendOutput($"이미지 표시 실패: {ex.Message}\r\n");
            }
        }
    }
}
