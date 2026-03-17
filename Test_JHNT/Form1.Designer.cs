namespace Test_JHNT
{
    partial class Form1
    {
        private System.ComponentModel.IContainer components = null;
        private System.Windows.Forms.Button buttonPrewarm;
        private System.Windows.Forms.Button buttonGenerateTrays;
        private System.Windows.Forms.Button buttonPrescriptionMasks;
        private System.Windows.Forms.Button buttonRun;
        private System.Windows.Forms.Button buttonCreateExcel;
        private System.Windows.Forms.Button buttonTrayImage;
        private System.Windows.Forms.Button buttonTray1Polygon;
        private System.Windows.Forms.Button buttonRegisterPill;
        private System.Windows.Forms.Button buttonDetectPill;
        private System.Windows.Forms.PictureBox pictureBoxTray1;
        private System.Windows.Forms.PictureBox pictureBoxTray2;
        private System.Windows.Forms.FlowLayoutPanel flowLayoutPanelTray1Polygons;
        private System.Windows.Forms.TextBox textBoxOutput;

        protected override void Dispose(bool disposing)
        {
            if (disposing && (components != null))
            {
                components.Dispose();
            }
            base.Dispose(disposing);
        }

        private void InitializeComponent()
        {
            buttonPrewarm = new Button();
            buttonGenerateTrays = new Button();
            buttonPrescriptionMasks = new Button();
            buttonRun = new Button();
            buttonCreateExcel = new Button();
            buttonTrayImage = new Button();
            buttonTray1Polygon = new Button();
            buttonRegisterPill = new Button();
            buttonDetectPill = new Button();
            pictureBoxTray1 = new PictureBox();
            pictureBoxTray2 = new PictureBox();
            flowLayoutPanelTray1Polygons = new FlowLayoutPanel();
            textBoxOutput = new TextBox();
            ((System.ComponentModel.ISupportInitialize)pictureBoxTray1).BeginInit();
            ((System.ComponentModel.ISupportInitialize)pictureBoxTray2).BeginInit();
            SuspendLayout();
            // 
            // buttonPrewarm
            // 
            buttonPrewarm.Enabled = false;
            buttonPrewarm.Location = new Point(15, 113);
            buttonPrewarm.Margin = new Padding(4, 5, 4, 5);
            buttonPrewarm.Name = "buttonPrewarm";
            buttonPrewarm.Size = new Size(231, 83);
            buttonPrewarm.TabIndex = 1;
            buttonPrewarm.Text = "사전 준비";
            buttonPrewarm.UseVisualStyleBackColor = true;
            buttonPrewarm.Click += buttonPrewarm_Click;
            // 
            // buttonGenerateTrays
            // 
            buttonGenerateTrays.Enabled = false;
            buttonGenerateTrays.Location = new Point(15, 20);
            buttonGenerateTrays.Margin = new Padding(4, 5, 4, 5);
            buttonGenerateTrays.Name = "buttonGenerateTrays";
            buttonGenerateTrays.Size = new Size(231, 83);
            buttonGenerateTrays.TabIndex = 0;
            buttonGenerateTrays.Text = "트레이 생성";
            buttonGenerateTrays.UseVisualStyleBackColor = true;
            buttonGenerateTrays.Click += buttonGenerateTrays_Click;
            // 
            // buttonPrescriptionMasks
            // 
            buttonPrescriptionMasks.Enabled = false;
            buttonPrescriptionMasks.Location = new Point(255, 113);
            buttonPrescriptionMasks.Margin = new Padding(4, 5, 4, 5);
            buttonPrescriptionMasks.Name = "buttonPrescriptionMasks";
            buttonPrescriptionMasks.Size = new Size(231, 83);
            buttonPrescriptionMasks.TabIndex = 2;
            buttonPrescriptionMasks.Text = "처방 마스크 생성";
            buttonPrescriptionMasks.UseVisualStyleBackColor = true;
            buttonPrescriptionMasks.Click += buttonPrescriptionMasks_Click;
            // 
            // buttonRun
            // 
            buttonRun.Enabled = false;
            buttonRun.Location = new Point(494, 113);
            buttonRun.Margin = new Padding(4, 5, 4, 5);
            buttonRun.Name = "buttonRun";
            buttonRun.Size = new Size(231, 83);
            buttonRun.TabIndex = 3;
            buttonRun.Text = "추론 실행";
            buttonRun.UseVisualStyleBackColor = true;
            buttonRun.Click += buttonRun_Click;
            // 
            // buttonCreateExcel
            // 
            buttonCreateExcel.Enabled = false;
            buttonCreateExcel.Location = new Point(733, 113);
            buttonCreateExcel.Margin = new Padding(4, 5, 4, 5);
            buttonCreateExcel.Name = "buttonCreateExcel";
            buttonCreateExcel.Size = new Size(231, 83);
            buttonCreateExcel.TabIndex = 4;
            buttonCreateExcel.Text = "Excel 파일 생성";
            buttonCreateExcel.UseVisualStyleBackColor = true;
            buttonCreateExcel.Click += buttonCreateExcel_Click;
            // 
            // buttonTrayImage
            // 
            buttonTrayImage.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            buttonTrayImage.Location = new Point(977, 20);
            buttonTrayImage.Margin = new Padding(4, 5, 4, 5);
            buttonTrayImage.Name = "buttonTrayImage";
            buttonTrayImage.Size = new Size(129, 40);
            buttonTrayImage.TabIndex = 6;
            buttonTrayImage.Text = "tray 이미지";
            buttonTrayImage.UseVisualStyleBackColor = true;
            buttonTrayImage.Click += buttonTrayImage_Click;
            // 
            // buttonTray1Polygon
            // 
            buttonTray1Polygon.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            buttonTray1Polygon.Location = new Point(977, 533);
            buttonTray1Polygon.Margin = new Padding(4, 5, 4, 5);
            buttonTray1Polygon.Name = "buttonTray1Polygon";
            buttonTray1Polygon.Size = new Size(129, 40);
            buttonTray1Polygon.TabIndex = 9;
            buttonTray1Polygon.Text = "Tray1 polygon";
            buttonTray1Polygon.UseVisualStyleBackColor = true;
            buttonTray1Polygon.Click += buttonTray1Polygon_Click;
            // 
            // buttonRegisterPill
            // 
            buttonRegisterPill.BackColor = Color.Coral;
            buttonRegisterPill.Location = new Point(255, 20);
            buttonRegisterPill.Margin = new Padding(4, 5, 4, 5);
            buttonRegisterPill.Name = "buttonRegisterPill";
            buttonRegisterPill.Size = new Size(231, 83);
            buttonRegisterPill.TabIndex = 11;
            buttonRegisterPill.Text = "약 등록";
            buttonRegisterPill.UseVisualStyleBackColor = false;
            buttonRegisterPill.Click += buttonRegisterPill_Click;
            // 
            // buttonDetectPill
            // 
            buttonDetectPill.BackColor = Color.LightGreen;
            buttonDetectPill.Font = new Font("맑은 고딕", 9F, FontStyle.Bold);
            buttonDetectPill.Location = new Point(494, 20);
            buttonDetectPill.Margin = new Padding(4, 5, 4, 5);
            buttonDetectPill.Name = "buttonDetectPill";
            buttonDetectPill.Size = new Size(231, 83);
            buttonDetectPill.TabIndex = 12;
            buttonDetectPill.Text = "약 검출";
            buttonDetectPill.UseVisualStyleBackColor = false;
            buttonDetectPill.Click += buttonDetectPill_Click;
            // 
            // pictureBoxTray1
            // 
            pictureBoxTray1.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            pictureBoxTray1.BorderStyle = BorderStyle.FixedSingle;
            pictureBoxTray1.Location = new Point(977, 63);
            pictureBoxTray1.Margin = new Padding(4, 5, 4, 5);
            pictureBoxTray1.Name = "pictureBoxTray1";
            pictureBoxTray1.Size = new Size(282, 224);
            pictureBoxTray1.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBoxTray1.TabIndex = 7;
            pictureBoxTray1.TabStop = false;
            // 
            // pictureBoxTray2
            // 
            pictureBoxTray2.Anchor = AnchorStyles.Top | AnchorStyles.Right;
            pictureBoxTray2.BorderStyle = BorderStyle.FixedSingle;
            pictureBoxTray2.Location = new Point(977, 298);
            pictureBoxTray2.Margin = new Padding(4, 5, 4, 5);
            pictureBoxTray2.Name = "pictureBoxTray2";
            pictureBoxTray2.Size = new Size(282, 224);
            pictureBoxTray2.SizeMode = PictureBoxSizeMode.Zoom;
            pictureBoxTray2.TabIndex = 8;
            pictureBoxTray2.TabStop = false;
            // 
            // flowLayoutPanelTray1Polygons
            // 
            flowLayoutPanelTray1Polygons.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Right;
            flowLayoutPanelTray1Polygons.AutoScroll = true;
            flowLayoutPanelTray1Polygons.BorderStyle = BorderStyle.FixedSingle;
            flowLayoutPanelTray1Polygons.Location = new Point(977, 577);
            flowLayoutPanelTray1Polygons.Margin = new Padding(4, 5, 4, 5);
            flowLayoutPanelTray1Polygons.Name = "flowLayoutPanelTray1Polygons";
            flowLayoutPanelTray1Polygons.Size = new Size(282, 152);
            flowLayoutPanelTray1Polygons.TabIndex = 10;
            // 
            // textBoxOutput
            // 
            textBoxOutput.Anchor = AnchorStyles.Top | AnchorStyles.Bottom | AnchorStyles.Left | AnchorStyles.Right;
            textBoxOutput.Font = new Font("Consolas", 9F);
            textBoxOutput.Location = new Point(15, 207);
            textBoxOutput.Margin = new Padding(4, 5, 4, 5);
            textBoxOutput.Multiline = true;
            textBoxOutput.Name = "textBoxOutput";
            textBoxOutput.ReadOnly = true;
            textBoxOutput.ScrollBars = ScrollBars.Vertical;
            textBoxOutput.Size = new Size(945, 521);
            textBoxOutput.TabIndex = 5;
            // 
            // Form1
            // 
            AutoScaleDimensions = new SizeF(9F, 20F);
            AutoScaleMode = AutoScaleMode.Font;
            ClientSize = new Size(1286, 750);
            Controls.Add(buttonDetectPill);
            Controls.Add(flowLayoutPanelTray1Polygons);
            Controls.Add(pictureBoxTray1);
            Controls.Add(pictureBoxTray2);
            Controls.Add(buttonTray1Polygon);
            Controls.Add(buttonTrayImage);
            Controls.Add(buttonRegisterPill);
            Controls.Add(textBoxOutput);
            Controls.Add(buttonCreateExcel);
            Controls.Add(buttonRun);
            Controls.Add(buttonPrescriptionMasks);
            Controls.Add(buttonGenerateTrays);
            Controls.Add(buttonPrewarm);
            Margin = new Padding(4, 5, 4, 5);
            Name = "Form1";
            Text = "Prediction Processing";
            ((System.ComponentModel.ISupportInitialize)pictureBoxTray1).EndInit();
            ((System.ComponentModel.ISupportInitialize)pictureBoxTray2).EndInit();
            ResumeLayout(false);
            PerformLayout();
        }
    }
}
