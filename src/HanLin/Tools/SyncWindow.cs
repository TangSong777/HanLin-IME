using System;
using System.Drawing;
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
        private readonly Label status = Theme.Label("正在检查配对…", Theme.Small, Theme.Ink);
        private readonly InkButton primary = new InkButton("立即同步", true) { Size = new Size(106, 32), Dock = DockStyle.None };
        private readonly TableLayoutPanel pairing;
        private bool paired, busy;

        internal SyncWindow(HostActions host) : base("云同步", new Size(500, 300))
        {
            this.host = host;
            Text = "寒淋输入法 · 云同步";
            string saved = HostActions.ConfiguredSyncServer(host.DirectoryPath);
            paired = saved != null;
            server.Text = saved ?? "https://sync.yanghanlin.cn";
            code.MaskInput();
            Body.Padding = new Padding(22, 15, 22, 14);
            var layout = Theme.Layout(1, 48, 98, 0, 36);
            layout.Controls.Add(status, 0, 0);
            pairing = Theme.Layout(1, 18, 29, 18, 29);
            pairing.Controls.Add(Theme.Label("服务器地址", Theme.Small, Theme.Muted), 0, 0);
            pairing.Controls.Add(server, 0, 1);
            pairing.Controls.Add(Theme.Label("Authenticator 六位动态验证码", Theme.Small, Theme.Muted), 0, 2);
            pairing.Controls.Add(code, 0, 3);
            layout.Controls.Add(pairing, 0, 1);
            var hint = Theme.Label("合并词频前会备份本机词库。服务器可以读取同步内容，换电脑后请重新配对。",
                Theme.Small, Theme.Muted);
            layout.Controls.Add(hint, 0, 2);
            var footer = new FlowLayoutPanel { Dock = DockStyle.Fill, FlowDirection = FlowDirection.RightToLeft };
            primary.Click += (sender, args) => ActivateAction();
            footer.Controls.Add(primary);
            layout.Controls.Add(footer, 0, 3);
            Body.Controls.Add(layout);
            SetPairing(paired);
            Shown += (sender, args) => { if (paired) Run("status"); };
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

        protected override void OnFormClosing(FormClosingEventArgs e)
        {
            if (busy && e.CloseReason == CloseReason.UserClosing) e.Cancel = true;
            base.OnFormClosing(e);
        }

        private async void Run(string operation, string secret = null)
        {
            if (busy) return;
            busy = true;
            primary.Enabled = pairing.Enabled = false;
            string endpoint = server.Text.Trim();
            status.Text = "正在处理…";
            try
            {
                var result = await Task.Run(() => host.CloudSync(operation, endpoint, Environment.MachineName, secret, null));
                if (result.Item1 != 0)
                {
                    status.Text = result.Item2;
                    if (result.Item2.Contains("重新配对")) SetPairing(false);
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
                status.Text = info ?? (operation == "sync" ? "词频同步完成。"
                    : Array.Find(lines, line => line.StartsWith("上次同步：", StringComparison.Ordinal)) ?? "已配对，等待首次同步。");
            }
            catch (Exception ex)
            {
                status.Text = "无法运行同步工具。请确认已安装 PowerShell 7。\r\n" + ex.Message;
            }
            finally
            {
                busy = false;
                primary.Enabled = pairing.Enabled = true;
            }
        }
    }
}
