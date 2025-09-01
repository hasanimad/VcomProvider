using System.Windows;
using VCom.Client;

public partial class App : Application
{
    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);
        var mainWindow = new MainWindow();
        mainWindow.Show();

        // Add this Exit event handler
        this.Exit += (s, args) =>
        {
            if (mainWindow.DataContext is IDisposable disposable)
            {
                disposable.Dispose();
            }
        };
    }
}