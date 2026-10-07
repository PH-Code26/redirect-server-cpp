$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $scriptDir

$pfxPath   = Join-Path $scriptDir "cert.pfx"
$certPath  = Join-Path $scriptDir "cert.pem"
$keyPath   = Join-Path $scriptDir "key.pem"
$aliasesFile = Join-Path $scriptDir "aliases.json"
$password    = "alias123"

if (-not (Test-Path $aliasesFile)) {
    Write-Host "No aliases.json, skipping."
    exit 0
}

$aliases = Get-Content $aliasesFile -Raw | ConvertFrom-Json
$names = $aliases.PSObject.Properties.Name

if ($names.Count -eq 0) {
    Write-Host "No aliases in aliases.json, skipping."
    exit 0
}

if (Test-Path $pfxPath) {
    Write-Host "cert.pfx already exists, skipping generation."
    exit 0
}

# Generate self-signed certificate
$dnsParams = @()
foreach ($n in $names) { $dnsParams += $n }

$cert = New-SelfSignedCertificate `
    -DnsName $dnsParams `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -KeyUsage DigitalSignature, KeyEncipherment `
    -Type SSLServerAuthentication `
    -FriendlyName "URL-Alias-Redirect-Cpp"

# Export PFX (backward compat)
$securePass = ConvertTo-SecureString -String $password -Force -AsPlainText
Export-PfxCertificate -Cert $cert -FilePath $pfxPath -Password $securePass | Out-Null
Write-Host "Exported: cert.pfx"

# Also export PEM: cert + key
# We need the private key exportable. New-SelfSignedCertificate generates it with
# KeyExportPolicy = Exportable by default in CurrentUser\My.
# Export public cert as PEM via temporary CER, then convert
$tmpCer = Join-Path $env:TEMP "tmp-alias-cert.cer"
Export-Certificate -Cert $cert -FilePath $tmpCer -Type CERT | Out-Null

$certBytes = [System.IO.File]::ReadAllBytes($tmpCer)
$certB64   = [System.Convert]::ToBase64String($certBytes, [System.Base64FormattingOptions]::InsertLineBreaks)
$certPem   = "-----BEGIN CERTIFICATE-----`r`n$certB64`r`n-----END CERTIFICATE-----`r`n"
[System.IO.File]::WriteAllText($certPath, $certPem)
Write-Host "Exported: cert.pem"
Remove-Item $tmpCer -Force -ErrorAction SilentlyContinue

# Export private key via temporary PFX -> OpenSSL
# PowerShell cannot directly export private key as PEM.
# Try OpenSSL first:
$openssl = Get-Command openssl -ErrorAction SilentlyContinue
if ($openssl) {
    $tmpPfx = Join-Path $env:TEMP "tmp-alias.pfx"
    Export-PfxCertificate -Cert $cert -FilePath $tmpPfx -Password $securePass | Out-Null
    & openssl pkcs12 -in $tmpPfx -out $keyPath -nocerts -nodes -passin "pass:$password" 2>$null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Exported: key.pem (via OpenSSL)"
    } else {
        Write-Host "[WARN] OpenSSL key export failed. HTTPS needs key.pem; try manual export."
    }
    Remove-Item $tmpPfx -Force -ErrorAction SilentlyContinue
} else {
    # Fallback: use certutil to export key (only works if cert has exportable key)
    try {
        $thumb = $cert.Thumbprint
        $tmpPfx = Join-Path $env:TEMP "tmp-alias.pfx"
        Export-PfxCertificate -Cert $cert -FilePath $tmpPfx -Password $securePass | Out-Null
        
        # Use .NET to extract key from PFX
        $pfxBytes = [System.IO.File]::ReadAllBytes($tmpPfx)
        $x509 = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2
        $x509.Import($pfxBytes, $password, [System.Security.Cryptography.X509Certificates.X509KeyStorageFlags]::Exportable)
        
        if ($x509.HasPrivateKey) {
            $rsa = [System.Security.Cryptography.X509Certificates.RSACertificateExtensions]::GetRSAPrivateKey($x509)
            if ($rsa) {
                $keyBytes = $rsa.ExportRSAPrivateKey()
                $keyB64   = [System.Convert]::ToBase64String($keyBytes, [System.Base64FormattingOptions]::InsertLineBreaks)
                $keyPem   = "-----BEGIN RSA PRIVATE KEY-----`r`n$keyB64`r`n-----END RSA PRIVATE KEY-----`r`n"
                [System.IO.File]::WriteAllText($keyPath, $keyPem)
                Write-Host "Exported: key.pem (via .NET)"
            } else {
                Write-Host "[WARN] Cannot export RSA key. Install OpenSSL for HTTPS support."
            }
        } else {
            Write-Host "[WARN] No exportable private key. Install OpenSSL for HTTPS support."
        }
        $x509.Dispose()
        Remove-Item $tmpPfx -Force -ErrorAction SilentlyContinue
    } catch {
        Write-Host "[WARN] key.pem export failed: $_"
        Write-Host "Install OpenSSL for automatic key export, or convert cert.pfx manually."
    }
}

# Clean up cert store
Remove-Item "Cert:\CurrentUser\My\$($cert.Thumbprint)" -Force -ErrorAction SilentlyContinue

Write-Host "Domains: $($dnsParams -join ', ')"