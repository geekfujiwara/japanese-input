namespace Astelio.Settings.Tests;

public sealed class CommandLineTests
{
    [Fact]
    public void ReadsTheAppAndThePage()
    {
        CommandLine args = CommandLine.Parse(["--app", "Notepad.exe", "--page", "history"]);

        Assert.Equal("notepad.exe", args.App);
        Assert.Equal("history", args.Page);
    }

    [Fact]
    public void NoArgumentsOpenTheFirstPage()
    {
        Assert.Equal(new CommandLine(null, null), CommandLine.Parse([]));
        Assert.Equal(new CommandLine(null, null), CommandLine.Parse(["--app"]));
        Assert.Null(CommandLine.Parse(["--app", ""]).App);
    }
}
