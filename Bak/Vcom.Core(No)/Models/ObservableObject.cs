using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace VCom.Core.Models
{
    /// <summary>
    /// A base class that implements INotifyPropertyChanged to signal the UI
    /// when a property's value changes. This is crucial for data binding.
    /// </summary>
    public abstract class ObservableObject : INotifyPropertyChanged
    {
        public event PropertyChangedEventHandler? PropertyChanged;

        /// <summary>
        /// Notifies listeners that a property value has changed.
        /// </summary>
        /// <param name="propertyName">Name of the property used to notify listeners.</param>
        protected void OnPropertyChanged([CallerMemberName] string? propertyName = null)
        {
            PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
        }
    }
}
