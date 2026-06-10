param(
    [string]$FileName         = "File",
    [int]   $AutoCloseSeconds = 6
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$form = New-Object System.Windows.Forms.Form
$form.Text            = "Risknox Pulse"
$form.Size            = New-Object System.Drawing.Size(380, 110)
$form.StartPosition   = "Manual"
$form.FormBorderStyle = "None"
$form.TopMost         = $true
$form.BackColor       = [System.Drawing.Color]::FromArgb(28, 28, 28)

# Position bottom-right corner above taskbar
$screen    = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
$form.Left = $screen.Right  - $form.Width  - 12
$form.Top  = $screen.Bottom - $form.Height - 12

# Green left accent bar
$accent = New-Object System.Windows.Forms.Panel
$accent.Size      = New-Object System.Drawing.Size(5, 110)
$accent.Location  = New-Object System.Drawing.Point(0, 0)
$accent.BackColor = [System.Drawing.Color]::FromArgb(40, 167, 69)
$form.Controls.Add($accent)

# App label
$lblApp = New-Object System.Windows.Forms.Label
$lblApp.Text      = "Risknox Pulse"
$lblApp.Font      = New-Object System.Drawing.Font("Segoe UI", 8, [System.Drawing.FontStyle]::Bold)
$lblApp.ForeColor = [System.Drawing.Color]::FromArgb(40, 167, 69)
$lblApp.AutoSize  = $true
$lblApp.Location  = New-Object System.Drawing.Point(16, 10)
$form.Controls.Add($lblApp)

# Main message
$lblMsg = New-Object System.Windows.Forms.Label
$lblMsg.Text      = "Scan Complete - No Threats Found"
$lblMsg.Font      = New-Object System.Drawing.Font("Segoe UI", 10, [System.Drawing.FontStyle]::Bold)
$lblMsg.ForeColor = [System.Drawing.Color]::White
$lblMsg.AutoSize  = $true
$lblMsg.Location  = New-Object System.Drawing.Point(16, 32)
$form.Controls.Add($lblMsg)

# Filename sub-label
$short = if ($FileName.Length -gt 45) { $FileName.Substring(0, 42) + "..." } else { $FileName }
$lblFile = New-Object System.Windows.Forms.Label
$lblFile.Text      = $short
$lblFile.Font      = New-Object System.Drawing.Font("Segoe UI", 8)
$lblFile.ForeColor = [System.Drawing.Color]::FromArgb(180, 180, 180)
$lblFile.AutoSize  = $true
$lblFile.Location  = New-Object System.Drawing.Point(16, 60)
$form.Controls.Add($lblFile)

# Close button
$btnClose = New-Object System.Windows.Forms.Button
$btnClose.Text      = "x"
$btnClose.Font      = New-Object System.Drawing.Font("Segoe UI", 9)
$btnClose.ForeColor = [System.Drawing.Color]::FromArgb(160, 160, 160)
$btnClose.BackColor = [System.Drawing.Color]::FromArgb(28, 28, 28)
$btnClose.FlatStyle = "Flat"
$btnClose.FlatAppearance.BorderSize = 0
$btnClose.Size      = New-Object System.Drawing.Size(24, 24)
$btnClose.Location  = New-Object System.Drawing.Point(348, 4)
$btnClose.Add_Click({ $form.Close() })
$form.Controls.Add($btnClose)

# Auto-close timer started after form loads (message pump must be running)
$form.Add_Load({
    $script:closeTimer = New-Object System.Windows.Forms.Timer
    $script:closeTimer.Interval = $AutoCloseSeconds * 1000
    $script:closeTimer.Add_Tick({
        $script:closeTimer.Stop()
        $form.Close()
    })
    $script:closeTimer.Start()
})

$form.Add_FormClosed({
    if ($script:closeTimer) { $script:closeTimer.Stop() }
})

[System.Windows.Forms.Application]::Run($form)
