using System;
using System.Globalization;
using System.Windows.Data;

namespace VCom.Client.Converters
{
    public class NullToBooleanConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            // If the value is not null, return true. Otherwise, return false.
            return value != null;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            // This is a one-way conversion, so we don't need to implement this method.
            throw new NotImplementedException();
        }
    }
}