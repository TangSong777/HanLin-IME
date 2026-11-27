using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows;
using System.Windows.Media;
using Drawing = System.Drawing;
using Drawing2D = System.Drawing.Drawing2D;
using Forms = System.Windows.Forms;

namespace HanLin.Tools
{

    internal sealed class TrayMenu : Forms.ContextMenuStrip
    {
        private readonly Drawing.Font menuFont = new Drawing.Font("Microsoft YaHei UI", 9f);
        private readonly Drawing.Bitmap imageSlot = new Drawing.Bitmap(16, 16);

        private readonly Forms.Timer fade = new Forms.Timer { Interval = 15 };
        private readonly Stopwatch opening = new Stopwatch();
        private const int MouseLowLevelHook = 14;
        private readonly MouseHook mouseCallback;
        private IntPtr mouseHook;
        private bool dismissPosted;
        private bool animateOpening;

        public TrayMenu()
        {
            mouseCallback = ObserveOutsideClick;
            Font = menuFont;
            Renderer = new TrayRenderer();
            Padding = new Forms.Padding(3, 4, 3, 4);
            Cursor = Forms.Cursors.Arrow;
            ShowItemToolTips = false;
            fade.Tick += (sender, args) =>
            {
                double t = Math.Min(1, opening.Elapsed.TotalMilliseconds / 110);
                Opacity = 1 - 0.15 * Math.Pow(1 - t, 3);
                if (t >= 1) fade.Stop();
            };
        }

        public Forms.ToolStripMenuItem AddAction(string text, string icon, System.Action action, string shortcut = null)
        {
            var item = new Forms.ToolStripMenuItem(text, imageSlot)
            {
                Tag = icon,
                ShowShortcutKeys = shortcut != null,
                ShortcutKeyDisplayString = shortcut ?? ""
            };
            item.Click += (sender, args) => action();
            Items.Add(item);
            return item;
        }

        protected override void OnOpening(CancelEventArgs e)
        {
            base.OnOpening(e);
            if (e.Cancel) return;
            animateOpening = !Forms.SystemInformation.HighContrast && Forms.SystemInformation.IsMenuAnimationEnabled &&
                SystemParameters.ClientAreaAnimation && SystemParameters.UIEffects;
            dismissPosted = false;
            // The tray launches a separate process; its popup may never own the
            // foreground, so a click in another process need not move our focus.
            mouseHook = SetWindowsHookExW(MouseLowLevelHook, mouseCallback, GetModuleHandleW(null), 0);
        }

        protected override void OnOpened(EventArgs e)
        {
            base.OnOpened(e);
            if (mouseHook == IntPtr.Zero) SetForegroundWindow(Handle);
            if (animateOpening)
            {
                Opacity = 0.85;
                opening.Restart();
                fade.Start();
            }
        }

        protected override void OnClosed(Forms.ToolStripDropDownClosedEventArgs e)
        {
            StopWatchingMouse();
            fade.Stop();
            Opacity = 1;
            base.OnClosed(e);
        }

        private IntPtr ObserveOutsideClick(int code, IntPtr message, IntPtr data)
        {
            if (code == 0 && !dismissPosted && Visible)
            {
                int kind = message.ToInt32();
                if (kind == 0x0201 || kind == 0x0204 || kind == 0x0207 || kind == 0x020B)
                {
                    // MSLLHOOKSTRUCT starts with a POINT in screen coordinates.
                    var point = new Drawing.Point(Marshal.ReadInt32(data), Marshal.ReadInt32(data, sizeof(int)));
                    if (!Bounds.Contains(point))
                    {
                        dismissPosted = true;
                        BeginInvoke(new Forms.MethodInvoker(() =>
                        {
                            if (Visible) Close(Forms.ToolStripDropDownCloseReason.AppClicked);
                        }));
                    }
                }
            }
            return CallNextHookEx(IntPtr.Zero, code, message, data);
        }

        private void StopWatchingMouse()
        {
            if (mouseHook == IntPtr.Zero) return;
            UnhookWindowsHookEx(mouseHook);
            mouseHook = IntPtr.Zero;
        }

        private delegate IntPtr MouseHook(int code, IntPtr message, IntPtr data);

        [DllImport("user32.dll", ExactSpelling = true)]
        private static extern IntPtr SetWindowsHookExW(int kind, MouseHook callback, IntPtr module, uint threadId);

        [DllImport("user32.dll")]
        private static extern IntPtr CallNextHookEx(IntPtr hook, int code, IntPtr message, IntPtr data);

        [DllImport("user32.dll")]
        private static extern bool UnhookWindowsHookEx(IntPtr hook);

        [DllImport("kernel32.dll", ExactSpelling = true, CharSet = CharSet.Unicode)]
        private static extern IntPtr GetModuleHandleW(string name);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr window);

        protected override void OnSizeChanged(EventArgs e)
        {
            base.OnSizeChanged(e);
            if (ClientSize.Width < 2 || ClientSize.Height < 2)
                return;
            var previous = Region;
            using (var outline = Rounded(ClientRectangle, 8 * DpiScale(this)))
                Region = new Drawing.Region(outline);
            if (previous != null) previous.Dispose();
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) StopWatchingMouse();
            base.Dispose(disposing);
            if (disposing)
            {
                fade.Dispose();
                imageSlot.Dispose();
                menuFont.Dispose();
            }
        }

        private static float DpiScale(Forms.ToolStrip strip)
        {
            if (strip == null || !strip.IsHandleCreated) return 1f;
            using (var graphics = strip.CreateGraphics()) return graphics.DpiX / 96f;
        }

        private static Drawing2D.GraphicsPath Rounded(Drawing.RectangleF bounds, float radius)
        {
            var path = new Drawing2D.GraphicsPath();
            float corner = Math.Min(radius, Math.Min(bounds.Width, bounds.Height) / 2);
            float diameter = corner * 2;
            path.AddArc(bounds.Left, bounds.Top, diameter, diameter, 180, 90);
            path.AddArc(bounds.Right - diameter, bounds.Top, diameter, diameter, 270, 90);
            path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
            path.AddArc(bounds.Left, bounds.Bottom - diameter, diameter, diameter, 90, 90);
            path.CloseFigure();
            return path;
        }

        private sealed class TrayRenderer : Forms.ToolStripRenderer
        {
            private static Drawing.Color Color(string resource, Drawing.Color system)
            {
                if (Forms.SystemInformation.HighContrast)
                    return system;
                switch (resource)
                {
                    case "SurfaceBrush": return Theme.Paper;
                    case "EdgeBrush": return Theme.Edge;
                    case "LineBrush": return Theme.Line;
                    case "PressBrush": return Theme.Press;
                    case "HoverBrush": return Theme.Hover;
                    case "MutedBrush": return Theme.Muted;
                    case "OverdueBrush": return Theme.Alert;
                    default: return Theme.Ink;
                }
            }

            protected override void OnRenderToolStripBackground(Forms.ToolStripRenderEventArgs e)
            {
                using (var fill = new Drawing.SolidBrush(Color("SurfaceBrush", Drawing.SystemColors.Menu)))
                    e.Graphics.FillRectangle(fill, e.ToolStrip.ClientRectangle);
            }

            protected override void OnRenderToolStripBorder(Forms.ToolStripRenderEventArgs e)
            {
                var bounds = e.ToolStrip.ClientRectangle;
                if (bounds.Width < 2 || bounds.Height < 2)
                    return;
                e.Graphics.SmoothingMode = Drawing2D.SmoothingMode.AntiAlias;
                using (var outline = Rounded(new Drawing.RectangleF(0.5f, 0.5f, bounds.Width - 1, bounds.Height - 1),
                    8 * DpiScale(e.ToolStrip)))
                using (var pen = new Drawing.Pen(Color("EdgeBrush", Drawing.SystemColors.WindowFrame)))
                    e.Graphics.DrawPath(pen, outline);
            }

            protected override void OnRenderMenuItemBackground(Forms.ToolStripItemRenderEventArgs e)
            {
                if (!e.Item.Enabled || (!e.Item.Selected && !e.Item.Pressed))
                    return;
                float scale = DpiScale(e.ToolStrip);
                var bounds = new Drawing.RectangleF(2 * scale, scale, e.Item.Width - 4 * scale, e.Item.Height - 2 * scale);
                if (bounds.Width <= 0 || bounds.Height <= 0)
                    return;
                e.Graphics.SmoothingMode = Drawing2D.SmoothingMode.AntiAlias;
                using (var highlight = Rounded(bounds, 5 * scale))
                using (var fill = new Drawing.SolidBrush(Color(e.Item.Pressed ? "PressBrush" : "HoverBrush",
                    Drawing.SystemColors.Highlight)))
                    e.Graphics.FillPath(fill, highlight);
            }

            protected override void OnRenderItemText(Forms.ToolStripItemTextRenderEventArgs e)
            {
                var item = e.Item as Forms.ToolStripMenuItem;
                bool shortcut = item != null && e.Text == item.ShortcutKeyDisplayString;
                string color = shortcut || !e.Item.Enabled ? "MutedBrush" : Equals(e.Item.Tag, "exit") ? "OverdueBrush" : "InkBrush";
                e.TextColor = Color(color, ItemSystemColor(e.Item));
                base.OnRenderItemText(e);
            }

            protected override void OnRenderItemImage(Forms.ToolStripItemImageRenderEventArgs e)
            {
                var bounds = e.ImageRectangle;
                var icon = e.Item.Tag as string;
                string data;
                if (icon == null || !Glyphs.TryGetValue(icon, out data) || bounds.Width <= 0 || bounds.Height <= 0)
                    return;
                float scale = Math.Min(bounds.Width, bounds.Height) / 16f;
                float left = bounds.Left + bounds.Width / 2f - 7 * scale;
                float top = bounds.Top + bounds.Height / 2f - 7 * scale;
                Func<Point, Drawing.PointF> place = point =>
                    new Drawing.PointF(left + (float)point.X * scale, top + (float)point.Y * scale);
                using (var path = new Drawing2D.GraphicsPath())
                {
                    foreach (var figure in Geometry.Parse(data).GetFlattenedPathGeometry().Figures)
                    {
                        var points = new List<Drawing.PointF> { place(figure.StartPoint) };
                        foreach (var segment in figure.Segments)
                        {
                            var line = segment as LineSegment;
                            var polyline = segment as PolyLineSegment;
                            if (line != null) points.Add(place(line.Point));
                            if (polyline != null)
                                foreach (var point in polyline.Points) points.Add(place(point));
                        }
                        if (points.Count < 2) continue;
                        path.StartFigure();
                        path.AddLines(points.ToArray());
                        if (figure.IsClosed) path.CloseFigure();
                    }
                    using (var pen = new Drawing.Pen(Color("MutedBrush", ItemSystemColor(e.Item)), 1.25f * scale))
                    {
                        pen.StartCap = pen.EndCap = Drawing2D.LineCap.Round;
                        pen.LineJoin = Drawing2D.LineJoin.Round;
                        e.Graphics.SmoothingMode = Drawing2D.SmoothingMode.AntiAlias;
                        e.Graphics.DrawPath(pen, path);
                    }
                }
            }

            private static readonly Dictionary<string, string> Glyphs = new Dictionary<string, string>
            {
                { "settings", "M 1,3.5 H 4 M 7,3.5 H 13 M 4,2 H 7 V 5 H 4 Z M 1,10.5 H 7 M 10,10.5 H 13 M 7,9 H 10 V 12 H 7 Z" },
                { "file", "M 3,1.4 H 8.2 L 11,4.2 V 12.6 H 3 Z M 8.2,1.4 V 4.2 H 11 M 5.2,7 H 8.8 M 5.2,9.5 H 8.8" },
                { "stats", "M 2,2 V 12 H 12 M 5,9 V 7 M 8,9 V 4 M 11,9 V 6" },
                { "words", "M 2,2 H 11.5 V 11.5 H 3.5 Q 2,11.5 2,10 V 2 M 4.5,2 V 11.5 M 6.5,5 H 9.5 M 6.5,8 H 9.5" },
                { "today", "M 12,7 A 5,5 0 1 1 2,7 A 5,5 0 1 1 12,7 M 7,4 V 7 L 9,8" },
            };

            protected override void OnRenderSeparator(Forms.ToolStripSeparatorRenderEventArgs e)
            {
                int inset = (int)Math.Round(7 * DpiScale(e.ToolStrip));
                int y = e.Item.Height / 2;
                if (e.Item.Width <= 2 * inset)
                    return;
                using (var pen = new Drawing.Pen(Color("LineBrush", Drawing.SystemColors.GrayText)))
                    e.Graphics.DrawLine(pen, inset, y, e.Item.Width - inset, y);
            }

            private static Drawing.Color ItemSystemColor(Forms.ToolStripItem item)
            {
                return !item.Enabled ? Drawing.SystemColors.GrayText :
                    item.Selected || item.Pressed ? Drawing.SystemColors.HighlightText : Drawing.SystemColors.MenuText;
            }
        }
    }
}
