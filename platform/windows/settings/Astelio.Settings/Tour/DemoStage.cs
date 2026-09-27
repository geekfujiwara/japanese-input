using System.Windows;
using System.Windows.Controls;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Animation;
using System.Windows.Shapes;
using System.Windows.Threading;

namespace Astelio.Settings.Tour;

/// <summary>
/// Plays a demo: a text field with the composition, the candidate window, the key just pressed, a caption and the
/// input mode icon. Follows the Windows setting for animations (no blinking or flashing when they are off).
/// </summary>
internal sealed class DemoStage : UserControl
{
    private static readonly Brush Accent = new LinearGradientBrush(Color.FromRgb(0x6B, 0x3F, 0xE6), Color.FromRgb(0x8A, 0x63, 0xFF), 45);

    private readonly DispatcherTimer _timer = new();
    private readonly TextBlock _text = new() { FontSize = 20, TextWrapping = TextWrapping.Wrap, MinHeight = 30 };
    private readonly ContentControl _popup = new() { Margin = new Thickness(0, 8, 0, 0), HorizontalAlignment = HorizontalAlignment.Left };
    private readonly StackPanel _keys = new() { Orientation = Orientation.Horizontal, MinHeight = 38 };
    private readonly TextBlock _caption = new() { FontSize = 14, TextWrapping = TextWrapping.Wrap, VerticalAlignment = VerticalAlignment.Center };
    private readonly Border _mode = new() { Width = 34, Height = 34, CornerRadius = new CornerRadius(8), BorderThickness = new Thickness(1) };
    private readonly TextBlock _modeText = new() { FontWeight = FontWeights.Bold, FontSize = 15, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center };
    private Demo? _demo;
    private int _index;
    private string _currentMode = "あ";

    /// <summary>Raised when the last frame of the demo has been shown for a while.</summary>
    public event EventHandler? Finished;

    public DemoStage()
    {
        var screen = new Border
        {
            CornerRadius = new CornerRadius(12),
            BorderThickness = new Thickness(1),
            Padding = new Thickness(16, 12, 16, 12),
            Child = _text,
        };
        screen.SetResourceReference(Border.BackgroundProperty, "SolidBackgroundFillColorBaseBrush");
        screen.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");

        _mode.Child = _modeText;
        _mode.RenderTransformOrigin = new Point(0.5, 0.5);
        _mode.RenderTransform = new ScaleTransform(1, 1);
        _mode.SetResourceReference(Border.BackgroundProperty, "SolidBackgroundFillColorBaseBrush");
        _mode.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");
        _caption.SetResourceReference(TextBlock.ForegroundProperty, "TextFillColorSecondaryBrush");

        var bottom = new DockPanel { Margin = new Thickness(0, 10, 0, 0), LastChildFill = true };
        DockPanel.SetDock(_mode, Dock.Right);
        bottom.Children.Add(_mode);
        bottom.Children.Add(_caption);

        var layout = new Grid();
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new RowDefinition { Height = new GridLength(1, GridUnitType.Star), MinHeight = 170 });
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        layout.RowDefinitions.Add(new RowDefinition { Height = GridLength.Auto });
        Grid.SetRow(_popup, 1);
        Grid.SetRow(_keys, 2);
        Grid.SetRow(bottom, 3);
        layout.Children.Add(screen);
        layout.Children.Add(_popup);
        layout.Children.Add(_keys);
        layout.Children.Add(bottom);

        Content = new Border
        {
            CornerRadius = new CornerRadius(16),
            Padding = new Thickness(20),
            Background = new LinearGradientBrush(Color.FromArgb(0x26, 0x6B, 0x3F, 0xE6), Color.FromArgb(0x26, 0x12, 0xC8, 0xE6), 30),
            Child = layout,
        };
        _timer.Tick += (_, _) => Next();
        Unloaded += (_, _) => _timer.Stop();
    }

    public void Play(Demo demo)
    {
        _demo = demo;
        _index = 0;
        _currentMode = "あ";
        Show(demo.Frames[0]);
    }

    public void Stop() => _timer.Stop();

    private static bool Animate => SystemParameters.ClientAreaAnimation;

    private void Next()
    {
        _timer.Stop();
        if (_demo is null)
        {
            return;
        }
        if (++_index >= _demo.Frames.Length)
        {
            Finished?.Invoke(this, EventArgs.Empty);
            if (_demo is not null && _index >= _demo.Frames.Length)
            {
                Play(_demo);
            }
            return;
        }
        Show(_demo.Frames[_index]);
    }

    private void Show(DemoFrame frame)
    {
        bool last = _demo is not null && ReferenceEquals(frame, _demo.Frames[^1]);
        _timer.Interval = TimeSpan.FromMilliseconds(frame.Milliseconds + (last ? 1800 : 0));
        _timer.Start();

        ShowText(frame);
        _popup.Content = frame.Menu is not null ? MenuView(frame) : frame.Candidates is not null ? CandidateView(frame.Candidates) : null;
        ShowKey(frame.Key);
        _caption.Text = frame.Caption;
        if (frame.Mode is not null)
        {
            _currentMode = frame.Mode;
        }
        _modeText.Text = _currentMode;
        if (frame.Flash && Animate)
        {
            var grow = new DoubleAnimation(1.5, 1, TimeSpan.FromMilliseconds(800)) { EasingFunction = new QuadraticEase() };
            ((ScaleTransform)_mode.RenderTransform).BeginAnimation(ScaleTransform.ScaleXProperty, grow);
            ((ScaleTransform)_mode.RenderTransform).BeginAnimation(ScaleTransform.ScaleYProperty, grow);
        }
    }

    private void ShowText(DemoFrame frame)
    {
        _text.Inlines.Clear();
        _text.Inlines.Add(new Run(frame.Committed));
        if (frame.Segments is not null)
        {
            for (int i = 0; i < frame.Segments.Length; i++)
            {
                _text.Inlines.Add(Underlined(frame.Segments[i], i == frame.Focus ? UnderlineKind.Focus : UnderlineKind.Converted));
                if (i + 1 < frame.Segments.Length)
                {
                    _text.Inlines.Add(new Run("\u2009"));
                }
            }
            return;
        }
        if (frame.CaretInComposition >= 0)
        {
            _text.Inlines.Add(Underlined(frame.Composition[..frame.CaretInComposition], UnderlineKind.Typing));
            _text.Inlines.Add(Caret());
            _text.Inlines.Add(Underlined(frame.Composition[frame.CaretInComposition..], UnderlineKind.Typing));
            return;
        }
        if (frame.Composition.Length > 0)
        {
            _text.Inlines.Add(Underlined(frame.Composition, UnderlineKind.Typing));
        }
        _text.Inlines.Add(Caret());
    }

    private enum UnderlineKind { Typing, Converted, Focus }

    private Run Underlined(string text, UnderlineKind kind)
    {
        var pen = new Pen(kind == UnderlineKind.Focus ? Accent : (Brush)FindResource("TextFillColorSecondaryBrush"),
            kind == UnderlineKind.Focus ? 3 : 1.5);
        if (kind == UnderlineKind.Typing)
        {
            pen.DashStyle = DashStyles.Dot;
        }
        var run = new Run(text);
        run.TextDecorations.Add(new TextDecoration(TextDecorationLocation.Underline, pen, 2, TextDecorationUnit.Pixel, TextDecorationUnit.Pixel));
        return run;
    }

    private static InlineUIContainer Caret()
    {
        var caret = new Rectangle { Width = 2, Height = 22, Fill = Accent, Margin = new Thickness(1, 0, 1, -4) };
        if (Animate)
        {
            var blink = new DoubleAnimationUsingKeyFrames { Duration = TimeSpan.FromSeconds(1), RepeatBehavior = RepeatBehavior.Forever };
            blink.KeyFrames.Add(new DiscreteDoubleKeyFrame(1, KeyTime.FromTimeSpan(TimeSpan.Zero)));
            blink.KeyFrames.Add(new DiscreteDoubleKeyFrame(0, KeyTime.FromTimeSpan(TimeSpan.FromMilliseconds(500))));
            caret.BeginAnimation(OpacityProperty, blink);
        }
        return new InlineUIContainer(caret) { BaselineAlignment = BaselineAlignment.Center };
    }

    private void ShowKey(string? key)
    {
        _keys.Children.Clear();
        if (key is null)
        {
            return;
        }
        var cap = new Border
        {
            CornerRadius = new CornerRadius(8),
            BorderThickness = new Thickness(1, 1, 1, 2),
            Padding = new Thickness(12, 5, 12, 5),
            Margin = new Thickness(0, 10, 0, 0),
            Background = Accent,
            Child = new TextBlock { Text = key, FontFamily = new FontFamily("Segoe UI"), FontSize = 14, Foreground = Brushes.White },
            RenderTransform = new TranslateTransform(0, 2),
        };
        cap.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");
        _keys.Children.Add(cap);
        // The key comes back up after a moment, as a key press would.
        var up = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(180) };
        up.Tick += (_, _) =>
        {
            up.Stop();
            cap.SetResourceReference(Border.BackgroundProperty, "SolidBackgroundFillColorBaseBrush");
            ((TextBlock)cap.Child).SetResourceReference(TextBlock.ForegroundProperty, "TextFillColorPrimaryBrush");
            cap.RenderTransform = Transform.Identity;
            cap.BorderThickness = new Thickness(1, 1, 1, 4);
        };
        up.Start();
    }

    private Border PopupFrame(UIElement child)
    {
        var frame = new Border
        {
            CornerRadius = new CornerRadius(10),
            BorderThickness = new Thickness(1),
            Padding = new Thickness(6),
            Child = child,
            Effect = new System.Windows.Media.Effects.DropShadowEffect { BlurRadius = 18, ShadowDepth = 4, Opacity = 0.18 },
        };
        frame.SetResourceReference(Border.BackgroundProperty, "SolidBackgroundFillColorBaseBrush");
        frame.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");
        if (Animate)
        {
            frame.BeginAnimation(OpacityProperty, new DoubleAnimation(0, 1, TimeSpan.FromMilliseconds(160)));
        }
        return frame;
    }

    private Border CandidateView(DemoCandidates candidates)
    {
        var columns = new StackPanel { Orientation = Orientation.Horizontal };
        var list = new StackPanel { MinWidth = 120 };
        for (int i = 0; i < candidates.Items.Length; i++)
        {
            string? tag = i == candidates.Typo ? "もしかして" : null;
            list.Children.Add(Row((i + 1).ToString(), candidates.Items[i], i == candidates.Selected, tag));
        }
        columns.Children.Add(list);
        if (candidates.Emoji is not null)
        {
            var emoji = new StackPanel { Margin = new Thickness(6, 0, 0, 0), MinWidth = 56 };
            for (int i = 0; i < candidates.Emoji.Length; i++)
            {
                emoji.Children.Add(Row((i + 1).ToString(), candidates.Emoji[i], i == candidates.EmojiSelected, null));
            }
            var separator = new Border { BorderThickness = new Thickness(1, 0, 0, 0), Margin = new Thickness(6, 2, 0, 2), Child = emoji };
            separator.SetResourceReference(Border.BorderBrushProperty, "CardStrokeColorDefaultBrush");
            columns.Children.Add(separator);
        }
        return PopupFrame(columns);
    }

    private Border MenuView(DemoFrame frame)
    {
        var list = new StackPanel { MinWidth = 180 };
        for (int i = 0; i < frame.Menu!.Length; i++)
        {
            list.Children.Add(Row(null, frame.Menu[i], i == frame.MenuSelection, null));
        }
        return PopupFrame(list);
    }

    private static Border Row(string? number, string text, bool selected, string? tag)
    {
        var line = new DockPanel();
        if (number is not null)
        {
            var digit = new TextBlock { Text = number, FontSize = 12, Width = 16, VerticalAlignment = VerticalAlignment.Center, Opacity = 0.7 };
            line.Children.Add(digit);
        }
        if (tag is not null)
        {
            var note = new TextBlock { Text = tag, FontSize = 11, Margin = new Thickness(12, 0, 0, 0), VerticalAlignment = VerticalAlignment.Center, Opacity = 0.8 };
            DockPanel.SetDock(note, Dock.Right);
            line.Children.Add(note);
        }
        line.Children.Add(new TextBlock { Text = text, FontSize = 16 });
        var row = new Border { CornerRadius = new CornerRadius(6), Padding = new Thickness(8, 3, 10, 3), Child = line };
        if (selected)
        {
            row.Background = Accent;
            TextElement.SetForeground(row, Brushes.White);
        }
        return row;
    }
}
