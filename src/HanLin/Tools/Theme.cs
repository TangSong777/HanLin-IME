using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal static class Theme
    {
        internal static readonly Color Paper = ColorFor("#FFFCF9", SystemColors.Window);
        internal static readonly Color Ink = ColorFor("#3C3C3C", SystemColors.WindowText);
        internal static readonly Color Muted = ColorFor("#736460", SystemColors.WindowText);
        internal static readonly Color Edge = ColorFor("#E2DFDD", SystemColors.WindowText);
        internal static readonly Color Line = ColorFor("#F1EFEC", SystemColors.WindowText);
        internal static readonly Color Accent = ColorFor("#E48887", SystemColors.Highlight);
        internal static readonly Color AccentInk = ColorFor("#000000", SystemColors.HighlightText);
        internal static readonly Color AccentHover = ColorFor("#D58180", SystemColors.Highlight);
        internal static readonly Color Tint = ColorFor("#FCEEEB", SystemColors.Window);
        internal static readonly Color Hover = ColorFor("#F9E0DE", SystemColors.Highlight);
        internal static readonly Color Press = ColorFor("#F5D1CF", SystemColors.Highlight);
        internal static readonly Color Header = ColorFor("#FAE8E6", SystemColors.Window);
        internal static readonly Color Alert = ColorFor("#93444D", SystemColors.WindowText);
        internal static readonly Font Body = new Font("Microsoft YaHei UI", 10);
        internal static readonly Font Small = new Font("Microsoft YaHei UI", 9);
        internal static readonly Font Heading = HeadingFont();
        internal static readonly Font Metric = new Font("Microsoft YaHei UI", 24, FontStyle.Bold);

        private static Color ColorFor(string hex, Color highContrast)
        {
            return SystemInformation.HighContrast ? highContrast : ColorTranslator.FromHtml(hex);
        }

        private static Font HeadingFont()
        {
            foreach (var name in new[] { "MF YueHei (Noncommercial)", "造字工房悦黑（非商用）",
                                       "MF YueHei (Noncommercial) Regular", "造字工房悦黑（非商用）常规体" })
            {
                var face = new Font(name, 13.5f);
                if (string.Equals(face.FontFamily.Name, name, StringComparison.OrdinalIgnoreCase)) return face;
                face.Dispose();
            }
            return new Font(Body.FontFamily, 13.5f, FontStyle.Bold);
        }

        internal static int Scale(int dip, int dpi) { return (int)Math.Round(dip * (double)dpi / 96); }

        internal static GraphicsPath Rounded(Rectangle border, int radius)
        {
            int diameter = Math.Min(radius * 2, Math.Min(border.Width, border.Height));
            var path = new GraphicsPath();
            if (diameter <= 0) { path.AddRectangle(border); return path; }
            path.AddArc(border.Left, border.Top, diameter, diameter, 180, 90);
            path.AddArc(border.Right - diameter, border.Top, diameter, diameter, 270, 90);
            path.AddArc(border.Right - diameter, border.Bottom - diameter, diameter, diameter, 0, 90);
            path.AddArc(border.Left, border.Bottom - diameter, diameter, diameter, 90, 90);
            path.CloseFigure();
            return path;
        }

        internal static Label Label(string text, Font font = null, Color? color = null)
        {
            return new Label {
                Text = text, Font = font ?? Body, ForeColor = color ?? Ink, BackColor = Color.Transparent,
                Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AutoEllipsis = true,
                Margin = Padding.Empty, UseMnemonic = false
            };
        }

        internal static TableLayoutPanel Layout(int columns, params float[] rows)
        {
            var panel = new TableLayoutPanel {
                ColumnCount = columns, RowCount = rows.Length, Dock = DockStyle.Fill,
                GrowStyle = TableLayoutPanelGrowStyle.FixedSize, Margin = Padding.Empty
            };
            for (int i = 0; i < columns; i++) panel.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f / columns));
            foreach (var height in rows)
                panel.RowStyles.Add(new RowStyle(height > 0 ? SizeType.Absolute : SizeType.Percent, height > 0 ? height : 100));
            return panel;
        }

        internal static InkField Field(string name) { return new InkField(name); }

        internal static DataGridView Grid()
        {
            var grid = new DataGridView {
                Dock = DockStyle.Fill, Margin = Padding.Empty, ReadOnly = true, MultiSelect = false,
                AllowUserToAddRows = false, AllowUserToDeleteRows = false, AllowUserToResizeRows = false,
                EnableHeadersVisualStyles = false, RowHeadersVisible = false, BackgroundColor = Paper,
                BorderStyle = BorderStyle.None, GridColor = Line, CellBorderStyle = DataGridViewCellBorderStyle.SingleHorizontal,
                SelectionMode = DataGridViewSelectionMode.FullRowSelect, AutoSizeColumnsMode = DataGridViewAutoSizeColumnsMode.Fill
            };
            grid.ColumnHeadersDefaultCellStyle = new DataGridViewCellStyle { BackColor = Tint, ForeColor = Ink, Font = Small };
            grid.DefaultCellStyle = new DataGridViewCellStyle {
                BackColor = Paper, ForeColor = Ink, SelectionBackColor = Tint, SelectionForeColor = Ink,
                Font = Small, Padding = new Padding(5, 0, 3, 0)
            };
            grid.AlternatingRowsDefaultCellStyle.BackColor = Paper;
            grid.RowTemplate.Height = 26;
            return grid;
        }
    }

    internal sealed class InkField : Panel
    {
        private readonly TextBox input;

        internal InkField(string name)
        {
            BackColor = Theme.Paper;
            Dock = DockStyle.Fill;
            Margin = new Padding(0, 3, 0, 4);
            input = new TextBox {
                AccessibleName = name, BorderStyle = BorderStyle.None, Font = Theme.Body,
                ForeColor = Theme.Ink, BackColor = Theme.Paper
            };
            input.TextChanged += (sender, args) => OnTextChanged(args);
            input.GotFocus += (sender, args) => Invalidate();
            input.LostFocus += (sender, args) => Invalidate();
            Controls.Add(input);
        }

        public override string Text
        {
            get { return input == null ? base.Text : input.Text; }
            set { if (input == null) base.Text = value; else input.Text = value; }
        }

        internal bool ReadOnly { set { input.ReadOnly = value; } }
        internal new bool Focus() { return input.Focus(); }

        protected override void OnLayout(LayoutEventArgs e)
        {
            base.OnLayout(e);
            if (input == null) return;
            int inset = Theme.Scale(8, DeviceDpi);
            input.SetBounds(inset, Math.Max(1, (Height - input.PreferredHeight) / 2),
                Math.Max(1, Width - 2 * inset), input.PreferredHeight);
        }

        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); Focus(); }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            if (Width < 2 || Height < 2) return;
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using (var frame = Theme.Rounded(new Rectangle(0, 0, Width - 1, Height - 1), Theme.Scale(5, DeviceDpi)))
            using (var stroke = new Pen(input.Focused ? Theme.Accent : Theme.Edge))
                e.Graphics.DrawPath(stroke, frame);
        }
    }

    internal sealed class InkButton : Button
    {
        private readonly bool primary;
        private readonly Action glyph;
        private bool hover;
        private bool pressed;

        internal InkButton(string text, bool primary = false, Action glyph = Action.None)
        {
            this.primary = primary;
            this.glyph = glyph;
            Text = text;
            AccessibleName = text;
            Font = glyph == Action.None ? Theme.Body : Theme.Small;
            FlatStyle = FlatStyle.Flat;
            FlatAppearance.BorderSize = 0;
            UseVisualStyleBackColor = false;
            Dock = DockStyle.Fill;
            Margin = new Padding(3);
            Cursor = Cursors.Hand;
        }

        protected override void OnMouseEnter(EventArgs e) { base.OnMouseEnter(e); hover = true; Invalidate(); }
        protected override void OnMouseLeave(EventArgs e) { base.OnMouseLeave(e); hover = pressed = false; Invalidate(); }
        protected override void OnMouseDown(MouseEventArgs e) { base.OnMouseDown(e); pressed = true; Invalidate(); }
        protected override void OnMouseUp(MouseEventArgs e) { base.OnMouseUp(e); pressed = false; Invalidate(); }
        protected override void OnGotFocus(EventArgs e) { base.OnGotFocus(e); Invalidate(); }
        protected override void OnLostFocus(EventArgs e) { base.OnLostFocus(e); Invalidate(); }

        protected override void OnPaint(PaintEventArgs e)
        {
            var graphics = e.Graphics;
            graphics.Clear(Parent == null ? Theme.Paper : Parent.BackColor);
            graphics.SmoothingMode = SmoothingMode.AntiAlias;
            bool menu = glyph != Action.None && glyph != Action.Stats;
            int inset = Theme.Scale(menu ? 2 : 1, DeviceDpi);
            var bounds = new Rectangle(inset, 1, Width - 1 - 2 * inset, Height - 3);
            if (bounds.Width <= 0 || bounds.Height <= 0) return;
            Color normal = Parent == null ? Theme.Paper : Parent.BackColor;
            Color background = !Enabled ? normal : primary ? (hover || pressed ? Theme.AccentHover : Theme.Accent)
                : pressed ? Theme.Press : hover ? Theme.Hover : normal;
            Color ink = !Enabled ? Theme.Muted : primary || (SystemInformation.HighContrast && (hover || pressed))
                ? Theme.AccentInk : Theme.Ink;
            using (var path = Theme.Rounded(bounds, Theme.Scale(menu ? 5 : 7, DeviceDpi)))
            using (var fill = new SolidBrush(background))
            using (var stroke = new Pen(Focused ? Theme.Accent : Theme.Edge))
            {
                graphics.FillPath(fill, path);
                if (Focused || (!primary && !menu && Enabled)) graphics.DrawPath(stroke, path);
            }
            if (menu)
            {
                int glyphSize = Theme.Scale(14, DeviceDpi);
                DrawGlyph(graphics, new Point(bounds.X + Theme.Scale(9, DeviceDpi),
                    bounds.Y + (bounds.Height - glyphSize) / 2), glyphSize, ink);
                int label = Theme.Scale(34, DeviceDpi);
                bounds.X += label;
                bounds.Width -= label;
            }
            TextRenderer.DrawText(graphics, Text, Font, bounds, ink,
                (menu ? TextFormatFlags.Left : TextFormatFlags.HorizontalCenter) |
                TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
        }

        private void DrawGlyph(Graphics graphics, Point point, int size, Color color)
        {
            var state = graphics.Save();
            graphics.TranslateTransform(point.X, point.Y);
            graphics.ScaleTransform(size / 14f, size / 14f);
            using (var pen = new Pen(color, 1.25f) { StartCap = LineCap.Round, EndCap = LineCap.Round, LineJoin = LineJoin.Round })
            {
                if (glyph == Action.Words)
                {
                    graphics.DrawRectangle(pen, 2, 1, 10, 12);
                    graphics.DrawLine(pen, 5, 5, 9, 5);
                    graphics.DrawLine(pen, 5, 8, 9, 8);
                }
                else if (glyph == Action.Settings)
                {
                    graphics.DrawEllipse(pen, 3, 3, 8, 8);
                    graphics.DrawEllipse(pen, 6, 6, 2, 2);
                    graphics.DrawLine(pen, 7, 1, 7, 3);
                    graphics.DrawLine(pen, 7, 11, 7, 13);
                    graphics.DrawLine(pen, 1, 7, 3, 7);
                    graphics.DrawLine(pen, 11, 7, 13, 7);
                }
            }
            graphics.Restore(state);
        }
    }

    internal class HanLinForm : Form
    {
        internal readonly Panel Body;

        internal HanLinForm(string caption, Size size, bool popup = false)
        {
            Text = caption;
            Font = Theme.Body;
            BackColor = Theme.Paper;
            ForeColor = Theme.Ink;
            AutoScaleDimensions = new SizeF(96, 96);
            AutoScaleMode = AutoScaleMode.Dpi;
            FormBorderStyle = FormBorderStyle.None;
            StartPosition = popup ? FormStartPosition.Manual : FormStartPosition.CenterScreen;
            ShowInTaskbar = !popup;
            TopMost = popup;
            ClientSize = size;
            Padding = new Padding(1);
            Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath);
            Body = new Panel { Dock = DockStyle.Fill, Padding = new Padding(popup ? 9 : 16) };
            Controls.Add(Body);
            if (!popup)
            {
                var title = new TitleStrip {
                    ColumnCount = 3, RowCount = 1, Dock = DockStyle.Top, Height = 46,
                    BackColor = Theme.Paper, Padding = new Padding(12, 3, 6, 3)
                };
                title.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 34));
                title.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
                title.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 36));
                title.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
                var mark = new PictureBox {
                    Image = Icon == null ? null : Icon.ToBitmap(), Dock = DockStyle.Fill,
                    SizeMode = PictureBoxSizeMode.Zoom, Margin = new Padding(3, 5, 5, 5)
                };
                var label = Theme.Label(caption, Theme.Heading);
                var close = new InkButton("×") { AccessibleName = "关闭", DialogResult = DialogResult.Cancel };
                close.Click += (sender, args) => Close();
                label.MouseDown += DragWindow;
                mark.MouseDown += DragWindow;
                title.MouseDown += DragWindow;
                title.Controls.Add(mark, 0, 0);
                title.Controls.Add(label, 1, 0);
                title.Controls.Add(close, 2, 0);
                Controls.Add(title);
            }
        }

        private void DragWindow(object sender, MouseEventArgs e)
        {
            if (e.Button != MouseButtons.Left) return;
            ReleaseCapture();
            SendMessage(Handle, 0x00A1, new IntPtr(2), IntPtr.Zero);
        }

        protected override void OnResize(EventArgs e)
        {
            base.OnResize(e);
            if (Width <= 1 || Height <= 1) return;
            using (var curve = Theme.Rounded(ClientRectangle, Theme.Scale(8, DeviceDpi)))
            {
                var old = Region;
                Region = new Region(curve);
                if (old != null) old.Dispose();
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            base.OnPaint(e);
            if (Width <= 1 || Height <= 1) return;
            e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
            using (var curve = Theme.Rounded(new Rectangle(0, 0, Width - 1, Height - 1), Theme.Scale(8, DeviceDpi)))
            using (var border = new Pen(Theme.Edge))
                e.Graphics.DrawPath(border, curve);
        }

        private sealed class TitleStrip : TableLayoutPanel
        {
            protected override void OnPaintBackground(PaintEventArgs e)
            {
                if (Width <= 1 || Height <= 1) return;
                using (var brush = new LinearGradientBrush(ClientRectangle, Theme.Header, Theme.Paper, 12f))
                {
                    brush.InterpolationColors = new ColorBlend {
                        Positions = new[] { 0f, .33f, 1f }, Colors = new[] { Theme.Header, Theme.Paper, Theme.Paper }
                    };
                    e.Graphics.FillRectangle(brush, ClientRectangle);
                }
                using (var pen = new Pen(Theme.Line)) e.Graphics.DrawLine(pen, 0, Height - 1, Width, Height - 1);
            }
        }

        protected override bool ProcessCmdKey(ref Message msg, Keys keys)
        {
            if (keys != Keys.Escape) return base.ProcessCmdKey(ref msg, keys);
            Close();
            return true;
        }

        [DllImport("user32.dll")]
        private static extern bool ReleaseCapture();
        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr window, int message, IntPtr word, IntPtr data);
    }
}
