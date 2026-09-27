using System.IO;
using System.Text;

namespace Astelio.Settings.Setup;

/// <summary>
/// The setup exe is this app with the MSI appended: [app][msi][path of the app next to the msi, UTF-8]
/// [int32 path length][int64 msi length]["ASTSETUP"]. The MSI leaves the app out (tools/Build-Installer.ps1), and the
/// setup writes its own first part next to the MSI, where Windows Installer looks for it.
/// </summary>
internal sealed record SetupPayload(long AppLength, long MsiLength, string AppPath)
{
    public const string MsiName = "AstelioIME.msi";
    private static readonly byte[] Magic = "ASTSETUP"u8.ToArray();
    private const int TrailerLength = 4 + 8;

    public static SetupPayload? Read(Stream file)
    {
        if (file.Length < TrailerLength + Magic.Length)
        {
            return null;
        }
        var trailer = new byte[TrailerLength + Magic.Length];
        file.Seek(-trailer.Length, SeekOrigin.End);
        file.ReadExactly(trailer);
        if (!trailer.AsSpan(TrailerLength).SequenceEqual(Magic))
        {
            return null;
        }
        int pathLength = BitConverter.ToInt32(trailer, 0);
        long msiLength = BitConverter.ToInt64(trailer, 4);
        long appLength = file.Length - trailer.Length - pathLength - msiLength;
        if (pathLength is <= 0 or > 260 || msiLength <= 0 || appLength <= 0)
        {
            return null;
        }
        var path = new byte[pathLength];
        file.Seek(appLength + msiLength, SeekOrigin.Begin);
        file.ReadExactly(path);
        string appPath = Encoding.UTF8.GetString(path);
        return IsSafeRelativePath(appPath) ? new SetupPayload(appLength, msiLength, appPath) : null;
    }

    /// <summary>Writes the MSI and the app into `directory`; returns the path of the MSI.</summary>
    public string Extract(Stream file, string directory)
    {
        string msi = Path.Combine(directory, MsiName);
        string app = Path.Combine(directory, AppPath);
        Directory.CreateDirectory(Path.GetDirectoryName(app)!);
        file.Seek(0, SeekOrigin.Begin);
        CopyPart(file, app, AppLength);
        CopyPart(file, msi, MsiLength);
        return msi;
    }

    /// <summary>Appends the MSI to a copy of the app (the build does the same in PowerShell).</summary>
    public static void Append(Stream target, Stream msi, string appPath)
    {
        byte[] path = Encoding.UTF8.GetBytes(appPath);
        target.Seek(0, SeekOrigin.End);
        long msiLength = msi.Length;
        msi.CopyTo(target);
        target.Write(path);
        target.Write(BitConverter.GetBytes(path.Length));
        target.Write(BitConverter.GetBytes(msiLength));
        target.Write(Magic);
    }

    private static bool IsSafeRelativePath(string path) =>
        !Path.IsPathRooted(path) && path.IndexOfAny(Path.GetInvalidPathChars()) < 0 &&
        !path.Split('\\', '/').Any(part => part is "" or "." or "..");

    private static void CopyPart(Stream source, string path, long length)
    {
        using var output = new FileStream(path, FileMode.Create, FileAccess.Write, FileShare.None);
        var buffer = new byte[1 << 20];
        while (length > 0)
        {
            int read = source.Read(buffer, 0, (int)Math.Min(buffer.Length, length));
            if (read == 0)
            {
                throw new EndOfStreamException();
            }
            output.Write(buffer, 0, read);
            length -= read;
        }
    }
}
