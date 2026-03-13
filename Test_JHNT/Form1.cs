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

        int[] ranges_Execution = { 1, 5, 6, 10, 11, 12 };

        private const int Tray1PolygonMode = 0;
        private const string Tray1PolygonPillName = "P1234";

        // 선택된 약 목록
        private List<string> selectedPills = new List<string>();

        // 클래스 변수에 추가 - 각 약의 이미지 개수를 저장하는 딕셔너리
        private Dictionary<string, int> pillImageCounts = new Dictionary<string, int>();

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

        /// <summary>
        /// 트레이 이미지 업로드 버튼 클릭 이벤트 (다중 선택 버전)
        /// </summary>
        private void buttonUploadTray_Click(object sender, EventArgs e)
        {
            using (OpenFileDialog openFileDialog = new OpenFileDialog())
            {
                openFileDialog.Title = "트레이 이미지 선택 (여러 개 선택 가능)";
                openFileDialog.Filter = "이미지 파일 (*.bmp;*.jpg;*.jpeg;*.png)|*.bmp;*.jpg;*.jpeg;*.png|모든 파일 (*.*)|*.*";
                openFileDialog.Multiselect = true;  // 여러 파일 선택 가능
                openFileDialog.InitialDirectory = Environment.GetFolderPath(Environment.SpecialFolder.Desktop);

                if (openFileDialog.ShowDialog() == DialogResult.OK)
                {
                    string[] selectedFiles = openFileDialog.FileNames;
                    
                    if (selectedFiles.Length == 0)
                    {
                        AppendOutput("선택된 파일이 없습니다.\r\n");
                        return;
                    }

                    try
                    {
                        AppendOutput($"\r\n=== 트레이 이미지 업로드 시작 ({selectedFiles.Length}개) ===\r\n");

                        // 트레이 폴더 존재 확인
                        if (!Directory.Exists(traysDir))
                        {
                            Directory.CreateDirectory(traysDir);
                            AppendOutput($"트레이 폴더 생성: {traysDir}\r\n");
                        }

                        int successCount = 0;
                        int failCount = 0;

                        // 각 파일 처리
                        for (int i = 0; i < selectedFiles.Length; i++)
                        {
                            string selectedFile = selectedFiles[i];
                            
                            try
                            {
                                AppendOutput($"\r\n[{i + 1}/{selectedFiles.Length}] 처리 중...\r\n");
                                AppendOutput($"선택된 파일: {selectedFile}\r\n");

                                // 파일 존재 및 읽기 가능 여부 확인
                                if (!File.Exists(selectedFile))
                                {
                                    AppendOutput($"⚠ 오류: 파일을 찾을 수 없습니다.\r\n");
                                    failCount++;
                                    continue;
                                }

                                // 파일 크기 확인 (너무 큰 파일 필터링)
                                long fileSize = new FileInfo(selectedFile).Length;
                                if (fileSize > 100 * 1024 * 1024) // 100MB 이상
                                {
                                    AppendOutput($"⚠ 오류: 파일이 너무 큽니다 ({fileSize / 1024.0 / 1024.0:F2} MB). 100MB 이하의 파일만 가능합니다.\r\n");
                                    failCount++;
                                    continue;
                                }

                                AppendOutput($"파일 크기: {fileSize / 1024.0:F2} KB\r\n");

                                // 원본 파일명 유지 (확장자는 .bmp로 통일)
                                string originalFileName = Path.GetFileNameWithoutExtension(selectedFile);
                                string targetFileName = $"{originalFileName}.bmp";
                                string targetPath = Path.Combine(traysDir, targetFileName);

                                AppendOutput($"대상 파일명: {targetFileName}\r\n");

                                // 이미지를 BMP로 변환하여 저장
                                ConvertAndSaveTrayImage(selectedFile, targetPath);
                                successCount++;
                            }
                            catch (Exception ex)
                            {
                                AppendOutput($"⚠ 예외 발생: {ex.Message}\r\n");
                                failCount++;
                            }
                        }

                        // 최종 결과 출력
                        AppendOutput($"\r\n=== 업로드 완료 ===\r\n");
                        AppendOutput($"성공: {successCount}개\r\n");
                        if (failCount > 0)
                        {
                            AppendOutput($"실패: {failCount}개\r\n");
                        }
                        AppendOutput($"총 처리: {selectedFiles.Length}개\r\n");
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"\r\n예외 발생: {ex.Message}\r\n");
                        if (ex.InnerException != null)
                        {
                            AppendOutput($"내부 예외: {ex.InnerException.Message}\r\n");
                        }
                    }
                }
            }
        }

        private void ConvertAndSaveTrayImage(string sourcePath, string targetPath)
        {
            try
            {
                string targetDir = Path.GetDirectoryName(targetPath);
                if (!Directory.Exists(targetDir))
                {
                    Directory.CreateDirectory(targetDir);
                    AppendOutput($"대상 디렉토리 생성: {targetDir}\r\n");
                }

                if (File.Exists(targetPath))
                {
                    try
                    {
                        File.Delete(targetPath);
                        AppendOutput($"기존 파일 삭제: {Path.GetFileName(targetPath)}\r\n");
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"경고: 기존 파일 삭제 실패 - {ex.Message}\r\n");
                    }
                }

                if (!File.Exists(sourcePath))
                {
                    throw new FileNotFoundException($"원본 파일을 찾을 수 없습니다: {sourcePath}>:");
                }

                AppendOutput($"원본 파일 읽기: {Path.GetFileName(sourcePath)}\r\n");

                Bitmap originalBitmap = null;
                try
                {
                    originalBitmap = new Bitmap(sourcePath);
                    AppendOutput($"이미지 로드 완료: {originalBitmap.Width}x{originalBitmap.Height}\r\n");

                    using (Bitmap bmpCopy = new Bitmap(originalBitmap))
                    {
                        System.Threading.Thread.Sleep(50);
                        
                        bmpCopy.Save(targetPath, ImageFormat.Bmp);
                        AppendOutput($"✓ 이미지 저장 완료: {Path.GetFileName(targetPath)}\r\n");
                        AppendOutput($"  파일 크기: {new FileInfo(targetPath).Length / 1024.0:F2} KB\r\n");
                    }
                }
                catch (Exception ex)
                {
                    throw new Exception($"이미지 변환 중 오류 발생: {ex.Message}", ex);
                }
                finally
                {
                    originalBitmap?.Dispose();
                }
            }
            catch (Exception ex)
            {
                throw new Exception($"이미지 변환 실패: {ex.Message}", ex);
            }
        }

        // ===== 아래는 기존 랜덤 생성 코드 (주석처리) =====
        // 랜덤 트레이 생성은 더 이상 사용하지 않습니다.
        // 사용자가 업로드한 이미지를 직접 사용합니다.

        /*
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
        */

        /// <summary>
        /// 수정된 buttonGenerateTrays_Click: 업로드 기능으로 대체
        /// </summary>
        private void buttonGenerateTrays_Click(object sender, EventArgs e)
        {
            AppendOutput("\r\n※ '트레이 이미지 업로드' 기능으로 대체되었습니다.\r\n");
            AppendOutput("   우측 상단의 업로드 버튼을 사용하여 트레이 이미지를 업로드하세요.\r\n");
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

                // pill_list.txt 확인
                string pillListPath = Path.Combine(traysDir, "pill_list.txt");
                AppendOutput($"\r\n[1단계] 선택된 약 목록 확인\r\n");
                AppendOutput($"파일 경로: {pillListPath}\r\n");
                if (File.Exists(pillListPath))
                {
                    var pillList = File.ReadAllLines(pillListPath, Encoding.UTF8);
                    AppendOutput($"✓ 메모장에 등록된 약: {pillList.Length}개\r\n");
                    foreach (var pill in pillList)
                    {
                        AppendOutput($"  - {pill}\r\n");
                    }
                }
                else
                {
                    AppendOutput($"✗ 오류: pill_list.txt 파일이 없습니다!\r\n");
                    buttonRun.Enabled = true;
                    return;
                }

                // 업로드된 트레이 이미지 확인
                AppendOutput($"\r\n[2단계] 업로드된 트레이 이미지 확인\r\n");
                AppendOutput($"폴더 경로: {traysDir}\r\n");
                if (Directory.Exists(traysDir))
                {
                    var trayImages = Directory.GetFiles(traysDir, "*.bmp").ToList();
                    trayImages.RemoveAll(f => Path.GetFileName(f).ToLower() == "pill_list.txt");

                    AppendOutput($"✓ 등록된 트레이 이미지: {trayImages.Count}개\r\n");

                    if (trayImages.Count == 0)
                    {
                        AppendOutput($"✗ 경고: BMP 파일이 없습니다!\r\n");
                        buttonRun.Enabled = true;
                        return;
                    }

                    foreach (var img in trayImages.OrderBy(x => x).Take(5))
                    {
                        var fileInfo = new FileInfo(img);
                        AppendOutput($"  - {Path.GetFileName(img)} ({fileInfo.Length / 1024}KB)\r\n");
                    }
                    if (trayImages.Count > 5)
                    {
                        AppendOutput($"  ... 외 {trayImages.Count - 5}개\r\n");
                    }
                }
                else
                {
                    AppendOutput($"✗ 오류: trays 폴더가 없습니다!\r\n");
                    buttonRun.Enabled = true;
                    return;
                }

                // DLL 실행 전 절대 경로 출력 (DLL이 상대 경로 사용 가능성)
                AppendOutput($"\r\n[3단계] 절대 경로 정보\r\n");
                AppendOutput($"Trays Dir: {Path.GetFullPath(traysDir)}\r\n");
                AppendOutput($"Datasets Dir: {Path.GetFullPath(datasetsDir)}\r\n");
                AppendOutput($"Result Dir: {Path.GetFullPath(resultDir)}\r\n");

                // 결과 디렉토리 확인
                AppendOutput($"\r\n[4단계] 결과 디렉토리 확인\r\n");
                if (!Directory.Exists(resultDir))
                {
                    Directory.CreateDirectory(resultDir);
                    AppendOutput($"✓ 결과 디렉토리 생성\r\n");
                }
                else
                {
                    AppendOutput($"✓ 결과 디렉토리 존재\r\n");
                }

                // 기존 결과 파일 삭제
                AppendOutput($"\r\n[5단계] 기존 결과 파일 정리\r\n");
                string csvPath = Path.Combine(resultDir, "final_result.csv");
                string txtPath = Path.Combine(resultDir, "final_result.txt");
                string summaryPath = Path.Combine(resultDir, "summary.txt");

                if (File.Exists(csvPath)) { File.Delete(csvPath); AppendOutput($"✓ {Path.GetFileName(csvPath)} 삭제\r\n"); }
                if (File.Exists(txtPath)) { File.Delete(txtPath); AppendOutput($"✓ {Path.GetFileName(txtPath)} 삭제\r\n"); }
                if (File.Exists(summaryPath)) { File.Delete(summaryPath); AppendOutput($"✓ {Path.GetFileName(summaryPath)} 삭제\r\n"); }

                AppendOutput($"\r\n[6단계] DLL 실행\r\n");

                int[] ranges = GetAvailableTrayRanges();
                if (ranges.Length == 0)
                {
                    AppendOutput($"✗ 오류: 처리할 트레이가 없습니다!\r\n");
                    buttonRun.Enabled = true;
                    return;
                }

                AppendOutput($"호출: RunPillDetectionMain(ranges, {ranges.Length})\r\n");
                AppendOutput($"처리 대상 트레이: [{string.Join(", ", ranges)}]\r\n");

                int ret = await Task.Run(() => RunPillDetectionMain(ranges, ranges.Length));

                stopwatch.Stop();

                AppendOutput($"\r\n[7단계] DLL 실행 결과\r\n");
                AppendOutput($"반환값: {ret}\r\n");
                AppendOutput($"실행 시간: {stopwatch.Elapsed.TotalMilliseconds}ms\r\n");

                if (ret != 0)
                {
                    AppendOutput($"✗ 오류 발생: 반환 코드 {ret}\r\n");
                    AppendOutput($"💡 팁: DLL이 `pill_list.txt` 또는 트레이 이미지를 찾지 못했을 수 있습니다.\r\n");
                }
                else
                {
                    AppendOutput($"✓ DLL 실행 성공\r\n");
                }

                // 생성된 파일 확인
                AppendOutput($"\r\n[8단계] 생성된 결과 파일 확인\r\n");
                System.Threading.Thread.Sleep(1000); // 1초 대기

                string[] resultFiles = { csvPath, txtPath, summaryPath };
                foreach (var filePath in resultFiles)
                {
                    if (File.Exists(filePath))
                    {
                        var fileInfo = new FileInfo(filePath);
                        AppendOutput($"✓ {Path.GetFileName(filePath)} ({fileInfo.Length} bytes)\r\n");
                    }
                    else
                    {
                        AppendOutput($"✗ {Path.GetFileName(filePath)} - 생성되지 않음\r\n");
                    }
                }

                // CSV 내용 확인
                AppendOutput($"\r\n[9단계] CSV 결과 내용 확인\r\n");
                if (File.Exists(csvPath))
                {
                    try
                    {
                        using (var fileStream = new FileStream(csvPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                        using (var reader = new StreamReader(fileStream, Encoding.UTF8))
                        {
                            var lines = new List<string>();
                            string line;
                            while ((line = reader.ReadLine()) != null)
                            {
                                lines.Add(line);
                            }

                            if (lines.Count == 0)
                            {
                                AppendOutput($"⚠ CSV 파일이 비어있습니다!\r\n");
                                AppendOutput($"💡 원인: DLL이 트레이에서 약을 검출하지 못했습니다.\r\n");
                            }
                            else
                            {
                                AppendOutput($"✓ 총 {lines.Count}행\r\n");
                                AppendOutput($"첫 5줄:\r\n");
                                for (int i = 0; i < Math.Min(5, lines.Count); i++)
                                {
                                    string displayLine = lines[i];
                                    if (displayLine.Length > 120)
                                        AppendOutput($"  {i}: {displayLine.Substring(0, 120)}...\r\n");
                                    else
                                        AppendOutput($"  {i}: {displayLine}\r\n");
                                }
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"✗ CSV 읽기 오류: {ex.Message}\r\n");
                    }
                }

                AppendOutput($"\r\n[9단계] pill_list.txt 내용 확인\r\n");
                if (File.Exists(pillListPath))
                {
                    var pillContent = File.ReadAllLines(pillListPath, Encoding.UTF8);
                    AppendOutput($"pill_list.txt 내용:\r\n");
                    foreach (var line in pillContent)
                    {
                        AppendOutput($"  - '{line}'\r\n");
                    }
                }

                AppendOutput($"\r\n[10단계] DLL 작동 진단\r\n");
                
                // 결과 파일 생성 확인
                if (File.Exists(csvPath) && new FileInfo(csvPath).Length > 114)
                {
                    AppendOutput($"✓ CSV에 데이터가 있음 - DLL이 정상 작동\r\n");
                }
                else
                {
                    AppendOutput($"✗ CSV에 데이터가 없음 - DLL이 트레이를 분석하지 못함\r\n");
                    AppendOutput($"💡 확인 사항:\r\n");
                    AppendOutput($"  1. pill_list.txt가 올바른 형식인지 확인\r\n");
                    AppendOutput($"  2. {traysDir} 경로가 올바른지 확인\r\n");
                    AppendOutput($"  3. {datasetsDir} 폴더가 존재하고 학습 모델이 있는지 확인\r\n");
                    AppendOutput($"  4. DLL의 설정 파일이나 환경 변수 확인\r\n");
                }

                TimeSpan elapsed = stopwatch.Elapsed;
                AppendOutput($"\r\n=== 처리 완료 ===\r\n");
                AppendOutput($"종료 시간: {DateTime.Now:yyyy-MM-dd HH:mm:ss.fff}\r\n");
                AppendOutput($"총 실행 시간: {elapsed.Hours:D2}:{elapsed.Minutes:D2}:{elapsed.Seconds:D2}.{elapsed.Milliseconds:D3}\r\n");
            }
            catch (Exception ex)
            {
                stopwatch.Stop();
                AppendOutput($"\r\n✗ 예외 발생: {ex.Message}\r\n");
                AppendOutput($"스택 트레이스:\r\n{ex.StackTrace}\r\n");
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
                    List<string[]> csvData = ReadCsvFile(csvPath);
                    if (csvData.Count == 0)
                    {
                        AppendOutput("오류: CSV 파일이 비어있습니다.\r\n");
                        return;
                    }

                    AppendOutput($"Excel 파일 생성 중: {excelPath}\r\n");

                    ExcelPackage.LicenseContext = LicenseContext.NonCommercial;

                    using (var package = new ExcelPackage())
                    {
                        var worksheet = package.Workbook.Worksheets.Add("Results_Transposed");

                        worksheet.Column(1).Width = 25;
                        worksheet.Column(2).Width = 12;
                        
                        int totalRows = csvData.Count;
                        int maxCols = 0;
                        
                        for (int row = 0; row < totalRows; row++)
                        {
                            if (csvData[row].Length > maxCols)
                                maxCols = csvData[row].Length;
                        }
                        
                        for (int row = 0; row < totalRows; row++)
                        {
                            string[] rowData = csvData[row];
                            
                            worksheet.Row(row + 1).Height = 80;
                            
                            for (int col = 0; col < rowData.Length; col++)
                            {
                                var cell = worksheet.Cells[row + 1, col + 1];
                                
                                cell.Value = rowData[col];
                                
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

                        int prescriptionImageCount = 0;
                        for (int row = 1; row < totalRows; row++)
                        {
                            string[] rowData = csvData[row];
                            if (rowData.Length < 1) continue;
                            
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

                        int totalCropImages = 0;
                        int missingCropImages = 0;
                        for (int row = 1; row < totalRows; row++)
                        {
                            string[] rowData = csvData[row];
                            
                            for (int col = 2; col < rowData.Length; col++)
                            {
                                string cellValue = rowData[col];
                                if (string.IsNullOrWhiteSpace(cellValue))
                                    continue;

                                // 새로운 메서드 사용 - 약 정보 포함
                                List<(string cropName, float probability, string pillCode)> cropDetails = 
                                    ExtractCropNamesWithDetails(cellValue);
                                
                                if (cropDetails.Count > 0)
                                {
                                    var cell = worksheet.Cells[row + 1, col + 1];
                                    int imageIndex = 0;
                                    int totalImageHeight = 0;
                                    int imageSpacing = 5;
                                    
                                    // 셀 값을 정리된 형식으로 재구성
                                    var formattedValues = cropDetails.Select(cd => 
                                        string.IsNullOrEmpty(cd.pillCode) 
                                            ? $"{cd.cropName} ({cd.probability:F2})"
                                            : $"{cd.cropName} ({cd.probability:F2}, {cd.pillCode})"
                                    ).ToList();
                                    
                                    cell.Value = string.Join("\n", formattedValues);
                                    cell.Style.WrapText = true;
                                    cell.Style.VerticalAlignment = OfficeOpenXml.Style.ExcelVerticalAlignment.Top;
                                    
                                    // 이미지 삽입
                                    foreach (var (cropName, prob, pillCode) in cropDetails)
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

                        FileInfo excelFile = new FileInfo(excelPath);
                        package.SaveAs(excelFile);
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
                    // 이스케이프된 따옴표 (“”)는 두 개가 하나의 실제 따옴표이므로
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
            // 또는 "img_1_10 (0.00, 06730081)" - 확률과 약 코드 포함
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

        /// <summary>
        /// 셀 값에서 확률과 약 정보를 추출하는 메서드 추가
        /// </summary>
        private List<(string cropName, float probability, string pillCode)> ExtractCropNamesWithDetails(string cellValue)
        {
            List<(string, float, string)> results = new List<(string, float, string)>();
            
            // 셀 값 형식: "img_1_10 (0.70, 06730081)"
            string[] lines = cellValue.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
            
            foreach (string line in lines)
            {
                string trimmedLine = line.Trim();
                if (string.IsNullOrEmpty(trimmedLine))
                    continue;
                
                // "img_1_10 (0.00, 06730081)" 형식 파싱
                // 정규식: 이미지명 (확률, 약코드) 형식 추출
                Match match = Regex.Match(trimmedLine, @"^([^\(]+)\s*\(\s*([\d.]+)\s*,\s*([^\)]+)\s*\)");
                if (match.Success)
                {
                    string cropName = match.Groups[1].Value.Trim();
                    string probStr = match.Groups[2].Value.Trim();
                    string pillCode = match.Groups[3].Value.Trim();
                    
                    if (float.TryParse(probStr, out float probability))
                    {
                        results.Add((cropName, probability, pillCode));
                    }
                }
                else
                {
                    // 약 코드가 없는 경우
                    Match match2 = Regex.Match(trimmedLine, @"^([^\(]+)\s*\(\s*([\d.]+)\s*\)");
                    if (match2.Success)
                    {
                        string cropName = match2.Groups[1].Value.Trim();
                        string probStr = match2.Groups[2].Value.Trim();
                        
                        if (float.TryParse(probStr, out float probability))
                        {
                            results.Add((cropName, probability, ""));
                        }
                    }
                }
            }

            return results;
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

        private void buttonSelectPills_Click(object sender, EventArgs e)
        {
            if (panelPillSelection.Visible)
            {
                // 패널 숨기기
                panelPillSelection.Visible = false;
                textBoxOutput.Size = new System.Drawing.Size(1016, 382);
                AppendOutput("\r\n약 선택 패널 닫음\r\n");
            }
            else
            {
                // 약 목록 로드 및 패널 표시
                LoadPillsFromDatasets();
                panelPillSelection.Visible = true;
                textBoxOutput.Size = new System.Drawing.Size(766, 382);
                AppendOutput($"\r\n약 선택 패널 열음 - 총 {checkedListBoxPills.Items.Count}개 약물\r\n");
            }
        }

        /// <summary>
        /// datasets 폴더에서 약 목록을 로드하여 CheckedListBox에 추가
        /// 각 약의 이미지 개수도 함께 카운트
        /// </summary>
        private void LoadPillsFromDatasets()
        {
            try
            {
                checkedListBoxPills.Items.Clear();
                pillImageCounts.Clear();
                
                if (!Directory.Exists(datasetsDir))
                {
                    AppendOutput($"오류: datasets 폴더를 찾을 수 없습니다: {datasetsDir}\r\n");
                    return;
                }
                
                // datasets 폴더의 모든 서브폴더 (약 폴더) 가져오기
                var pillFolders = Directory.GetDirectories(datasetsDir)
                    .Select(path => Path.GetFileName(path))
                    .OrderBy(name => name)
                    .ToList();
                
                if (pillFolders.Count == 0)
                {
                    AppendOutput($"datasets 폴더에 약 폴더가 없습니다: {datasetsDir}\r\n");
                    return;
                }
                
                // 각 약 폴더에서 이미지 개수 카운트
                foreach (var pillName in pillFolders)
                {
                    string pillPath = Path.Combine(datasetsDir, pillName);
                    int imageCount = 0;
                    
                    try
                    {
                        // 이미지 파일 개수 세기 (.bmp, .jpg, .jpeg, .png)
                        var imageFiles = Directory.GetFiles(pillPath, "*.*")
                            .Where(f => f.EndsWith(".bmp", StringComparison.OrdinalIgnoreCase) ||
                                       f.EndsWith(".jpg", StringComparison.OrdinalIgnoreCase) ||
                                       f.EndsWith(".jpeg", StringComparison.OrdinalIgnoreCase) ||
                                       f.EndsWith(".png", StringComparison.OrdinalIgnoreCase))
                            .ToList();
                        
                        imageCount = imageFiles.Count;
                        pillImageCounts[pillName] = imageCount;
                    }
                    catch (Exception ex)
                    {
                        AppendOutput($"경고: {pillName} 폴더에서 이미지 개수 계산 실패 - {ex.Message}\r\n");
                        pillImageCounts[pillName] = 0;
                    }
                }
                
                // CheckedListBox에 약 목록 추가 (이미지 개수 포함)
                foreach (var pillName in pillFolders)
                {
                    int count = pillImageCounts.ContainsKey(pillName) ? pillImageCounts[pillName] : 0;
                    string displayName = $"{pillName}({count})";
                    bool isChecked = selectedPills.Contains(pillName);
                    checkedListBoxPills.Items.Add(displayName, isChecked);
                }
                
                AppendOutput($"약 목록 로드 완료: {pillFolders.Count}개\r\n");
                UpdatePillCountLabel();
            }
            catch (Exception ex)
            {
                AppendOutput($"약 목록 로드 중 오류: {ex.Message}\r\n");
            }
        }

        /// <summary>
        /// CheckedListBox의 항목 선택 상태 변경 이벤트
        /// 실시간으로 선택 상태를 업데이트하고 파일에 저장
        /// </summary>
        private void checkedListBoxPills_ItemCheck(object sender, ItemCheckEventArgs e)
        {
            // 항목 상태 변경 후 업데이트
            this.BeginInvoke(new Action(() =>
            {
                UpdateSelectedPills();
                SaveSelectedPillsList();
            }));
        }

        /// <summary>
        /// 선택된 약 목록 업데이트
        /// CheckedListBox의 표시명에서 실제 약 이름 추출
        /// </summary>
        private void UpdateSelectedPills()
        {
            selectedPills.Clear();
            
            foreach (var item in checkedListBoxPills.CheckedItems)
            {
                string displayName = item.ToString();
                
                // "약이름(갯수)" 형식에서 약이름만 추출
                int parenIndex = displayName.LastIndexOf('(');
                if (parenIndex > 0)
                {
                    string pillName = displayName.Substring(0, parenIndex).Trim();
                    selectedPills.Add(pillName);
                }
                else
                {
                    // 괄호가 없으면 전체 문자열 사용
                    selectedPills.Add(displayName);
                }
            }
            UpdatePillCountLabel();
        }

        /// <summary>
        /// 선택된 약 개수 레이블 업데이트
        /// </summary>
        private void UpdatePillCountLabel()
        {
            labelPillCount.Text = $"선택된 약: {selectedPills.Count}개";
        }

        /// <summary>
        /// 선택된 약 목록을 pill_list.txt에 실시간으로 저장
        /// 형식: 약 코드만 (예: 06710032)
        /// </summary>
        private void SaveSelectedPillsList()
        {
            try
            {
                if (!Directory.Exists(traysDir))
                {
                    Directory.CreateDirectory(traysDir);
                }

                string pillListPath = Path.Combine(traysDir, "pill_list.txt");

                // UTF-8 BOM 없음으로 파일 작성
                // 주의: 괄호 안의 개수는 제거하고 순수 약 코드만 저장
                using (var writer = new StreamWriter(pillListPath, false, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false)))
                {
                    foreach (var pill in selectedPills)
                    {
                        // 혹시 괄호가 포함된 형식이면 제거
                        string cleanPill = pill;
                        int parenIndex = cleanPill.IndexOf('(');
                        if (parenIndex > 0)
                        {
                            cleanPill = cleanPill.Substring(0, parenIndex).Trim();
                        }

                        writer.WriteLine(cleanPill);
                    }
                }
            }
            catch (Exception ex)
            {
                AppendOutput($"⚠ 약 목록 저장 중 오류: {ex.Message}\r\n");
            }
        }

        private int[] GetAvailableTrayRanges()
        {
            try
            {
                if (!Directory.Exists(traysDir))
                    return new int[] { };

                var trayFiles = Directory.GetFiles(traysDir, "*.bmp")
                    .Where(f => !Path.GetFileName(f).Contains("pill_list"))
                    .OrderBy(f => f)
                    .ToList();

                // 30개 트레이면 1~30 배열 생성
                int[] ranges = new int[trayFiles.Count];
                for (int i = 0; i < trayFiles.Count; i++)
                {
                    ranges[i] = i + 1;  // 1, 2, 3, ..., 30
                }

                AppendOutput($"✓ 감지된 트레이 파일: {trayFiles.Count}개\r\n");
                foreach (var file in trayFiles)
                {
                    AppendOutput($"  - {Path.GetFileName(file)}\r\n");
                }

                return ranges;
            }
            catch
            {
                return new int[] { };
            }
        }
    }
}
