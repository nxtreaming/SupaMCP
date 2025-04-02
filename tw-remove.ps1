Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass

Get-ChildItem -Path . -Recurse -Include *.c, *.h -File |
    ForEach-Object {
        $filePath = $_.FullName
        $trimmedLines = Get-Content $filePath | ForEach-Object { $_.TrimEnd() }
        Set-Content -Path $filePath -Value $trimmedLines -Encoding UTF8
    }
