# Saves the running game's program image (D2RLoader.exe, which runs D2R) from memory into a file for analysis.
#
# Why: D2R's code is encrypted on disk and D2RLoader.exe fills its sections at start-up, so the code only exists in
# memory. This reads it (read-only: nothing in the game is changed) and writes it as a PE file whose section file
# offsets equal their RVAs, the same addresses plugins use. Unreadable pages are written as zeros. PowerShell port of
# d2rAI-toolkit's tools/dump_image.py, so no Python is needed.
#
# Use: start the game through D2RLoader and wait for the main menu, then in PowerShell:
#   powershell -ExecutionPolicy Bypass -File dump-game.ps1
# Output: D2RLoader_<timestamp>_dump.exe and a .zip of it on your Desktop (or -Out <folder>).
# The file is a copy of Blizzard's program: keep it private (never commit or publish it).
param(
    [string]$Out = [Environment]::GetFolderPath("Desktop"),
    [string]$Process = "D2RLoader"
)
$ErrorActionPreference = 'Stop'

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class GameDump {
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr OpenProcess(uint access, bool inherit, int pid);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ReadProcessMemory(IntPtr h, IntPtr addr, byte[] buf, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")]
    static extern bool CloseHandle(IntPtr h);
    [DllImport("psapi.dll", SetLastError = true)]
    static extern bool EnumProcessModulesEx(IntPtr h, IntPtr[] mods, uint cb, out uint needed, uint filter);

    static byte[] Read(IntPtr h, long addr, int size) {
        byte[] buf = new byte[size];
        IntPtr n;
        if (ReadProcessMemory(h, new IntPtr(addr), buf, new IntPtr(size), out n) && n.ToInt64() == size) return buf;
        // page by page; pages that cannot be read stay zero
        byte[] page = new byte[0x1000];
        for (int off = 0; off < size; off += 0x1000) {
            int chunk = Math.Min(0x1000, size - off);
            if (ReadProcessMemory(h, new IntPtr(addr + off), page, new IntPtr(chunk), out n))
                Array.Copy(page, 0, buf, off, (int)n.ToInt64());
        }
        return buf;
    }

    // Returns the image (memory layout) and its PE timestamp.
    public static byte[] Dump(int pid, out uint timestamp, out string report) {
        IntPtr h = OpenProcess(0x0400 | 0x0010, false, pid);  // QUERY_INFORMATION | VM_READ
        if (h == IntPtr.Zero) throw new Exception("OpenProcess failed (" + Marshal.GetLastWin32Error() + ")");
        try {
            IntPtr[] mods = new IntPtr[1];
            uint needed;
            if (!EnumProcessModulesEx(h, mods, (uint)IntPtr.Size, out needed, 0x03))
                throw new Exception("EnumProcessModulesEx failed (" + Marshal.GetLastWin32Error() + ")");
            long baseAddr = mods[0].ToInt64();
            byte[] head = Read(h, baseAddr, 0x1000);
            int pe = BitConverter.ToInt32(head, 0x3C);
            int nsec = BitConverter.ToUInt16(head, pe + 6);
            timestamp = BitConverter.ToUInt32(head, pe + 8);
            int opt = pe + 24;
            int sizeOfImage = BitConverter.ToInt32(head, opt + 56);
            byte[] image = Read(h, baseAddr, sizeOfImage);
            // memory layout: raw offset = RVA, raw size = virtual size
            int sec = opt + BitConverter.ToUInt16(image, pe + 20);
            for (int i = 0; i < nsec; i++) {
                int o = sec + 40 * i;
                Array.Copy(image, o + 8, image, o + 16, 4);   // SizeOfRawData = VirtualSize
                Array.Copy(image, o + 12, image, o + 20, 4);  // PointerToRawData = VirtualAddress
            }
            Array.Copy(BitConverter.GetBytes(baseAddr), 0, image, opt + 24, 8);  // ImageBase = where it was loaded
            report = String.Format("base 0x{0:x}, image 0x{1:x} bytes, {2} sections, timestamp 0x{3:x8}", baseAddr, sizeOfImage, nsec, timestamp);
            return image;
        } finally {
            CloseHandle(h);
        }
    }
}
"@

$proc = Get-Process -Name $Process -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $proc) { throw "$Process.exe is not running. Start the game through D2RLoader, wait for the main menu, then run this again." }
$timestamp = [uint32]0
$report = ""
$image = [GameDump]::Dump($proc.Id, [ref]$timestamp, [ref]$report)
Write-Output "pid $($proc.Id), $report"
$name = "D2RLoader_{0:x8}_dump" -f $timestamp
$exe = Join-Path $Out "$name.exe"
[IO.File]::WriteAllBytes($exe, $image)
$zip = Join-Path $Out "$name.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $exe -DestinationPath $zip -CompressionLevel Optimal
Write-Output ("wrote {0} ({1:N0} MB) and {2} ({3:N0} MB)" -f $exe, ($image.Length / 1MB), $zip, ((Get-Item $zip).Length / 1MB))
