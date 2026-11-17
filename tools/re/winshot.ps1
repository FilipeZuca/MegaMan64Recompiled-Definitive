# Brings the game window forward, optionally sends keys, and saves a screenshot of it. Usage: winshot.ps1 out.png [keys...]
param([string]$Out, [string[]]$Keys, [int[]]$Click)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class W { [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(int f, int x, int y, int d, int e);
 public struct RECT { public int L, T, R, B; } }
"@
$p = Get-Process MegaMan64Recompiled | Select-Object -First 1
$h = $p.MainWindowHandle
[W]::ShowWindow($h, 9) | Out-Null; [W]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 600
$r0 = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r0) | Out-Null
for ($i = 0; $i + 1 -lt $Click.Count; $i += 2) { [W]::SetCursorPos($r0.L + $Click[$i], $r0.T + $Click[$i+1]) | Out-Null; Start-Sleep -Milliseconds 150; [W]::mouse_event(1,1,0,0,0); Start-Sleep -Milliseconds 100; [W]::mouse_event(2,0,0,0,0); Start-Sleep -Milliseconds 120; [W]::mouse_event(4,0,0,0,0); Start-Sleep -Milliseconds 900 }
foreach ($k in $Keys) { [System.Windows.Forms.SendKeys]::SendWait($k); Start-Sleep -Milliseconds 700 }
$r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null
$bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp); $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($Out); "saved $Out $($bmp.Width)x$($bmp.Height)"
