# ASCII-only PowerShell helper for Feishu cards on Windows runners.
# Chinese text is built from Unicode code points so Windows PowerShell 5.1
# (which reads BOM-less scripts as system ANSI/GBK) cannot mojibake literals.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-UnicodeString {
  param([Parameter(Mandatory = $true)][int[]]$Codes)
  return -join ($Codes | ForEach-Object { [char]$_ })
}

# StrictMode-safe property access for GitHub event JSON (optional fields).
function Get-NotePropertyValue {
  param(
    $Object,
    [Parameter(Mandatory = $true)][string]$Name,
    $Default = $null
  )
  if ($null -eq $Object) {
    return $Default
  }
  $property = $Object.PSObject.Properties[$Name]
  if ($null -eq $property) {
    return $Default
  }
  return $property.Value
}

function Get-FeishuLabels {
  return @{
    Repo       = (Get-UnicodeString 0x4ED3, 0x5E93)                         # 
    Branch     = (Get-UnicodeString 0x5206, 0x652F)                         # 
    Commit     = (Get-UnicodeString 0x63D0, 0x4EA4)                         # 
    Author     = (Get-UnicodeString 0x63D0, 0x4EA4, 0x8005)                 # 
    FailedJobs = (Get-UnicodeString 0x5931, 0x8D25, 0x4EFB, 0x52A1)         # 
    RunLink    = (Get-UnicodeString 0x8FD0, 0x884C, 0x94FE, 0x63A5)         # 
    ViewDetail = (Get-UnicodeString 0x67E5, 0x770B, 0x8BE6, 0x60C5)         # 
    PkgName    = (Get-UnicodeString 0x5305, 0x540D)                         # 
    Note       = (Get-UnicodeString 0x8BF4, 0x660E)                         # 
    PublishEnv = (Get-UnicodeString 0x53D1, 0x5E03, 0x73AF, 0x5883)         # 
    TriggerRef = ((Get-UnicodeString 0x89E6, 0x53D1) + ' ref')              #  ref
    SourceSha  = ((Get-UnicodeString 0x6E90) + ' SHA')                      #  SHA
    VersionSha = ((Get-UnicodeString 0x7248, 0x672C) + ' SHA')              #  SHA
    BuildFail  = (
      [string][char]0x274C + ' DXMT Nightly ' +
      (Get-UnicodeString 0x6784, 0x5EFA, 0x5931, 0x8D25)
    )                                                                       #  DXMT Nightly 
    BuildOk    = (
      [string][char]0x2705 + ' DXMT Nightly ' +
      (Get-UnicodeString 0x6784, 0x5EFA, 0x6210, 0x529F)
    )                                                                       #  DXMT Nightly 
    PubFail    = (
      [string][char]0x274C + ' DXMT ' +
      (Get-UnicodeString 0x7EC4, 0x4EF6, 0x53D1, 0x5E03, 0x5931, 0x8D25)
    )                                                                       #  DXMT 
    PubOk      = (
      [string][char]0x2705 + ' DXMT ' +
      (Get-UnicodeString 0x7EC4, 0x4EF6, 0x53D1, 0x5E03, 0x6210, 0x529F)
    )                                                                       #  DXMT 
    NightlyNote = (
      'push nightly ' +
      (Get-UnicodeString 0x4EC5, 0x6784, 0x5EFA, 0x6253, 0x5305) +
      (Get-UnicodeString 0xFF0C, 0x4E0D, 0x8DD1, 0x6D4B, 0x8BD5)
    )                                                                       # push nightly 
    ComponentNote = (
      (Get-UnicodeString 0x542B, 0x5B8C, 0x6574, 0x6D4B, 0x8BD5) +
      ' + ' +
      (Get-UnicodeString 0x540E, 0x7AEF, 0x7EC4, 0x4EF6, 0x4E0A, 0x4F20)
    )                                                                       #  + 
  }
}

function Send-FeishuCard {
  param(
    [Parameter(Mandatory = $true)][string]$WebhookUrl,
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][string]$Template,
    [Parameter(Mandatory = $true)][string]$Markdown,
    [string]$Subtitle = ''
  )

  if ([string]::IsNullOrWhiteSpace($WebhookUrl)) {
    Write-Host 'FEISHU_WEBHOOK_URL is not configured; skipping notification.'
    return
  }

  $header = [ordered]@{
    title    = [ordered]@{ tag = 'plain_text'; content = $Title }
    template = $Template
  }
  if (-not [string]::IsNullOrWhiteSpace($Subtitle)) {
    $header.subtitle = [ordered]@{ tag = 'plain_text'; content = $Subtitle }
  }

  $payload = [ordered]@{
    msg_type = 'interactive'
    card     = [ordered]@{
      schema = '2.0'
      config = [ordered]@{ update_multi = $true }
      header = $header
      body   = [ordered]@{
        direction = 'vertical'
        padding   = '12px 12px 12px 12px'
        elements  = @(
          [ordered]@{ tag = 'markdown'; content = $Markdown }
        )
      }
    }
  }

  # ConvertTo-Json emits \uXXXX for non-ASCII; body is UTF-8 bytes.
  $json = $payload | ConvertTo-Json -Depth 20 -Compress
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
  Invoke-RestMethod -Method Post -Uri $WebhookUrl `
    -ContentType 'application/json; charset=utf-8' `
    -Body $bytes | Out-Null
}

function Send-FeishuCardV1 {
  # Classic card shape used by GameHub-PC build notifications (lark_md).
  param(
    [Parameter(Mandatory = $true)][string]$WebhookUrl,
    [Parameter(Mandatory = $true)][string]$Title,
    [Parameter(Mandatory = $true)][string]$Template,
    [Parameter(Mandatory = $true)][string]$Markdown
  )

  if ([string]::IsNullOrWhiteSpace($WebhookUrl)) {
    Write-Host 'FEISHU_WEBHOOK_URL is not configured; skipping notification.'
    return
  }

  $payload = [ordered]@{
    msg_type = 'interactive'
    card     = [ordered]@{
      header = [ordered]@{
        title    = [ordered]@{ tag = 'plain_text'; content = $Title }
        template = $Template
      }
      elements = @(
        [ordered]@{
          tag  = 'div'
          text = [ordered]@{ tag = 'lark_md'; content = $Markdown }
        }
      )
    }
  }

  $json = $payload | ConvertTo-Json -Depth 20 -Compress
  $bytes = [System.Text.Encoding]::UTF8.GetBytes($json)
  Invoke-RestMethod -Method Post -Uri $WebhookUrl `
    -ContentType 'application/json; charset=utf-8' `
    -Body $bytes | Out-Null
}
