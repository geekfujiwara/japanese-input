using System.IO;
using System.Text;
using Astelio.Settings.Setup;
using Astelio.Settings.Tour;

namespace Astelio.Settings.Tests;

// T-I12-1: the setup exe carries the MSI and writes both where Windows Installer looks for them.
public sealed class SetupPayloadTests : IDisposable
{
    private readonly string _directory = Directory.CreateTempSubdirectory("astelio-setup-test-").FullName;

    public void Dispose() => Directory.Delete(_directory, recursive: true);

    [Fact]
    public void AppendedMsiIsExtractedWithTheAppNextToIt()
    {
        byte[] app = Enumerable.Range(0, 3000).Select(i => (byte)i).ToArray();
        byte[] msi = Encoding.ASCII.GetBytes("pretend this is an MSI");
        using var setup = new MemoryStream();
        setup.Write(app);
        SetupPayload.Append(setup, new MemoryStream(msi), "AstelioSettings.exe");

        SetupPayload payload = SetupPayload.Read(setup)!;
        string msiPath = payload.Extract(setup, _directory);

        Assert.Equal(Path.Combine(_directory, "AstelioIME.msi"), msiPath);
        Assert.Equal(msi, File.ReadAllBytes(msiPath));
        Assert.Equal(app, File.ReadAllBytes(Path.Combine(_directory, "AstelioSettings.exe")));
    }

    [Fact]
    public void AppsWithoutAPayloadAreNotSetups()
    {
        Assert.Null(SetupPayload.Read(new MemoryStream(new byte[100])));
        Assert.Null(SetupPayload.Read(new MemoryStream()));
    }

    [Theory]
    [InlineData(@"..\AstelioSettings.exe")]
    [InlineData(@"C:\Windows\AstelioSettings.exe")]
    [InlineData(@"\AstelioSettings.exe")]
    public void PathsOutsideTheFolderAreRefused(string path)
    {
        using var setup = new MemoryStream();
        setup.Write(new byte[10]);
        SetupPayload.Append(setup, new MemoryStream(new byte[5]), path);

        Assert.Null(SetupPayload.Read(setup));
    }
}

public sealed class MsiProgressTests
{
    [Fact]
    public void FollowsTheScriptAndTheExecution()
    {
        var progress = new MsiProgress();
        progress.OnProgress("1: 0 2: 100 3: 0 4: 1 ");
        progress.OnProgress("1: 2 2: 50 3: 0 ");
        Assert.Equal(0.05, progress.Fraction, 3);

        progress.OnProgress("1: 0 2: 1000 3: 0 4: 0 ");
        progress.OnProgress("1: 1 2: 100 3: 1 ");
        progress.OnActionData();
        progress.OnActionData();
        Assert.Equal(0.1 + 0.9 * 0.2, progress.Fraction, 3);

        progress.OnProgress("1: 3 2: 1000 ");
        Assert.Equal(0.1 + 0.9 * 0.1, progress.Fraction, 3);
    }

    [Fact]
    public void RollbackGoesBackwards()
    {
        var progress = new MsiProgress();
        progress.OnProgress("1: 0 2: 100 3: 1 4: 0 ");
        progress.OnProgress("1: 2 2: 25 3: 0 ");

        Assert.Equal(0.1 + 0.9 * 0.75, progress.Fraction, 3);
    }

    [Theory]
    [InlineData("Action 17:10:30: InstallFiles. Copying new files", "ファイルをコピーしています")]
    [InlineData("アクション 17:10:31: RegisterNative。", "入力方式を登録しています")]
    [InlineData("Action 9:01:02: RemoveExistingProducts.", "前の版を削除しています")]
    [InlineData("something else", "インストールしています")]
    public void DescribesTheActions(string message, string expected)
    {
        Assert.Equal(expected, MsiProgress.Describe(message));
    }
}

// T-U03-1: every scene of the tour can be shown.
public sealed class DemosTests
{
    [Fact]
    public void EveryFrameCanBeShown()
    {
        IReadOnlyList<Demo> demos = Demos.All(new DateTime(2026, 9, 27));

        Assert.True(demos.Count >= 6);
        foreach (Demo demo in demos)
        {
            Assert.False(string.IsNullOrWhiteSpace(demo.Title));
            Assert.False(string.IsNullOrWhiteSpace(demo.Description));
            Assert.NotEmpty(demo.Frames);
            foreach (DemoFrame frame in demo.Frames)
            {
                Assert.False(string.IsNullOrWhiteSpace(frame.Caption), demo.Title);
                Assert.InRange(frame.CaretInComposition, -1, frame.Composition.Length);
                if (frame.Segments is not null)
                {
                    Assert.InRange(frame.Focus, 0, frame.Segments.Length - 1);
                }
                if (frame.Candidates is { } candidates)
                {
                    Assert.InRange(candidates.Selected, -1, candidates.Items.Length - 1);
                    Assert.InRange(candidates.EmojiSelected, -1, (candidates.Emoji?.Length ?? 0) - 1);
                }
                if (frame.Menu is not null)
                {
                    Assert.InRange(frame.MenuSelection, -1, frame.Menu.Length - 1);
                }
            }
        }
    }

    [Fact]
    public void DatesAreToday()
    {
        Demo dates = Demos.All(new DateTime(2026, 9, 27)).Single(demo => demo.Title.StartsWith("日付"));

        Assert.Contains(dates.Frames, frame => frame.Committed.StartsWith("2026-09-27"));
    }
}
