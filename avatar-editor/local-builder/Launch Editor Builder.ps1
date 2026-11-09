# SPDX-License-Identifier: GPL-3.0-or-later
# Local Windows UI. Does not download, upload, install, or contact a console.
[CmdletBinding()]
param([switch]$LibraryOnly, [string]$PreviewPath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-NativeArgument([AllowEmptyString()][string]$Value) {
    # CommandLineToArgvW/CRT quoting. No cmd.exe or shell interpolation.
    if ($Value -match '^[A-Za-z0-9_./:=+\-]+$') { return $Value }
    $result = New-Object System.Text.StringBuilder
    [void]$result.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') { $slashes++; continue }
        if ($character -eq '"') {
            [void]$result.Append(('\' * (2 * $slashes + 1)))
        } else {
            [void]$result.Append(('\' * $slashes))
        }
        [void]$result.Append($character)
        $slashes = 0
    }
    [void]$result.Append(('\' * (2 * $slashes)))
    [void]$result.Append('"')
    return $result.ToString()
}

function New-BuildPayload([string]$App, [string]$Inputs, [string]$Dependencies, [string]$Work) {
    $payload = @{ app = $App; inputs = $Inputs; dependencies = $Dependencies; work = $Work } | ConvertTo-Json -Compress
    return [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($payload))
}

# This controller owns exactly one new process group. Cancellation is sent on its
# private stdin pipe; it never kills a WSL distribution or unrelated process.
$script:Controller = @'
import base64, json, os, signal, subprocess, sys, threading, time
if sys.version_info < (3, 11):
    sys.exit('Python 3.11 or newer is required inside your default WSL distribution.')
cancel = threading.Event()
def control():
    try:
        if sys.stdin.readline().strip() == 'CANCEL':
            cancel.set()
    except OSError:
        pass
threading.Thread(target=control, daemon=True).start()
try:
    supplied = json.loads(base64.b64decode(sys.argv[1]).decode('utf-8'))
    paths = {}
    for key, value in supplied.items():
        result = subprocess.run(['wslpath', '-a', '-u', value], check=True,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                text=True, encoding='utf-8')
        paths[key] = result.stdout.rstrip('\r\n')
    if cancel.is_set():
        sys.exit(130)
    command = [sys.executable, '-u', os.path.join(paths['app'], 'builder.py')]
    for key in ('inputs', 'dependencies', 'work'):
        command.extend(['--' + key, paths[key]])
    child = subprocess.Popen(command, stdin=subprocess.DEVNULL, start_new_session=True)
    deadline = None
    while child.poll() is None:
        if cancel.is_set():
            if deadline is None:
                print('Cancelling the owned build process group...', flush=True)
                os.killpg(child.pid, signal.SIGTERM)
                deadline = time.monotonic() + 5
            elif time.monotonic() >= deadline:
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                break
        time.sleep(0.1)
    code = child.wait()
    if cancel.is_set():
        # Descendants can outlive a quickly exiting parent; reap only its group.
        try:
            os.killpg(child.pid, signal.SIGKILL)
        except ProcessLookupError:
            pass
        sys.exit(130)
    sys.exit(code if code >= 0 else 128 - code)
except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
    print('Unable to start the local build: ' + str(error), file=sys.stderr, flush=True)
    sys.exit(2)
'@

function New-StartInfo([string]$Executable, [string[]]$Arguments) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $Executable
    $info.Arguments = (($Arguments | ForEach-Object { ConvertTo-NativeArgument $_ }) -join ' ')
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.RedirectStandardInput = $true
    $info.StandardOutputEncoding = New-Object Text.UTF8Encoding($false)
    $info.StandardErrorEncoding = New-Object Text.UTF8Encoding($false)
    return $info
}

if ($LibraryOnly) { return }

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[Windows.Forms.Application]::EnableVisualStyles()
$script:BuildProcess = $null
$script:OutRead = $null
$script:ErrorRead = $null
$script:Cancelled = $false
$script:CloseWhenDone = $false
$script:AppDirectory = $PSScriptRoot

$form = New-Object Windows.Forms.Form
$form.Text = 'Build Avatar Editor'
$form.Size = New-Object Drawing.Size(870, 620)
$form.MinimumSize = New-Object Drawing.Size(720, 520)
$form.StartPosition = 'CenterScreen'
$form.Font = New-Object Drawing.Font('Segoe UI', 10)
$layout = New-Object Windows.Forms.TableLayoutPanel
$layout.Dock = 'Fill'
$layout.Padding = New-Object Windows.Forms.Padding(16)
$layout.ColumnCount = 3
$layout.RowCount = 8
[void]$layout.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('AutoSize')))
[void]$layout.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
[void]$layout.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('AutoSize')))
for ($row = 0; $row -lt 7; $row++) { [void]$layout.RowStyles.Add((New-Object Windows.Forms.RowStyle('AutoSize'))) }
[void]$layout.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 100)))
$form.Controls.Add($layout)
$heading = New-Object Windows.Forms.Label
$heading.Text = 'Build Avatar Editor'
$heading.Font = New-Object Drawing.Font('Segoe UI', 17, [Drawing.FontStyle]::Bold)
$heading.AutoSize = $true
$layout.Controls.Add($heading, 0, 0)
$layout.SetColumnSpan($heading, 3)
$intro = New-Object Windows.Forms.Label
$intro.Text = "Select an originals folder containing avatareditor.xex, xenonclatin.xtt and splash_360.png. Everything stays local. Requires WSL with Python 3.11+ and compatible build dependencies."
$intro.AutoSize = $true
$intro.MaximumSize = New-Object Drawing.Size(790, 0)
$intro.Margin = New-Object Windows.Forms.Padding(3, 6, 3, 14)
$layout.Controls.Add($intro, 0, 1)
$layout.SetColumnSpan($intro, 3)
$script:Fields = @()
$script:Browsers = @()
$names = @('Original files', 'Dependency sources', 'Workspace / output')
for ($index = 0; $index -lt 3; $index++) {
    $label = New-Object Windows.Forms.Label
    $label.Text = $names[$index]
    $label.AutoSize = $true
    $label.Anchor = 'Left'
    $entry = New-Object Windows.Forms.TextBox
    $entry.Dock = 'Fill'
    $button = New-Object Windows.Forms.Button
    $button.Text = 'Browse...'
    $button.AutoSize = $true
    $button.Tag = $entry
    $button.Add_Click({
        $dialog = New-Object Windows.Forms.FolderBrowserDialog
        $dialog.Description = 'Choose a folder on this computer'
        if ($dialog.ShowDialog($form) -eq [Windows.Forms.DialogResult]::OK) { $this.Tag.Text = $dialog.SelectedPath }
        $dialog.Dispose()
    })
    $layout.Controls.Add($label, 0, $index + 2)
    $layout.Controls.Add($entry, 1, $index + 2)
    $layout.Controls.Add($button, 2, $index + 2)
    $script:Fields += $entry
    $script:Browsers += $button
}
$controls = New-Object Windows.Forms.FlowLayoutPanel
$controls.AutoSize = $true
$controls.Dock = 'Fill'
$buildButton = New-Object Windows.Forms.Button
$buildButton.Text = 'Build'
$cancelButton = New-Object Windows.Forms.Button
$cancelButton.Text = 'Cancel'
$cancelButton.Enabled = $false
$controls.Controls.AddRange(@($buildButton, $cancelButton))
$layout.Controls.Add($controls, 0, 5)
$layout.SetColumnSpan($controls, 3)
$status = New-Object Windows.Forms.Label
$status.Text = 'Choose folders, then Build. Requirements are checked before compiling.'
$status.AutoSize = $true
$status.MaximumSize = New-Object Drawing.Size(790, 0)
$layout.Controls.Add($status, 0, 6)
$layout.SetColumnSpan($status, 3)
$log = New-Object Windows.Forms.TextBox
$log.Multiline = $true
$log.ReadOnly = $true
$log.ScrollBars = 'Both'
$log.WordWrap = $false
$log.Dock = 'Fill'
$log.Font = New-Object Drawing.Font('Consolas', 9)
$layout.Controls.Add($log, 0, 7)
$layout.SetColumnSpan($log, 3)

function Set-Busy([bool]$Busy) {
    foreach ($field in ($script:Fields + $script:Browsers)) { $field.Enabled = -not $Busy }
    $buildButton.Enabled = -not $Busy
    $cancelButton.Enabled = $Busy
}

function Request-Cancel {
    if ($null -eq $script:BuildProcess -or $script:BuildProcess.HasExited -or $script:Cancelled) { return }
    $script:Cancelled = $true
    $cancelButton.Enabled = $false
    $status.Text = 'Cancelling the local build; waiting for its processes to stop...'
    try {
        $script:BuildProcess.StandardInput.WriteLine('CANCEL')
        $script:BuildProcess.StandardInput.Flush()
    } catch {
        $log.AppendText("Could not send cancellation: $($_.Exception.Message)`r`n")
    }
}

$buildButton.Add_Click({
    try {
        foreach ($field in $script:Fields) { if ([string]::IsNullOrWhiteSpace($field.Text)) { throw 'Choose all three folders first.' } }
        for ($index = 0; $index -lt 2; $index++) {
            if (-not [IO.Directory]::Exists($script:Fields[$index].Text)) { throw ($names[$index] + ' must be an existing folder.') }
        }
        $backend = Join-Path $script:AppDirectory 'builder.py'
        if (-not [IO.File]::Exists($backend)) { throw 'The builder backend is missing. This download is incomplete.' }
        $wsl = Get-Command wsl.exe -ErrorAction SilentlyContinue
        if ($null -eq $wsl) { throw 'WSL was not found. Set up a WSL distribution with Python 3.11+ and the documented dependencies, then reopen this app.' }
        $payload = New-BuildPayload $script:AppDirectory $script:Fields[0].Text $script:Fields[1].Text $script:Fields[2].Text
        $arguments = @('--exec', 'python3', '-u', '-c', $script:Controller, $payload)
        $script:BuildProcess = New-Object Diagnostics.Process
        $script:BuildProcess.StartInfo = New-StartInfo $wsl.Source $arguments
        $script:Cancelled = $false
        [void]$script:BuildProcess.Start()
        $script:OutRead = $script:BuildProcess.StandardOutput.ReadLineAsync()
        $script:ErrorRead = $script:BuildProcess.StandardError.ReadLineAsync()
        $log.AppendText("Starting local build...`r`n")
        $status.Text = 'Building locally. The log reports the current stage.'
        Set-Busy $true
    } catch {
        if ($null -ne $script:BuildProcess) { $script:BuildProcess.Dispose(); $script:BuildProcess = $null }
        $status.Text = 'Build did not start.'
        $log.AppendText("$($_.Exception.Message)`r`n")
        [void][Windows.Forms.MessageBox]::Show($form, $_.Exception.Message, 'Cannot start build', 'OK', 'Error')
    }
})
$cancelButton.Add_Click({ Request-Cancel })
$timer = New-Object Windows.Forms.Timer
$timer.Interval = 100
$timer.Add_Tick({
    if ($null -eq $script:BuildProcess) { return }
    foreach ($stream in @('OutRead', 'ErrorRead')) {
        for ($count = 0; $count -lt 200; $count++) {
            $pending = Get-Variable -Name $stream -Scope Script -ValueOnly
            if ($null -eq $pending -or -not $pending.IsCompleted) { break }
            try { $line = $pending.GetAwaiter().GetResult() } catch { $line = $null }
            if ($null -eq $line) { Set-Variable -Name $stream -Scope Script -Value $null; break }
            $log.AppendText($line + "`r`n")
            $reader = if ($stream -eq 'OutRead') { $script:BuildProcess.StandardOutput } else { $script:BuildProcess.StandardError }
            Set-Variable -Name $stream -Scope Script -Value ($reader.ReadLineAsync())
        }
    }
    if ($script:BuildProcess.HasExited -and $null -eq $script:OutRead -and $null -eq $script:ErrorRead) {
        $code = $script:BuildProcess.ExitCode
        $script:BuildProcess.Dispose()
        $script:BuildProcess = $null
        Set-Busy $false
        if ($script:Cancelled) { $status.Text = 'Build cancelled. Partial outputs and logs were preserved.' }
        elseif ($code -eq 0) { $status.Text = 'Build completed. See the output path in the log.' }
        else { $status.Text = "Build failed (exit $code). See the local error above; no package is claimed." }
        if ($script:CloseWhenDone) { $form.Close() }
    }
})
$form.Add_FormClosing({
    if ($null -ne $script:BuildProcess) {
        $_.Cancel = $true
        if (-not $script:CloseWhenDone -and [Windows.Forms.MessageBox]::Show($form, 'Cancel the running build and close when it stops?', 'Cancel build?', 'YesNo', 'Question') -eq 'Yes') {
            $script:CloseWhenDone = $true
            Request-Cancel
        }
    }
})
if ($PreviewPath) {
    $form.ShowInTaskbar = $false
    $form.StartPosition = 'Manual'
    $form.Location = New-Object Drawing.Point(-30000, -30000)
    $form.Show()
    [Windows.Forms.Application]::DoEvents()
    $form.PerformLayout()
    $bitmap = New-Object Drawing.Bitmap($form.Width, $form.Height)
    try {
        $form.DrawToBitmap($bitmap, (New-Object Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
        $bitmap.Save([IO.Path]::GetFullPath($PreviewPath), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose(); $timer.Dispose(); $form.Dispose() }
    return
}
$timer.Start()
try { [void]$form.ShowDialog() } finally { $timer.Stop(); $timer.Dispose(); $form.Dispose() }
