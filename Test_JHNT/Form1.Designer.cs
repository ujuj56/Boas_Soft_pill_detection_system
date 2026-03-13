namespace Test_JHNT
{
    partial class Form1
    {
        private System.ComponentModel.IContainer components = null;
        private System.Windows.Forms.Button buttonPrewarm;
        private System.Windows.Forms.Button buttonPrescriptionMasks;
        private System.Windows.Forms.Button buttonRun;
        private System.Windows.Forms.Button buttonCreateExcel;
        private System.Windows.Forms.Button buttonUploadTray;
        private System.Windows.Forms.Button buttonTray1Polygon;
        private System.Windows.Forms.Button buttonSelectPills;
        private System.Windows.Forms.TextBox textBoxOutput;
        private System.Windows.Forms.Panel panelPillSelection;
        private System.Windows.Forms.CheckedListBox checkedListBoxPills;
        private System.Windows.Forms.Label labelPillCount;
        private System.Windows.Forms.FlowLayoutPanel flowLayoutPanelTray1Polygons;

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
            this.buttonPrewarm = new System.Windows.Forms.Button();
            this.buttonPrescriptionMasks = new System.Windows.Forms.Button();
            this.buttonRun = new System.Windows.Forms.Button();
            this.buttonCreateExcel = new System.Windows.Forms.Button();
            this.buttonUploadTray = new System.Windows.Forms.Button();
            this.buttonTray1Polygon = new System.Windows.Forms.Button();
            this.buttonSelectPills = new System.Windows.Forms.Button();
            this.textBoxOutput = new System.Windows.Forms.TextBox();
            this.panelPillSelection = new System.Windows.Forms.Panel();
            this.checkedListBoxPills = new System.Windows.Forms.CheckedListBox();
            this.labelPillCount = new System.Windows.Forms.Label();
            this.flowLayoutPanelTray1Polygons = new System.Windows.Forms.FlowLayoutPanel();
            this.panelPillSelection.SuspendLayout();
            this.SuspendLayout();
            
            // buttonPrewarm
            this.buttonPrewarm.Location = new System.Drawing.Point(12, 12);
            this.buttonPrewarm.Name = "buttonPrewarm";
            this.buttonPrewarm.Size = new System.Drawing.Size(140, 40);
            this.buttonPrewarm.TabIndex = 0;
            this.buttonPrewarm.Text = "사전 준비";
            this.buttonPrewarm.UseVisualStyleBackColor = true;
            this.buttonPrewarm.Click += new System.EventHandler(this.buttonPrewarm_Click);
            
            // buttonPrescriptionMasks
            this.buttonPrescriptionMasks.Location = new System.Drawing.Point(158, 12);
            this.buttonPrescriptionMasks.Name = "buttonPrescriptionMasks";
            this.buttonPrescriptionMasks.Size = new System.Drawing.Size(140, 40);
            this.buttonPrescriptionMasks.TabIndex = 1;
            this.buttonPrescriptionMasks.Text = "처방 마스크 생성";
            this.buttonPrescriptionMasks.UseVisualStyleBackColor = true;
            this.buttonPrescriptionMasks.Click += new System.EventHandler(this.buttonPrescriptionMasks_Click);
            
            // buttonRun
            this.buttonRun.Location = new System.Drawing.Point(304, 12);
            this.buttonRun.Name = "buttonRun";
            this.buttonRun.Size = new System.Drawing.Size(140, 40);
            this.buttonRun.TabIndex = 2;
            this.buttonRun.Text = "추론 실행";
            this.buttonRun.UseVisualStyleBackColor = true;
            this.buttonRun.Click += new System.EventHandler(this.buttonRun_Click);
            
            // buttonCreateExcel
            this.buttonCreateExcel.Location = new System.Drawing.Point(450, 12);
            this.buttonCreateExcel.Name = "buttonCreateExcel";
            this.buttonCreateExcel.Size = new System.Drawing.Size(140, 40);
            this.buttonCreateExcel.TabIndex = 3;
            this.buttonCreateExcel.Text = "Excel 파일 생성";
            this.buttonCreateExcel.UseVisualStyleBackColor = true;
            this.buttonCreateExcel.Click += new System.EventHandler(this.buttonCreateExcel_Click);
            
            // buttonUploadTray
            this.buttonUploadTray.Location = new System.Drawing.Point(596, 12);
            this.buttonUploadTray.Name = "buttonUploadTray";
            this.buttonUploadTray.Size = new System.Drawing.Size(140, 40);
            this.buttonUploadTray.TabIndex = 4;
            this.buttonUploadTray.Text = "이미지 업로드";
            this.buttonUploadTray.UseVisualStyleBackColor = true;
            this.buttonUploadTray.Click += new System.EventHandler(this.buttonUploadTray_Click);
            
            // buttonSelectPills
            this.buttonSelectPills.Location = new System.Drawing.Point(742, 12);
            this.buttonSelectPills.Name = "buttonSelectPills";
            this.buttonSelectPills.Size = new System.Drawing.Size(140, 40);
            this.buttonSelectPills.TabIndex = 5;
            this.buttonSelectPills.Text = "약 선택";
            this.buttonSelectPills.UseVisualStyleBackColor = true;
            this.buttonSelectPills.Click += new System.EventHandler(this.buttonSelectPills_Click);
            
            // buttonTray1Polygon
            this.buttonTray1Polygon.Location = new System.Drawing.Point(888, 12);
            this.buttonTray1Polygon.Name = "buttonTray1Polygon";
            this.buttonTray1Polygon.Size = new System.Drawing.Size(140, 40);
            this.buttonTray1Polygon.TabIndex = 6;
            this.buttonTray1Polygon.Text = "Tray1 polygon";
            this.buttonTray1Polygon.UseVisualStyleBackColor = true;
            this.buttonTray1Polygon.Click += new System.EventHandler(this.buttonTray1Polygon_Click);
            
            // panelPillSelection
            this.panelPillSelection.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.panelPillSelection.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.panelPillSelection.Location = new System.Drawing.Point(1034, 58);
            this.panelPillSelection.Name = "panelPillSelection";
            this.panelPillSelection.Size = new System.Drawing.Size(250, 382);
            this.panelPillSelection.TabIndex = 7;
            this.panelPillSelection.Visible = false;
            this.panelPillSelection.SuspendLayout();
            
            // labelPillCount
            this.labelPillCount.AutoSize = true;
            this.labelPillCount.Location = new System.Drawing.Point(8, 8);
            this.labelPillCount.Name = "labelPillCount";
            this.labelPillCount.Size = new System.Drawing.Size(81, 12);
            this.labelPillCount.TabIndex = 0;
            this.labelPillCount.Text = "선택된 약: 0개";
            
            // checkedListBoxPills
            this.checkedListBoxPills.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.checkedListBoxPills.FormattingEnabled = true;
            this.checkedListBoxPills.Location = new System.Drawing.Point(8, 28);
            this.checkedListBoxPills.Name = "checkedListBoxPills";
            this.checkedListBoxPills.Size = new System.Drawing.Size(232, 340);
            this.checkedListBoxPills.TabIndex = 1;
            this.checkedListBoxPills.ItemCheck += new System.Windows.Forms.ItemCheckEventHandler(this.checkedListBoxPills_ItemCheck);
            
            this.panelPillSelection.Controls.Add(this.labelPillCount);
            this.panelPillSelection.Controls.Add(this.checkedListBoxPills);
            this.panelPillSelection.ResumeLayout(false);
            this.panelPillSelection.PerformLayout();
            
            // flowLayoutPanelTray1Polygons
            this.flowLayoutPanelTray1Polygons.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.flowLayoutPanelTray1Polygons.AutoScroll = true;
            this.flowLayoutPanelTray1Polygons.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.flowLayoutPanelTray1Polygons.Location = new System.Drawing.Point(12, 58);
            this.flowLayoutPanelTray1Polygons.Name = "flowLayoutPanelTray1Polygons";
            this.flowLayoutPanelTray1Polygons.Size = new System.Drawing.Size(1016, 382);
            this.flowLayoutPanelTray1Polygons.TabIndex = 9;
            this.flowLayoutPanelTray1Polygons.Visible = false;
            
            // textBoxOutput
            this.textBoxOutput.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.textBoxOutput.Location = new System.Drawing.Point(12, 58);
            this.textBoxOutput.Multiline = true;
            this.textBoxOutput.Name = "textBoxOutput";
            this.textBoxOutput.ReadOnly = true;
            this.textBoxOutput.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this.textBoxOutput.Size = new System.Drawing.Size(1016, 382);
            this.textBoxOutput.TabIndex = 8;
            this.textBoxOutput.Font = new System.Drawing.Font("Consolas", 9F);
            
            // Form1
            this.AutoScaleDimensions = new System.Drawing.SizeF(7F, 12F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(1296, 450);
            this.Controls.Add(this.flowLayoutPanelTray1Polygons);
            this.Controls.Add(this.panelPillSelection);
            this.Controls.Add(this.textBoxOutput);
            this.Controls.Add(this.buttonTray1Polygon);
            this.Controls.Add(this.buttonSelectPills);
            this.Controls.Add(this.buttonUploadTray);
            this.Controls.Add(this.buttonCreateExcel);
            this.Controls.Add(this.buttonRun);
            this.Controls.Add(this.buttonPrescriptionMasks);
            this.Controls.Add(this.buttonPrewarm);
            this.Name = "Form1";
            this.Text = "Prediction Processing";
            this.panelPillSelection.ResumeLayout(false);
            this.ResumeLayout(false);
            this.PerformLayout();
        }
    }
}
