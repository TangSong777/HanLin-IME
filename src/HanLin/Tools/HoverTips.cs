using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal sealed class HoverTips : IDisposable
    {
        private readonly Form owner;
        private readonly Timer delay = new Timer { Interval = 550 };
        private readonly Timer expire = new Timer { Interval = 6000 };
        private Control target;
        private string text;
        private Bubble bubble;
        private bool disposed;

        internal HoverTips(Form owner)
        {
            this.owner = owner;
            delay.Tick += (sender, args) => ShowCurrent();
            expire.Tick += (sender, args) => Hide();
            owner.Deactivate += (sender, args) => Hide();
            owner.VisibleChanged += (sender, args) => { if (!owner.Visible) Hide(); };
        }

        internal void Add(Control control, string hint)
        {
            control.MouseEnter += (sender, args) => Start(control, hint);
            control.MouseLeave += (sender, args) => HideIfCurrent(control);
            control.MouseDown += (sender, args) => HideIfCurrent(control);
            control.KeyDown += (sender, args) => HideIfCurrent(control);
            control.Disposed += (sender, args) => HideIfCurrent(control);
        }

        internal void AddGrid(DataGridView grid)
        {
            grid.ShowCellToolTips = false;
            grid.CellMouseEnter += (sender, args) =>
            {
                if (args.RowIndex < 0 || args.ColumnIndex < 0) return;
                var cell = grid[args.ColumnIndex, args.RowIndex];
                var style = cell.InheritedStyle;
                var value = Convert.ToString(cell.FormattedValue);
                if (!string.IsNullOrEmpty(value) &&
                    TextRenderer.MeasureText(value, style.Font ?? grid.Font).Width >
                    cell.Size.Width - style.Padding.Horizontal)
                    Start(grid, value);
            };
            grid.CellMouseLeave += (sender, args) => HideIfCurrent(grid);
            grid.CellMouseDown += (sender, args) => HideIfCurrent(grid);
            grid.Scroll += (sender, args) => HideIfCurrent(grid);
            grid.KeyDown += (sender, args) => HideIfCurrent(grid);
        }

        private void Start(Control control, string hint)
        {
            if (disposed) return;
            Hide();
            if (!control.Enabled) return;
            target = control;
            text = hint;
            delay.Start();
        }

        private void HideIfCurrent(Control control)
        {
            if (target == control) Hide();
        }

        private void ShowCurrent()
        {
            delay.Stop();
            var cursor = Control.MousePosition;
            if (target == null || target.IsDisposed || !target.Visible || !owner.Visible ||
                !target.RectangleToScreen(target.ClientRectangle).Contains(cursor))
            {
                Hide();
                return;
            }
            if (bubble == null) bubble = new Bubble();
            bubble.ShowAt(owner, text, cursor, target.DeviceDpi);
            expire.Start();
        }

        private void Hide()
        {
            if (disposed) return;
            delay.Stop();
            expire.Stop();
            target = null;
            if (bubble != null && !bubble.IsDisposed) bubble.Hide();
        }

        public void Dispose()
        {
            if (disposed) return;
            Hide();
            disposed = true;
            delay.Dispose();
            expire.Dispose();
            if (bubble != null) bubble.Dispose();
        }

        private sealed class Bubble : Form
        {
            private const TextFormatFlags TextFlags = TextFormatFlags.WordBreak | TextFormatFlags.NoPrefix | TextFormatFlags.NoPadding;
            private string message;
            private int dpi = 96;
            private readonly Color tipSurface = SystemInformation.HighContrast ? SystemColors.Info : Theme.Paper;
            private readonly Color tipText = SystemInformation.HighContrast ? SystemColors.InfoText : Theme.Ink;
            private readonly Color tipEdge = SystemInformation.HighContrast ? SystemColors.InfoText : Theme.Edge;

            internal Bubble()
            {
                AutoScaleMode = AutoScaleMode.None;
                FormBorderStyle = FormBorderStyle.None;
                ShowInTaskbar = false;
                StartPosition = FormStartPosition.Manual;
                BackColor = tipSurface;
                Font = Theme.Small;
                DoubleBuffered = true;
            }

            protected override bool ShowWithoutActivation { get { return true; } }

            protected override CreateParams CreateParams
            {
                get
                {
                    var parameters = base.CreateParams;
                    parameters.ExStyle |= 0x08000000 | 0x00000080 | 0x00000020;
                    return parameters;
                }
            }

            internal void ShowAt(Form owner, string text, Point cursor, int dpi)
            {
                message = text;
                this.dpi = dpi;
                int horizontal = Theme.Scale(10, dpi), vertical = Theme.Scale(7, dpi);
                var screen = Screen.FromPoint(cursor).WorkingArea;
                screen.Inflate(-4, -4);
                int maxText = Math.Max(1, Math.Min(Theme.Scale(280, dpi), screen.Width - 2 * horizontal));
                var measured = TextRenderer.MeasureText(text, Font, new Size(maxText, screen.Height), TextFlags);
                ClientSize = new Size(Math.Min(maxText, measured.Width) + 2 * horizontal,
                    Math.Min(screen.Height, measured.Height + 2 * vertical));
                var previous = Region;
                using (var shape = Theme.Rounded(ClientRectangle, Theme.Scale(7, dpi)))
                    Region = new Region(shape);
                if (previous != null) previous.Dispose();
                int x = Math.Max(screen.Left, Math.Min(cursor.X + Theme.Scale(12, dpi), screen.Right - Width));
                int below = cursor.Y + Theme.Scale(20, dpi);
                int y = below + Height <= screen.Bottom ? below : cursor.Y - Height - Theme.Scale(8, dpi);
                Location = new Point(x, Math.Max(screen.Top, Math.Min(y, screen.Bottom - Height)));
                if (!Visible) Show(owner);
                Invalidate();
            }

            protected override void OnPaint(PaintEventArgs e)
            {
                e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
                using (var shape = Theme.Rounded(new Rectangle(0, 0, Width - 1, Height - 1), Theme.Scale(7, dpi)))
                using (var edge = new Pen(tipEdge))
                    e.Graphics.DrawPath(edge, shape);
                var body = ClientRectangle;
                body.Inflate(-Theme.Scale(10, dpi), -Theme.Scale(7, dpi));
                TextRenderer.DrawText(e.Graphics, message, Font, body, tipText, TextFlags);
            }
        }
    }
}
