Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

Get-ChildItem -Path . -Recurse -Include *.c, *.h -File | ForEach-Object {
    $filePath = $_.FullName

    # 读取原始字节内容
    $bytes = [System.IO.File]::ReadAllBytes($filePath)

    # 检查 UTF-8 BOM（EF BB BF）
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        # 去掉前 3 个字节
        $newBytes = $bytes[3..($bytes.Length - 1)]
        [System.IO.File]::WriteAllBytes($filePath, $newBytes)
        Write-Host "Removed BOM from: $filePath"
    }
}
