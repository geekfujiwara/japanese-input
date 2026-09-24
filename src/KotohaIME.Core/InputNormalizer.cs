using System.Text;

namespace KotohaIME.Core;

public static class InputNormalizer
{
    private const string HalfWidthAsciiTargets =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz -=;'\"<>|\\¥+_)(＆&^*%$#@[]{}:";

    private static readonly IReadOnlyDictionary<string, string> JapanesePunctuation =
        new Dictionary<string, string>
        {
            ["["] = "｢",
            ["]"] = "｣",
            ["!"] = "!",
            ["?"] = "?",
            ["/"] = "･",
            ["."] = "｡",
            ["「"] = "｢",
            ["」"] = "｣",
            ["！"] = "!",
            ["？"] = "?",
            ["・"] = "･",
            ["。"] = "｡",
        };

    public static bool ShouldPreserveImeComposition(string input) =>
        input.Length == 1 &&
        (input[0] == ' ' || input[0] is >= 'A' and <= 'Z' or >= 'a' and <= 'z');

    public static bool ShouldUseHalfWidthImeComposition(string input, ImeSettings settings)
    {
        if (!settings.IsEnabled || !settings.ForceHalfWidthAscii || string.IsNullOrEmpty(input) || input == " ")
        {
            return false;
        }

        string normalized = NormalizeAscii(input);
        return normalized.Length == 1 && HalfWidthAsciiTargets.Contains(normalized[0]);
    }

    public static bool TryNormalize(string input, ImeSettings settings, out string replacement)
    {
        replacement = input;

        if (!settings.IsEnabled || string.IsNullOrEmpty(input))
        {
            return false;
        }

        if (settings.ForceHalfWidthJapanesePunctuation &&
            JapanesePunctuation.TryGetValue(input, out string? punctuation))
        {
            replacement = punctuation;
            return replacement != input || input is "!" or "?";
        }

        if (!settings.ForceHalfWidthAscii)
        {
            return false;
        }

        string normalized = NormalizeAscii(input);

        if (normalized.Length == 1 && HalfWidthAsciiTargets.Contains(normalized[0]))
        {
            replacement = normalized;
            return true;
        }

        return false;
    }

    private static string NormalizeAscii(string input) =>
        input.Normalize(NormalizationForm.FormKC)
            .Replace("￥", "\\", StringComparison.Ordinal)
            .Replace("¥", "\\", StringComparison.Ordinal)
            .Replace("ー", "-", StringComparison.Ordinal);
}