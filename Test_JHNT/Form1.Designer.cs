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
            this.buttonPrewarm = new System.Windows.Forms.Button();
            this.buttonGenerateTrays = new System.Windows.Forms.Button();
            this.buttonPrescriptionMasks = new System.Windows.Forms.Button();
            this.buttonRun = new System.Windows.Forms.Button();
            this.buttonCreateExcel = new System.Windows.Forms.Button();
            this.buttonTrayImage = new System.Windows.Forms.Button();
            this.buttonTray1Polygon = new System.Windows.Forms.Button();
            this.pictureBoxTray1 = new System.Windows.Forms.PictureBox();
            this.pictureBoxTray2 = new System.Windows.Forms.PictureBox();
            this.flowLayoutPanelTray1Polygons = new System.Windows.Forms.FlowLayoutPanel();
            this.textBoxOutput = new System.Windows.Forms.TextBox();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBoxTray1)).BeginInit();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBoxTray2)).BeginInit();
            this.SuspendLayout();

            // buttonPrewarm
            this.buttonPrewarm.Location = new System.Drawing.Point(12, 68);
            this.buttonPrewarm.Name = "buttonPrewarm";
            this.buttonPrewarm.Size = new System.Drawing.Size(180, 50);
            this.buttonPrewarm.TabIndex = 1;
            this.buttonPrewarm.Text = "사전 준비";
            this.buttonPrewarm.UseVisualStyleBackColor = true;
            this.buttonPrewarm.Click += new System.EventHandler(this.buttonPrewarm_Click);

            // buttonGenerateTrays
            this.buttonGenerateTrays.Location = new System.Drawing.Point(12, 12);
            this.buttonGenerateTrays.Name = "buttonGenerateTrays";
            this.buttonGenerateTrays.Size = new System.Drawing.Size(180, 50);
            this.buttonGenerateTrays.TabIndex = 0;
            this.buttonGenerateTrays.Text = "트레이 생성";
            this.buttonGenerateTrays.UseVisualStyleBackColor = true;
            this.buttonGenerateTrays.Click += new System.EventHandler(this.buttonGenerateTrays_Click);

            // buttonPrescriptionMasks
            this.buttonPrescriptionMasks.Location = new System.Drawing.Point(198, 68);
            this.buttonPrescriptionMasks.Name = "buttonPrescriptionMasks";
            this.buttonPrescriptionMasks.Size = new System.Drawing.Size(180, 50);
            this.buttonPrescriptionMasks.TabIndex = 2;
            this.buttonPrescriptionMasks.Text = "처방 마스크 생성";
            this.buttonPrescriptionMasks.UseVisualStyleBackColor = true;
            this.buttonPrescriptionMasks.Click += new System.EventHandler(this.buttonPrescriptionMasks_Click);

            // buttonRun
            this.buttonRun.Location = new System.Drawing.Point(384, 68);
            this.buttonRun.Name = "buttonRun";
            this.buttonRun.Size = new System.Drawing.Size(180, 50);
            this.buttonRun.TabIndex = 3;
            this.buttonRun.Text = "추론 실행";
            this.buttonRun.UseVisualStyleBackColor = true;
            this.buttonRun.Click += new System.EventHandler(this.buttonRun_Click);

            // buttonCreateExcel
            this.buttonCreateExcel.Location = new System.Drawing.Point(570, 68);
            this.buttonCreateExcel.Name = "buttonCreateExcel";
            this.buttonCreateExcel.Size = new System.Drawing.Size(180, 50);
            this.buttonCreateExcel.TabIndex = 4;
            this.buttonCreateExcel.Text = "Excel 파일 생성";
            this.buttonCreateExcel.UseVisualStyleBackColor = true;
            this.buttonCreateExcel.Click += new System.EventHandler(this.buttonCreateExcel_Click);

            // buttonTrayImage
            this.buttonTrayImage.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.buttonTrayImage.Location = new System.Drawing.Point(760, 12);
            this.buttonTrayImage.Name = "buttonTrayImage";
            this.buttonTrayImage.Size = new System.Drawing.Size(100, 24);
            this.buttonTrayImage.TabIndex = 6;
            this.buttonTrayImage.Text = "tray 이미지";
            this.buttonTrayImage.UseVisualStyleBackColor = true;
            this.buttonTrayImage.Click += new System.EventHandler(this.buttonTrayImage_Click);

            // buttonTray1Polygon
            this.buttonTray1Polygon.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.buttonTray1Polygon.Location = new System.Drawing.Point(760, 320);
            this.buttonTray1Polygon.Name = "buttonTray1Polygon";
            this.buttonTray1Polygon.Size = new System.Drawing.Size(100, 24);
            this.buttonTray1Polygon.TabIndex = 9;
            this.buttonTray1Polygon.Text = "Tray1 polygon";
            this.buttonTray1Polygon.UseVisualStyleBackColor = true;
            this.buttonTray1Polygon.Click += new System.EventHandler(this.buttonTray1Polygon_Click);

            // flowLayoutPanelTray1Polygons
            this.flowLayoutPanelTray1Polygons.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.flowLayoutPanelTray1Polygons.AutoScroll = true;
            this.flowLayoutPanelTray1Polygons.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.flowLayoutPanelTray1Polygons.Location = new System.Drawing.Point(760, 346);
            this.flowLayoutPanelTray1Polygons.Name = "flowLayoutPanelTray1Polygons";
            this.flowLayoutPanelTray1Polygons.Size = new System.Drawing.Size(220, 92);
            this.flowLayoutPanelTray1Polygons.TabIndex = 10;

            // pictureBoxTray1
            this.pictureBoxTray1.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.pictureBoxTray1.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.pictureBoxTray1.Location = new System.Drawing.Point(760, 38);
            this.pictureBoxTray1.Name = "pictureBoxTray1";
            this.pictureBoxTray1.Size = new System.Drawing.Size(220, 135);
            this.pictureBoxTray1.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
            this.pictureBoxTray1.TabIndex = 7;
            this.pictureBoxTray1.TabStop = false;

            // pictureBoxTray2
            this.pictureBoxTray2.Anchor = ((System.Windows.Forms.AnchorStyles)((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Right)));
            this.pictureBoxTray2.BorderStyle = System.Windows.Forms.BorderStyle.FixedSingle;
            this.pictureBoxTray2.Location = new System.Drawing.Point(760, 179);
            this.pictureBoxTray2.Name = "pictureBoxTray2";
            this.pictureBoxTray2.Size = new System.Drawing.Size(220, 135);
            this.pictureBoxTray2.SizeMode = System.Windows.Forms.PictureBoxSizeMode.Zoom;
            this.pictureBoxTray2.TabIndex = 8;
            this.pictureBoxTray2.TabStop = false;

            // textBoxOutput
            this.textBoxOutput.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
            this.textBoxOutput.Location = new System.Drawing.Point(12, 124);
            this.textBoxOutput.Multiline = true;
            this.textBoxOutput.Name = "textBoxOutput";
            this.textBoxOutput.ReadOnly = true;
            this.textBoxOutput.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
            this.textBoxOutput.Size = new System.Drawing.Size(736, 314);
            this.textBoxOutput.TabIndex = 5;
            this.textBoxOutput.Font = new System.Drawing.Font("Consolas", 9F);

            // Form1
            this.AutoScaleDimensions = new System.Drawing.SizeF(7F, 12F);
            this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
            this.ClientSize = new System.Drawing.Size(1000, 450);
            this.Controls.Add(this.flowLayoutPanelTray1Polygons);
            this.Controls.Add(this.pictureBoxTray1);
            this.Controls.Add(this.pictureBoxTray2);
            this.Controls.Add(this.buttonTray1Polygon);
            this.Controls.Add(this.buttonTrayImage);
            this.Controls.Add(this.textBoxOutput);
            this.Controls.Add(this.buttonCreateExcel);
            this.Controls.Add(this.buttonRun);
            this.Controls.Add(this.buttonPrescriptionMasks);
            this.Controls.Add(this.buttonGenerateTrays);
            this.Controls.Add(this.buttonPrewarm);
            this.Name = "Form1";
            this.Text = "Prediction Processing";
            ((System.ComponentModel.ISupportInitialize)(this.pictureBoxTray1)).EndInit();
            ((System.ComponentModel.ISupportInitialize)(this.pictureBoxTray2)).EndInit();
            this.ResumeLayout(false);
            this.PerformLayout();
        }
    }
}
