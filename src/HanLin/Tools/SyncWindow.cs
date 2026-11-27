using System;
using System.Drawing;
using System.Globalization;
using System.Text.RegularExpressions;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace HanLin.Tools
{
    internal sealed class SyncWindow : HanLinForm
    {
        private readonly HostActions host;
        private readonly InkField server = Theme.Field("服务器地址");
        private readonly InkField code = Theme.Field("六位动态验证码");
        private readonly Label status = Theme.Label("", Theme.Small, Theme.Ink);
        private readonly InkButton primary = new InkButton("立即同步", true) { Size = new Size(106, 32), Dock = DockStyle.None };
        private readonly TableLayoutPanel pairing;
        private bool paired, busy;

        internal SyncWindow(HostActions host) : base("云同步", new Size(500, 344))
        {
            this.host = host;
            Text = "寒淋输入法 · 云同步";
            var state = host.ReadCloudSyncState();
            object configuredServer = null;
            if (state != null) state.TryGetValue("server", out configuredServer);
            string saved = configuredServer as string;
            paired = !string.IsNullOrWhiteSpace(saved);
            server.Text = saved ?? "https://sync.yanghanlin.cn";
            code.MaskInput();
            Body.Padding = new Padding(24, 16, 24, 18);
            var layout = Theme.Layout(1, 48, 98, 0, 36);
            layout.Controls.Add(status, 0, 0);
            pairing = Theme.Layout(1, 18, 29, 18, 29);
            pairing.Controls.Add(Theme.Label("服务器地址", Theme.Small, Theme.Muted), 0, 0);
            pairing.Controls.Add(server, 0, 1);
            pairing.Controls.Add(Theme.Label("Authenticator 六位动态验证码", Theme.Small, Theme.Muted), 0, 2);
            pairing.Controls.Add(code, 0, 3);
            layout.Controls.Add(pairing, 0, 1);
            var hint = Theme.Label("自动同步每天一次，可在设置关闭。合并词频前会备份本机词库。",
                Theme.Small, Theme.Muted);
            layout.Controls.Add(hint, 0, 2);
            var footer = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            primary.Click += (sender, args) => ActivateAction();
            AcceptButton = primary;
            footer.Controls.Add(primary);
            layout.Controls.Add(footer, 0, 3);
            Body.Controls.Add(layout);
            SetPairing(paired);
            if (paired)
            {
                object value;
                DateTimeOffset lastSync;
                if (state.TryGetValue("pendingMerge", out value) && Equals(value, true))
                    status.Text = "词频待合并。请先使用寒淋输入法输入并选词，再重试同步。";
                else if (state.TryGetValue("lastSync", out value) &&
                    DateTimeOffset.TryParse(value as string, CultureInfo.InvariantCulture, DateTimeStyles.None, out lastSync))
                    status.Text = "上次同步：" + lastSync.ToLocalTime().ToString("yyyy-MM-dd HH:mm:ss");
                else
                    status.Text = "已配对，等待首次同步。";
            }
            Shown += (sender, args) =>
            {
                var job = host.CloudSyncJob;
                if (job != null && !job.IsCompleted) Observe(job, host.CloudSyncOperation);
                else if (!paired) code.Focus();
            };
        }

        private void SetPairing(bool enabled)
        {
            paired = enabled;
            primary.Text = paired ? "立即同步" : "配对";
            pairing.Visible = !paired;
            var layout = (TableLayoutPanel)pairing.Parent;
            layout.RowStyles[1].Height = paired ? 0 : 98;
            if (!paired) status.Text = "输入手机 Authenticator 上显示的当前验证码。";
        }

        private void ActivateAction()
        {
            if (paired) { Run("sync"); return; }
            string secret = code.Text.Trim();
            if (!Regex.IsMatch(secret, "^(?:[0-9]{6}|[A-Za-z0-9-]{8,32})$"))
            {
                status.Text = "请输入 Authenticator 的六位动态验证码。";
                return;
            }
            code.Text = "";
            Run("pair", secret);
        }

        private void Run(string operation, string secret = null)
        {
            if (busy) return;
            host.StartCloudSync(operation, server.Text.Trim(), secret);
            Observe(host.CloudSyncJob, host.CloudSyncOperation);
        }

        private async void Observe(Task<Tuple<int, string>> job, string operation)
        {
            busy = true;
            primary.Enabled = pairing.Enabled = false;
            status.Text = operation == "pair" ? "正在配对，关闭窗口后会继续。" : "正在同步词频，关闭窗口后会继续。";
            try
            {
                var result = await job;
                if (IsDisposed || Disposing || !Visible) return;
                if (result.Item1 != 0)
                {
                    if (result.Item2.Contains("重新配对")) SetPairing(false);
                    status.Text = result.Item2;
                    return;
                }
                if (operation == "pair")
                {
                    SetPairing(true);
                    status.Text = "配对成功。可以开始同步。";
                    return;
                }
                var lines = result.Item2.Split(new[] { "\r\n", "\n" }, StringSplitOptions.RemoveEmptyEntries);
                var info = Array.Find(lines, line => line.Contains("等待合并") || line.Contains("词频待合并"));
                status.Text = info ?? "词频同步完成。";
            }
            catch (Exception ex)
            {
                if (!IsDisposed && !Disposing && Visible)
                    status.Text = "无法运行同步工具。\r\n" + ex.Message;
            }
            finally
            {
                busy = false;
                if (!IsDisposed && !Disposing && Visible)
                {
                    primary.Enabled = pairing.Enabled = true;
                    if (!paired) code.Focus();
                }
            }
        }
    }
}
