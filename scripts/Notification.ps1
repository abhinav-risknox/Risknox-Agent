param(
    [string]$FileName        = "Unknown file",
    [string]$ThreatName      = "Unknown threat",
    [string]$FilePath        = "",
    [string]$SourceUrl       = "",
    [int]   $AutoCloseSeconds = 10,
    [string]$ResultFile      = ""
)

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$result = "DISMISSED"

$form = New-Object System.Windows.Forms.Form
$form.Text            = "Risknox Pulse - Threat Detected"
$form.Size            = New-Object System.Drawing.Size(500, 320)
$form.StartPosition   = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox     = $false
$form.MinimizeBox     = $false
$form.TopMost         = $true
$form.BackColor       = [System.Drawing.Color]::FromArgb(30, 30, 30)

# Header banner
$header = New-Object System.Windows.Forms.Panel
$header.Size      = New-Object System.Drawing.Size(500, 56)
$header.Location  = New-Object System.Drawing.Point(0, 0)
$header.BackColor = [System.Drawing.Color]::FromArgb(180, 30, 30)
$form.Controls.Add($header)

$lblTitle = New-Object System.Windows.Forms.Label
$lblTitle.Text      = "   !! THREAT DETECTED"
$lblTitle.Font      = New-Object System.Drawing.Font("Segoe UI", 14, [System.Drawing.FontStyle]::Bold)
$lblTitle.ForeColor = [System.Drawing.Color]::White
$lblTitle.AutoSize  = $false
$lblTitle.Size      = New-Object System.Drawing.Size(500, 56)
$lblTitle.Location  = New-Object System.Drawing.Point(0, 0)
$lblTitle.TextAlign = "MiddleLeft"
$header.Controls.Add($lblTitle)

# Detail rows
function Add-Row($parent, $label, $value, $y) {
    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text      = $label
    $lbl.Font      = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $lbl.ForeColor = [System.Drawing.Color]::FromArgb(160, 160, 160)
    $lbl.Size      = New-Object System.Drawing.Size(100, 20)
    $lbl.Location  = New-Object System.Drawing.Point(20, $y)
    $parent.Controls.Add($lbl)

    $val = New-Object System.Windows.Forms.Label
    $val.Text      = $value
    $val.Font      = New-Object System.Drawing.Font("Segoe UI", 9)
    $val.ForeColor = [System.Drawing.Color]::White
    $val.AutoSize  = $false
    $val.Size      = New-Object System.Drawing.Size(360, 20)
    $val.Location  = New-Object System.Drawing.Point(120, $y)
    $parent.Controls.Add($val)
}

Add-Row $form "File:"   $FileName   72
Add-Row $form "Threat:" $ThreatName 98
Add-Row $form "Path:"   $FilePath   124

# Separator
$sep = New-Object System.Windows.Forms.Panel
$sep.Size      = New-Object System.Drawing.Size(460, 1)
$sep.Location  = New-Object System.Drawing.Point(20, 158)
$sep.BackColor = [System.Drawing.Color]::FromArgb(70, 70, 70)
$form.Controls.Add($sep)

# Timer countdown label
$lblTimer = New-Object System.Windows.Forms.Label
$lblTimer.Text      = "Auto-quarantine in ${AutoCloseSeconds}s"
$lblTimer.Font      = New-Object System.Drawing.Font("Segoe UI", 8)
$lblTimer.ForeColor = [System.Drawing.Color]::FromArgb(140, 140, 140)
$lblTimer.AutoSize  = $true
$lblTimer.Location  = New-Object System.Drawing.Point(20, 168)
$form.Controls.Add($lblTimer)

# Buttons
function Add-ActionButton($parent, $text, $color, $x, $y, $action) {
    $btn = New-Object System.Windows.Forms.Button
    $btn.Text      = $text
    $btn.Font      = New-Object System.Drawing.Font("Segoe UI", 9, [System.Drawing.FontStyle]::Bold)
    $btn.ForeColor = [System.Drawing.Color]::White
    $btn.BackColor = $color
    $btn.FlatStyle = "Flat"
    $btn.FlatAppearance.BorderSize = 0
    $btn.Size      = New-Object System.Drawing.Size(130, 38)
    $btn.Location  = New-Object System.Drawing.Point($x, $y)
    $btn.Add_Click({
        $script:result = $action
        $form.Close()
    }.GetNewClosure())
    $parent.Controls.Add($btn)
}

Add-ActionButton $form "Quarantine" ([System.Drawing.Color]::FromArgb(180, 30, 30))  20  220 "QUARANTINE"
Add-ActionButton $form "Ignore"     ([System.Drawing.Color]::FromArgb(70, 70, 70))  170  220 "IGNORE"
Add-ActionButton $form "Details"    ([System.Drawing.Color]::FromArgb(30, 80, 160)) 320  220 "DETAILS"

# Countdown timer
$remaining = $AutoCloseSeconds
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 1000
$timer.Add_Tick({
    $script:remaining--
    $lblTimer.Text = "Auto-quarantine in ${script:remaining}s"
    if ($script:remaining -le 0) {
        $timer.Stop()
        $script:result = "QUARANTINE"
        $form.Close()
    }
})

$form.Add_Load({ $timer.Start() })
$form.Add_FormClosed({ $timer.Stop() })

[System.Windows.Forms.Application]::Run($form)

if ($ResultFile -ne "") {
    [System.IO.File]::WriteAllText($ResultFile, $result)
} else {
    Write-Output $result
}
